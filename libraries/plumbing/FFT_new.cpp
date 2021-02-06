

#include "plumbing/defs.h"
#include "plumbing/timing.h"
#include "plumbing/FFT_new.h"
#include "fftw3.h"

#include <mpi.h>

// just some values here
#define WRK_GATHER_TAG  42
#define WRK_SCATTER_TAG  43


timer fft_timer("FFT total time");
static timer fftw_plan_timer("  FFTW plan");
static timer fft_MPI_timer("  MPI in FFT");
static timer fftw_execute_timer("  FFTW execute");
timer fft_reshuffle_timer("  data reshuffle");
static timer fft_buffer_timer("  copy fftw buffers");
timer fft_collect_timer("  copy payload");
timer fft_save_timer("  save result");


struct fftnode_struct {
    int node;                // node rank to send stuff for fft:ing
    int size_to_dir;         // size of "node" to fft-dir
    int column_offset;       // first perp-plane column to be handled by "node"
    int column_number;       // and number of columns to be sent
    int recv_buf_size;       // size of my fft collect buffer (in units of elements*cmplx_size)
                             // for stuff received from / returned to "node"
    char * work_in;          // where to hang the fft collect buffers
    char * work_out;

    MPI_Request send_request, receive_request;
};



static direction fft_dir;
static int elements;
static int cmplx_size;
static int transform_dir;
static bool is_float_fft;

static int  my_columns[NDIM];  // how many columns does this node take care of

static std::vector<fftnode_struct> fft_comms[NDIM];

static fftw_plan fftwplan_d;   // should we save the plans?  probably, if too expensive
static fftw_complex * RESTRICT fftwbuf_d;
static fftwf_plan fftwplan_f;   
static fftwf_complex * RESTRICT fftwbuf_f;


int fft_get_buffer_offsets( const direction dir, const int elements,
                            CoordinateVector & offset, CoordinateVector & nmin ) {
    
    offset[dir] = 1;
    nmin = lattice->mynode.min;

    int element_offset = lattice->mynode.size[dir];
    int s = element_offset * elements;


    foralldir(d) if (d != dir) {
        offset[d] = s;
        s *= lattice->mynode.size[d];
    }

    return element_offset;
}

/// THis is to be called before fft to direction dir

