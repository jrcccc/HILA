#ifndef _BACKEND_LATTICE_H_
#define _BACKEND_LATTICE_H_




/// Lattice related data that needs to be communicated
/// to kernels
struct backend_lattice_struct {
  /// Storage for the neighbour indexes. Stored on device
  unsigned * d_neighb[NDIRS];
  /// Neighbour indexes with special boundaries. Stored on device
  unsigned * d_neighb_special[NDIRS];
  /// The full number of elements in a field, including haloes.
  /// This is necessary for structure-of-arrays -storage
  unsigned field_alloc_size;
  /// beginning and end of this loop (using lattice to communicate,
  /// which may not be the clearest choice.)
  int loop_begin, loop_end;
  /// Finally a pointer to the list of coordinates, stored on device
  coordinate_vector * d_coordinates;
  /// And store also the lattice size on device
  coordinate_vector d_size;

  /// setup the backend lattice data
  void setup(lattice_struct *lattice);

  #ifdef __CUDACC__
  /// get the coordinates at a given site
  __host__ __device__
  coordinate_vector coordinates( unsigned idx ){
    return d_coordinates[idx];
  }
  __host__ __device__
  int coordinate( unsigned idx, direction dir ){
    return d_coordinates[idx][dir];
  }
  __device__ coordinate_vector size(){
    return d_size;
  }
  __device__ int size(direction dir){
    return d_size[dir];
  }
  __device__ int64_t volume(){
    int64_t v = 1;
    foralldir(d) v *= d_size[d];
    return v;

  }




  #endif
};


#endif