void init_fft_direction( direction dir, int _elements, int T_size, fft_direction fftdir,
                         void * const buf_in, void * const buf_out ) {

    fft_dir = dir;
    elements = _elements;
    cmplx_size = T_size / elements;


    transform_dir = (fftdir == fft_direction::forward) ? FFTW_FORWARD : FFTW_BACKWARD;

    fftw_plan_timer.start();

    // allocate here fftw plans.  TODO: perhaps store, if plans take appreciable time

    if (cmplx_size == sizeof(Cmplx<double>)) {
        is_float_fft = false;
        fftwbuf_d  = (fftw_complex *)fftw_malloc(sizeof(fftw_complex) * lattice->size(dir));
        fftwplan_d = fftw_plan_dft_1d( lattice->size(dir), fftwbuf_d, fftwbuf_d, transform_dir, FFTW_ESTIMATE);

    } else if (cmplx_size == sizeof(Cmplx<float>)) {
        is_float_fft = true;
        fftwbuf_f  = (fftwf_complex *)fftwf_malloc(sizeof(fftwf_complex) * lattice->size(dir));
        fftwplan_f = fftwf_plan_dft_1d( lattice->size(dir), fftwbuf_f, fftwbuf_f, transform_dir, FFTW_ESTIMATE);
    }

    fftw_plan_timer.stop();

    // Set up the fft_comms -struct, if not yet done

    if (fft_comms[dir].size() == 0) {  
        // basic structs not yet set, do it here

        fft_comms[dir].resize( lattice->nodes.n_divisions[dir] );

        int nodenumber = 0;
        for (const node_info & n : lattice->nodes.nodelist) {
            bool is_in_column = true;
            foralldir(d) if (d != dir && n.min[d] != lattice->mynode.min[d]) {
                is_in_column = false;
                break;
            }

            /// store the nodes in the fft_comms -list in the right order -
            /// nodes may be reordered by some weird layout
            if (is_in_column) {
                fftnode_struct fn;
                fn.node = nodenumber;
                fn.size_to_dir = n.size[dir];
                for (int i=0; i<lattice->nodes.n_divisions[dir]; i++) {
                    if (n.min[dir] == lattice->nodes.divisors[dir][i] ) {
                        fft_comms[dir].at(i) = fn;
                    }
                }
            }
            ++nodenumber;
        }

        int total_columns = lattice->mynode.sites / lattice->mynode.size[dir];

        int nodes = fft_comms[dir].size();

        // column offset and number are used for sending
        int i = 0;
        for (fftnode_struct & fn : fft_comms[dir]) {
            fn.column_offset = ((i * total_columns) / nodes) * lattice->mynode.size[dir]; 
            fn.column_number = (((i+1) * total_columns) / nodes) * lattice->mynode.size[dir] 
                            - fn.column_offset;

            if (fn.node == hila::myrank()) {
                my_columns[dir] = fn.column_number/lattice->mynode.size[dir];
            }
            i++;
        }

        for (fftnode_struct & fn : fft_comms[dir]) {
            fn.recv_buf_size = my_columns[dir]*fn.size_to_dir;
        }

    } // setup


    // allocate work arrays which are used to feed fftw and get the results


    for ( fftnode_struct & fn : fft_comms[dir]) {

        if (fn.node != hila::myrank() ) {

            // usually, out/in buffer is the same
            fn.work_out = fn.work_in = 
                (char *)memalloc(fn.recv_buf_size * cmplx_size * elements);

        } else {

            // for local node, point directly to input/output arrays

            fn.work_in  = (char *)buf_in  + fn.column_offset * cmplx_size * elements;
            fn.work_out = (char *)buf_out + fn.column_offset * cmplx_size * elements;
        }

    }
}




// now all work buffers are ready, slice through the data

template <>
void fft_execute<Cmplx<double>>() {
    int n_fft = my_columns[fft_dir] * elements;

    for (int i=0; i<n_fft; i++) {
        // collect stuff from buffers

        fft_buffer_timer.start();

        fftw_complex * cp = fftwbuf_d;
        for (auto & fn : fft_comms[fft_dir]) {
            memcpy(cp, fn.work_in + i*fn.size_to_dir*sizeof(fftw_complex), 
                   sizeof(fftw_complex)*fn.size_to_dir );
            cp += fn.size_to_dir;
        }

        fft_buffer_timer.stop();
 
 
        // do the fft
        fftw_execute_timer.start();
 
        fftw_execute( fftwplan_d );
 
        fftw_execute_timer.stop();
  

        fft_buffer_timer.start();

        cp = fftwbuf_d;
        for (auto & fn : fft_comms[fft_dir]) {
            memcpy(fn.work_out + i*fn.size_to_dir*sizeof(fftw_complex), cp,
                   sizeof(fftw_complex)*fn.size_to_dir );
            cp += fn.size_to_dir;
        }
               

        fft_buffer_timer.stop();
    }
}

template <>
void fft_execute<Cmplx<float>>() {
    int n_fft = my_columns[fft_dir] * elements;

    for (int i=0; i<n_fft; i++) {
        // collect stuff from buffers

        fft_buffer_timer.start();

        fftwf_complex * cp = fftwbuf_f;
        for (auto & fn : fft_comms[fft_dir]) {
            memcpy(cp, fn.work_in + i*fn.size_to_dir*sizeof(fftwf_complex), 
                   sizeof(fftwf_complex)*fn.size_to_dir );
            cp += fn.size_to_dir;
        }

        fft_buffer_timer.stop();

        // do the fft
        fftw_execute_timer.start();
        fftwf_execute( fftwplan_f );
        fftw_execute_timer.stop();

        fft_buffer_timer.start();

        cp = fftwbuf_f;
        for (auto & fn : fft_comms[fft_dir]) {
            memcpy(fn.work_out + i*fn.size_to_dir*sizeof(fftwf_complex), cp,
                   sizeof(fftwf_complex)*fn.size_to_dir );
            cp += fn.size_to_dir;
        }

        fft_buffer_timer.stop();
    }
}




void fft_post_gather() {
    
    fft_MPI_timer.start();
    for ( auto & fn : fft_comms[fft_dir] ) {
        if (fn.node != hila::myrank() ) {

            MPI_Irecv( fn.work_in, fn.recv_buf_size * cmplx_size * elements,
                       MPI_BYTE, fn.node, WRK_GATHER_TAG, lattice->mpi_comm_lat, &fn.receive_request );
        }
    }
    fft_MPI_timer.stop();
}   
   
void fft_start_gather( void * buffer ) {

    fft_MPI_timer.start();
    for (auto & fn: fft_comms[fft_dir]) if (fn.node != hila::myrank() ) {
        char * p = (char *)buffer + fn.column_offset * (cmplx_size * elements);
        int n = fn.column_number * (cmplx_size * elements);

        MPI_Isend( p, n, MPI_BYTE, fn.node, WRK_GATHER_TAG, lattice->mpi_comm_lat, &fn.send_request );
    }

    fft_MPI_timer.stop();
}

void fft_wait_send() {

    fft_MPI_timer.start();

    int n = fft_comms[fft_dir].size() -1;
    if (n > 0) {
        MPI_Request rr[n];
        MPI_Status stat[n];
        int i = 0;
        for (auto & ft : fft_comms[fft_dir]) if (ft.node != hila::myrank()) {
            rr[i++] = ft.send_request;
        }
        MPI_Waitall(n, rr, stat);
    }

    fft_MPI_timer.stop();
}

void fft_wait_receive() {

    fft_MPI_timer.start();

    int n = fft_comms[fft_dir].size() -1;
    if (n > 0) {
        MPI_Request rr[n];
        MPI_Status stat[n];
        int i = 0;
        for (auto & ft : fft_comms[fft_dir]) if (ft.node != hila::myrank()) {
            rr[i++] = ft.receive_request;
        }
        MPI_Waitall(n, rr, stat);
    }

    fft_MPI_timer.stop();
}

// inverse of start_gather
void fft_post_scatter( void *buffer ) {

    fft_MPI_timer.start();    
    for ( auto & fn : fft_comms[fft_dir] ) if (fn.node != hila::myrank() ) {
        char * p = (char *)buffer + fn.column_offset * (cmplx_size * elements);
        int n = fn.column_number * (cmplx_size * elements);

        MPI_Irecv( p, n, MPI_BYTE, fn.node, WRK_SCATTER_TAG, lattice->mpi_comm_lat, &fn.receive_request );
    }
    fft_MPI_timer.stop();
}

// inverse of post_gather
void fft_start_scatter( ) {

    fft_MPI_timer.start();

    for ( auto & fn : fft_comms[fft_dir] ) if (fn.node != hila::myrank() ) {

        MPI_Isend( fn.work_out, fn.recv_buf_size * cmplx_size * elements,
                   MPI_BYTE, fn.node, WRK_SCATTER_TAG, lattice->mpi_comm_lat, &fn.send_request );
    }

    fft_MPI_timer.stop();
}

// free the work buffers
void fft_cleanup() {
    for ( auto & fn : fft_comms[fft_dir] ) {
        if (fn.node != hila::myrank() )  free( fn.work_in );
    }

    if (!is_float_fft) {
        fftw_destroy_plan(fftwplan_d);
        fftw_free(fftwbuf_d);
    } else {
        fftwf_destroy_plan(fftwplan_f);
        fftwf_free(fftwbuf_f);
    }    

}
