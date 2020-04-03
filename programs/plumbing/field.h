#ifndef FIELD_H
#define FIELD_H
#include <iostream>
#include <string>
#include <cstring> //Memcpy is here...
#include <math.h>
#include <type_traits>

#include "../plumbing/globals.h"
#include "../plumbing/defs.h"
#include "../plumbing/field_storage.h"
#include "../plumbing/lattice.h"

#ifdef USE_MPI
#include "../plumbing/comm_mpi.h"
#endif

static int next_mpi_field_tag = 0;


// This is a marker for transformer -- for does not survive as it is
#define onsites(p) for(parity parity_type_var_(p);;)

#if 0
// field_element class: virtual class, no storage allocated,
// wiped out by the transformer
template <typename T>
class field_element  {
 private:
  T v;   // TODO: this must be set appropriately?
  
 public:
  // the following are just placemarkers, and are substituted by the transformer

  // implicit conversion to type T: this works for storage types where field is an std
  // array of type T - this is again for type propagation
  // TODO: IS THIS NEEDED?  WE WANT TO AVOID CONVERSIONS FROM field<T> v -> T
  // operator T() { return v; }
      
  // The type is important for ensuring correctness
  // Possibility: write these so that they work without the transformer
  template <typename A,
            std::enable_if_t<std::is_assignable<T&,A>::value, int> = 0 >
  field_element<T>& operator= (const A &d) {
    v = d; 
    return *this;
  }
  
  // field_element = field_element
  field_element<T>& operator=  (const field_element<T>& rhs) {
    v  = rhs.v; return *this;}
  field_element<T>& operator+= (const field_element<T>& rhs) {
    v += rhs.v; return *this;}
  field_element<T>& operator-= (const field_element<T>& rhs) {
    v -= rhs.v; return *this;}
  field_element<T>& operator*= (const field_element<T>& rhs) {
    v *= rhs.v; return *this;}
  field_element<T>& operator/= (const field_element<T>& rhs) {
    v /= rhs.v; return *this;}
  field_element<T>& operator+= (const double rhs) {
    v += rhs; return *this;}
  field_element<T>& operator-= (const double rhs) {
    v -= rhs; return *this;}
  field_element<T>& operator*= (const double rhs) {
    v *= rhs; return *this;}
  field_element<T>& operator/= (const double rhs) {
    v /= rhs; return *this;}


  // access the raw value - TODO:short vectors 
  T get_value() { return v; }

  T reduce_plus() {
    return v;   // TODO: short vector!
  }

  T reduce_mult() {
    return v;   // TODO: short vector!
  }
  
};

// declarations, implemented by transformer -- not necessarily defined anywhere
// +
template <typename T>
field_element<T> operator+( const field_element<T> &lhs, const field_element<T> &rhs);

template <typename T,typename L>
field_element<T> operator+( const L &lhs, const field_element<T> &rhs);

template <typename T,typename R>
field_element<T> operator+( const field_element<T> &lhs, const R &rhs);

// -
template <typename T>
field_element<T> operator-( const field_element<T> &lhs, const field_element<T> &rhs);

template <typename T,typename L>
field_element<T> operator-( const L &lhs, const field_element<T> &rhs);

template <typename T,typename R>
field_element<T> operator-( const field_element<T> &lhs,  const R &rhs);

template <typename T>
field_element<T> operator*( const field_element<T> &lhs, const field_element<T> &rhs);

template <typename T,typename L>
field_element<T> operator*( const L &lhs, const field_element<T> &rhs);

template <typename T,typename R>
field_element<T> operator*( const field_element<T> &lhs,  const R &rhs);

template <typename T>
field_element<T> operator/( const field_element<T> &lhs, const field_element<T> &rhs);

template <typename T,typename L>
field_element<T> operator/( const L &lhs, const field_element<T> &rhs);

template <typename T,typename R>
field_element<T> operator/( const field_element<T> &lhs,  const R &rhs);

// a function
template <typename T>
field_element<T> exp( field_element<T> &arg) {
  field_element<T> res;
  res = exp(arg.get_value());
  return res;
}


// TRY NOW AUTOMATIC REDUCTION IDENTIFICATION
// Overload operator  res += expr, where
// res is type T and expr is field_element<T>
// Make these void, because these cannot be assigned from
// These will be modified by transformer

template <typename T>
void operator += (T& lhs, field_element<T>& rhs) {
  lhs += rhs.reduce_plus();
}

template <typename T>
void operator *= (T& lhs, field_element<T>& rhs) {
  lhs *= rhs.reduce_mult();
}

#endif


template <typename T>
using element = T;

// These are helpers, to make generic templates
// e.g. t_plus<A,B> gives the type of the operator a + b, where a is of type A and b B.
template<typename A, typename B>
using t_plus = decltype(std::declval<A>() + std::declval<B>());
template<typename A, typename B>
using t_minus= decltype(std::declval<A>() - std::declval<B>());
template<typename A, typename B>
using t_mul  = decltype(std::declval<A>() * std::declval<B>());
template<typename A, typename B>
using t_div  = decltype(std::declval<A>() / std::declval<B>());

// field class 
template <typename T>
class field {
 private:

  /// The following struct holds the data + information about the field
  /// TODO: field-specific boundary conditions?
  class field_struct {
    public:
      field_storage<T> payload; // TODO: must be maximally aligned, modifiers - never null
      lattice_struct * lattice;
      unsigned is_fetched[NDIRS];     // is communication done
      unsigned move_started[NDIRS];   // is communication going on
      unsigned assigned_to;           // keeps track of first assignment to parities
#ifdef USE_MPI
      std::vector<MPI_Request> receive_request[3*NDIRS];
      std::vector<MPI_Request> send_request[3*NDIRS];
      std::vector<char *> receive_buffer[3*NDIRS];
      std::vector<char *> send_buffer[3*NDIRS];
      int mpi_tag;
      void initialize_communication(){
        for(int d=0; d<NDIRS; d++) for(parity par: {EVEN,ODD}) {
          int tag = d + NDIRS*(int)par;
          lattice_struct::comminfo_struct ci = lattice->get_comminfo(d);
          receive_buffer[tag].resize(ci.from_node.size());
          send_buffer[tag].resize(ci.to_node.size());
          receive_request[tag].resize(ci.from_node.size());
          send_request[tag].resize(ci.to_node.size());
        }
        mpi_tag = next_mpi_field_tag;
        next_mpi_field_tag++;
      }
#else
      void initialize_communication(){};
#endif

      void allocate_payload() { 
        payload.allocate_field(lattice);
        initialize_communication();
      }
      void free_payload() { payload.free_field(); }

      /// Getter for an individual elements. Will not work in CUDA host code,
      /// but must be defined
      inline auto get(const int i) const {
        return payload.get( i, lattice->field_alloc_size() );
      }

      template<typename A>
      inline void set(const A & value, const int i) {
        payload.set( value, i, lattice->field_alloc_size() );
      }

      /// Gather boundary elements for communication
      void gather_comm_elements(char * buffer, lattice_struct::comm_node_struct to_node, parity par) const {
        payload.gather_comm_elements(buffer, to_node, par, lattice);
      };

      /// Place boundary elements from neighbour
      void place_comm_elements(char * buffer, lattice_struct::comm_node_struct from_node, parity par){
        payload.place_comm_elements(buffer, from_node, par, lattice);
      };
      
      /// Place boundary elements from local lattice (used in vectorized version)
      void set_local_boundary_elements(direction dir, parity par){
        payload.set_local_boundary_elements(dir, par, lattice);
      };

  };

  static_assert( std::is_pod<T>::value, "Field expects only pod-type elements (plain data): default constructor, copy and delete");
  
 public:

  field_struct * fs;
  
  field<T>() {
    // std::cout << "In constructor 1\n";
    fs = nullptr;             // lazy allocation on 1st use
  }
  
  // TODO: for some reason this straightforward copy constructor seems to be necessary, the
  // one below it does not catch implicit copying.  Try to understand why
  field<T>(const field<T>& other) {
    fs = nullptr;  // this is probably unnecessary
    (*this)[ALL] = other[X];
  }
    
  // copy constructor - from fields which can be assigned
  template <typename A,
            std::enable_if_t<std::is_convertible<A,T>::value, int> = 0 >  
  field<T>(const field<A>& other) {
    fs = nullptr;  // this is probably unnecessary
    (*this)[ALL] = other[X];
  }


  template <typename A,
            std::enable_if_t<std::is_convertible<A,T>::value, int> = 0 >  
  field<T>(const A& val) {
    fs = nullptr;
    // static_assert(!std::is_same<A,int>::value, "in int constructor");
    (*this)[ALL] = val;
  }
  
  // move constructor - steal the content
  field<T>(field<T>&& rhs) {
    // std::cout << "in move constructor\n";
    fs = rhs.fs;
    rhs.fs = nullptr;
  }

  ~field<T>() {
    free();
  }
    
  void allocate() {
    assert(fs == nullptr);
    if (lattice == nullptr) {
      // TODO: write to some named stream
      std::cout << "Can not allocate field variables before lattice.setup()\n";
      exit(1);  // TODO - more ordered exit?
    }
    fs = new field_struct;
    fs->lattice = lattice;
    fs->allocate_payload();
    mark_changed(ALL);      // guarantees communications will be done
    fs->assigned_to = 0;    // and this means that it is not assigned
  }

  void free() {
    if (fs != nullptr) {
      fs->free_payload();
      delete fs;
      fs = nullptr;
    }
  }

  bool is_allocated() const { return (fs != nullptr); }

  bool is_initialized(parity p) const { 
    return fs != nullptr && ((fs->assigned_to & parity_bits(p)) != 0);
  }
  
  /// call this BEFORE the var is written to
  void mark_changed(const parity p) {
    if (fs == nullptr) allocate();
    else {
      // turn off bits corresponding to parity p
      assert( parity_bits(p) );
      for (int i=0; i<NDIRS; i++) fs->is_fetched[i]   &= parity_bits_inverse(p);
      for (int i=0; i<NDIRS; i++) fs->move_started[i] &= parity_bits_inverse(p);
    }
    fs->assigned_to |= parity_bits(p);
  }

  // Is const version of mark_changed needed?  Sounds strange
  void mark_changed(const parity p) const {
    assert(is_allocated());
    assert( parity_bits(p) );
    for (int i=0; i<NDIRS; i++) fs->is_fetched[i]   &= parity_bits_inverse(p);
    for (int i=0; i<NDIRS; i++) fs->move_started[i] &= parity_bits_inverse(p);
    fs->assigned_to |= parity_bits(p);
  }

  /// Mark the field parity fetched from direction
  void mark_fetched( int dir, const parity p) const {
    assert( parity_bits(p) );
    fs->is_fetched[dir] |= parity_bits(p);
  }

  /// Check if the field has been changed since the previous communication
  bool is_fetched( int dir, parity par) const {
    assert(dir < NDIRS);
    unsigned p = parity_bits(par);
    // true if all par-bits are on 
    return (fs->is_fetched[dir] & p) == p ;
  }

  /* Mark communication started */
  void mark_move_started( int dir, parity p) const{
    assert(dir < NDIRS);
    fs->move_started[dir] |= parity_bits(p);
  }

  /// Check if communication has started
  bool is_move_started( int dir, parity par) const{
    assert(dir < NDIRS);
    unsigned p = parity_bits(par);
    return (fs->move_started[dir] & p) == p ;
  }

  
  // Overloading [] 
  // placemarker, should not be here
  // T& operator[] (const int i) { return data[i]; }

  // declarations -- WILL BE implemented by transformer, not written here
  element<T>& operator[] (const parity p) const;
  element<T>& operator[] (const parity_plus_direction p) const;
  element<T>& operator[] (const parity_plus_offset p) const;

  #if defined(VANILLA)
  // TEMPORARY HACK: return ptr to bare array
  inline auto field_buffer() const { return this->fs->payload.get_buffer(); }
  #endif

  /// Get an individual element outside a loop. This is also used as a getter in the vanilla code.
  inline auto get_value_at(int i) const { return this->fs->get(i); }

  /// Set an individual element outside a loop. This is also used as a setter in the vanilla code.
  template<typename A>
  inline void set_value_at(const A & value, int i) { this->fs->set( value, i); }

  // fetch the element at this loc
  // T get(int i) const;
  
  // NOTE: THIS SHOULD BE INCLUDED IN TEMPLATE BELOW; SEEMS NOT???  
  field<T>& operator= (const field<T>& rhs) {
   (*this)[ALL] = rhs[X];
   return *this;
  }

  // Overloading = - possible only if T = A is OK
  template <typename A, 
            std::enable_if_t<std::is_assignable<T&,A>::value, int> = 0 >
  field<T>& operator= (const field<A>& rhs) {
    (*this)[ALL] = rhs[X];
    return *this;
  }

  // same but without the field
  template <typename A, 
            std::enable_if_t<std::is_assignable<T&,A>::value, int> = 0 >
  field<T>& operator= (const A& d) {
    (*this)[ALL] = d;
    return *this;
  }
  
  // Do also move assignment
  field<T>& operator= (field<T> && rhs) {
    if (this != &rhs) {
      free();
      fs = rhs.fs;
      rhs.fs = nullptr;
    }
    return *this;
  }
  
  // is OK if T+A can be converted to type T
  template <typename A,
            std::enable_if_t<std::is_convertible<t_plus<T,A>,T>::value, int> = 0>
  field<T>& operator+= (const field<A>& rhs) { 
    (*this)[ALL] += rhs[X]; return *this;
  }
  
  template <typename A,
            std::enable_if_t<std::is_convertible<t_minus<T,A>,T>::value, int> = 0>  
  field<T>& operator-= (const field<A>& rhs) { 
    (*this)[ALL] -= rhs[X];
    return *this;
  }
  
  template <typename A,
            std::enable_if_t<std::is_convertible<t_mul<T,A>,T>::value, int> = 0>
  field<T>& operator*= (const field<A>& rhs) {
    (*this)[ALL] *= rhs[X]; 
    return *this;
  }

  template <typename A,
            std::enable_if_t<std::is_convertible<t_div<T,A>,T>::value, int> = 0>
  field<T>& operator/= (const field<A>& rhs) {
    (*this)[ALL] /= rhs[X];
    return *this;
  }

  template <typename A,
            std::enable_if_t<std::is_convertible<t_plus<T,A>,T>::value, int> = 0>
  field<T>& operator+= (const A & rhs) { (*this)[ALL] += rhs; return *this;}

  template <typename A,
            std::enable_if_t<std::is_convertible<t_minus<T,A>,T>::value, int> = 0>  
  field<T>& operator-= (const A & rhs) { (*this)[ALL] -= rhs; return *this;}

  template <typename A,
            std::enable_if_t<std::is_convertible<t_mul<T,A>,T>::value, int> = 0>
  field<T>& operator*= (const A & rhs) { (*this)[ALL] *= rhs; return *this;}
  
  template <typename A,
            std::enable_if_t<std::is_convertible<t_div<T,A>,T>::value, int> = 0>
  field<T>& operator/= (const A & rhs) { (*this)[ALL] /= rhs; return *this;}


  // Communication routines
  void start_move(direction d, parity p) const;
  void start_move(direction d) const { start_move(d, ALL);}
  void wait_move(direction d, parity p) const;

  // Declaration of shift methods
  field<T> shift(const coordinate_vector &v, parity par) const;
  field<T> shift(const coordinate_vector &v) const { return shift(v,ALL); }

  // General getters and setters
  void set_elements( T * elements, std::vector<unsigned> index_list) const;
  void set_elements( T * elements, std::vector<coordinate_vector> coord_list) const;

  // Fourier transform declarations
  void FFT();
};


// these operators rely on SFINAE, OK if field_t_plus<A,B> exists i.e. A+B is OK
/// operator +
template <typename A, typename B>
auto operator+( field<A> &lhs, field<B> &rhs) -> field<t_plus<A,B>>
{
  field <t_plus<A,B>> tmp;
  tmp[ALL] = lhs[X] + rhs[X];
  return tmp;
}

template <typename A, typename B>
auto operator+( const A &lhs, const field<B> &rhs) -> field<t_plus<A,B>>
{
  field<t_plus<A,B>> tmp;
  tmp[ALL] = lhs + rhs[X];
  return tmp;
}

template <typename A, typename B>
auto operator+( const field<A> &lhs, const B &rhs) -> field<t_plus<A,B>>
{
  field<t_plus<A,B>> tmp;
  tmp[ALL] = lhs[X] + rhs;
  return tmp;
}

/// operator -
template <typename A, typename B>
auto operator-( const field<A> &lhs, const field<B> &rhs) -> field<t_minus<A,B>>
{
  field <t_minus<A,B>> tmp;
  tmp[ALL] = lhs[X] - rhs[X];
  return tmp;
}

template <typename A, typename B>
auto operator-( const A &lhs, const field<B> &rhs) -> field<t_minus<A,B>>
{
  field<t_minus<A,B>> tmp;
  tmp[ALL] = lhs - rhs[X];
  return tmp;
}

template <typename A, typename B>
auto operator-( const field<A> &lhs, const B &rhs) -> field<t_minus<A,B>>
{
  field<t_minus<A,B>> tmp;
  tmp[ALL] = lhs[X] - rhs;
  return tmp;
}


/// operator *
template <typename A, typename B>
auto operator*( const field<A> &lhs, const field<B> &rhs) -> field<t_mul<A,B>>
{
  field <t_mul<A,B>> tmp;
  tmp[ALL] = lhs[X] * rhs[X];
  return tmp;
}

template <typename A, typename B>
auto operator*( const A &lhs, const field<B> &rhs) -> field<t_mul<A,B>>
{
  field<t_mul<A,B>> tmp;
  tmp[ALL] = lhs * rhs[X];
  return tmp;
}

template <typename A, typename B>
auto operator*( const field<A> &lhs, const B &rhs) -> field<t_mul<A,B>>
{
  field<t_mul<A,B>> tmp;
  tmp[ALL] = lhs[X] * rhs;
  return tmp;
}

/// operator /
template <typename A, typename B>
auto operator/( const field<A> &lhs, const field<B> &rhs) -> field<t_div<A,B>>
{
  field <t_div<A,B>> tmp;
  tmp[ALL] = lhs[X] / rhs[X];
  return tmp;
}

template <typename A, typename B>
auto operator/( const A &lhs, const field<B> &rhs) -> field<t_div<A,B>>
{
  field<t_div<A,B>> tmp;
  tmp[ALL] = lhs / rhs[X];
  return tmp;
}

template <typename A, typename B>
auto operator/( const field<A> &lhs, const B &rhs) -> field<t_div<A,B>>
{
  field<t_div<A,B>> tmp;
  tmp[ALL] = lhs[X] / rhs;
  return tmp;
}


#define NAIVE_SHIFT
#if defined(NAIVE_SHIFT)

// Define shift method here too - this is a placeholder, very inefficient
// works by repeatedly nn-copying the field

template<typename T>
field<T> field<T>::shift(const coordinate_vector &v, const parity par) const {
  field<T> r1, r2;
  r2 = *this;
  foralldir(d) {
    if (abs(v[d]) > 0) {
      direction dir;
      if (v[d] > 0) dir = d; else dir = -d;
    
      for (int i=0; i<abs(v[d]); i++) {
        r1[ALL] = r2[X+dir];
        r2 = r1;
      }
    }
  }
  return r2;
}

#elif !defined(USE_MPI)

template<typename T>
field<T> field<T>::shift(const coordinate_vector &v, const parity par) const {
  field<T> result;

  onsites(par) {
    if 
  }
  r2 = *this;
  foralldir(d) {
    if (abs(v[d]) > 0) {
      direction dir;
      if (v[d] > 0) dir = d; else dir = -d;
    
      for (int i=0; i<abs(v[d]); i++) {
        r1[ALL] = r2[X+dir];
        r2 = r1;
      }
    }
  }
  return r2;
}

#endif



/// Functions for manipulating lists of elements
template<typename T>
void field<T>::set_elements( T * elements, std::vector<unsigned> index_list) const {
  fs->payload.gather_elements(elements, index_list, fs->lattice);
}

template<typename T>
void field<T>::set_elements( T * elements, std::vector<coordinate_vector> coord_list) const {
  std::vector<unsigned> index_list(coord_list.size());
  for (int j=0; j<coord_list.size(); j++) {
    index_list[j] = fs->lattice->site_index(coord_list[j]);
  }
  fs->payload.gather_elements(elements, index_list, fs->lattice);
}



#if defined(USE_MPI) && !defined(TRANSFORMER) 
/* MPI implementations
 * For simplicity, these functions do not use field expressions and
 * can be ignored by the transformer. Since the transformer does not
 * have access to mpi.h, it cannot process this branch.
 */

/// wait_move(): Communicate the field at parity par from direction
///  d. Uses accessors to prevent dependency on the layout.
template<typename T>
void field<T>::start_move(direction d, parity p) const {

  for( parity par: loop_parities(p) ) {
    if( is_move_started(d, par) ){
      // Not changed, return directly
      // Keep count of gathers optimized away
      lattice->n_gather_avoided += 1;
      return;
    }

    // Communication hasn't been started yet, do it now
    int index = static_cast<int>(d) + NDIRS*static_cast<int>(par);
    int tag =  fs->mpi_tag*3*NDIRS + index;
    constexpr int size = sizeof(T);

    lattice_struct::comminfo_struct ci = lattice->comminfo[d];
    int n = 0;
    std::vector<MPI_Request> & receive_request = fs->receive_request[index];
    std::vector<MPI_Request> & send_request = fs->send_request[index];
    std::vector<char *> & receive_buffer = fs->receive_buffer[index];
    std::vector<char *> & send_buffer = fs->send_buffer[index];

    /* HANDLE RECEIVES: loop over nodes which will send here */
    for( lattice_struct::comm_node_struct from_node : ci.from_node ){
      unsigned sites = from_node.n_sites(par);
      if(receive_buffer[n] == NULL)
        receive_buffer[n] = (char *)malloc( sites*size );

      //printf("node %d, recv tag %d from %d\n", mynode(), tag, from_node.rank);

      MPI_Irecv( receive_buffer[n], sites*size, MPI_BYTE, from_node.rank, 
	             tag, lattice->mpi_comm_lat, &receive_request[n] );
      n++;
    }

    /* HANDLE SENDS: Copy field elements on the boundary to a send buffer and send */
    n=0;
    for( lattice_struct::comm_node_struct to_node : ci.to_node ){
       /* gather data into the buffer  */
       unsigned sites = to_node.n_sites(par);
       if(send_buffer[n] == NULL)
         send_buffer[n] = (char *)malloc( sites*size );

       fs->gather_comm_elements(send_buffer[n], to_node, par);
 
       //printf("node %d, send tag %d to %d\n", mynode(), tag, to_node.rank);

       /* And send */
       MPI_Isend( send_buffer[n], sites*size, MPI_BYTE, to_node.rank, 
               tag, lattice->mpi_comm_lat, &send_request[n]);
       //printf("node %d, sent tag %d\n", mynode(), tag);
       n++;
     }

    mark_move_started(d, par);
  }
}

///* wait_move(): Wait for communication at parity par from
///  direction d completes the communication in the function.
///  If the communication has not started yet, also calls
///  start_move()
///
///  NOTE: This will be called even if the field is marked const.
///  Therefore this function is const, even though it does change
///  the internal content of the field, the halo. From the point
///  of view of the user, the value of the field does not change.
template<typename T>
void field<T>::wait_move(direction d, parity p) const {

  // Loop over parities
  // (if p=ALL, do both EVEN and ODD otherwise just p);
  for( parity par: loop_parities(p) ) {
    int index = static_cast<int>(d) + NDIRS*static_cast<int>(par);
    int tag =  fs->mpi_tag*3*NDIRS + index;

    if( is_fetched(d, par) ){
      // Not changed, return directly
      // Keep count of gathers optimized away
      lattice->n_gather_avoided += 1;
      return;
    }

    //printf("wait_move tag %d node %d\n",tag,mynode());

    // This will start the communication if it has not been started yet
    start_move(d, par);

    // Update local elements in the halo (necessary for vectorized version)
    fs->set_local_boundary_elements(d, par);

    lattice_struct::comminfo_struct ci = lattice->comminfo[d];
    std::vector<MPI_Request> & receive_request = fs->receive_request[index];
    std::vector<char *> & receive_buffer = fs->receive_buffer[index];

    /* Wait for the data here */
    int n = 0;
    for( lattice_struct::comm_node_struct from_node : ci.from_node ){
      MPI_Status status;
      //printf("node %d, waiting for recv tag %d\n", mynode(), tag);
      MPI_Wait(&receive_request[n], &status);
      //printf("node %d, received tag %d\n", mynode(), tag);

      fs->place_comm_elements(receive_buffer[n], from_node, par);
      n++;
    }

    /* Mark the parity fetched from direction dir */
    mark_fetched(d, par);

    /* Keep count of communications */
    lattice->n_gather_done += 1;
  }
}



#include "../datatypes/cmplx.h"
#include "fftw3.h"


/// Run Fast Fourier Transform on the field to each direction
// This is done by collecting a column of elements to each node,
// running the Fourier transform on the column and redistributing
// the result
template<>
inline void field<cmplx<double>>::FFT(){

  foralldir(dir){

    size_t local_sites = lattice->local_size(dir);
    size_t sites = lattice->size(dir);
    int nnodes = sites / local_sites;
    std::vector<node_info> allnodes = lattice->nodelist();
    int myrank = lattice->node_rank();
    int my_column_rank = -1;
    MPI_Comm column_communicator;

    printf(" node %d has %ld sites out of %ld in direction %d\n",myrank,local_sites, sites, (int)dir);
    
    // Build a list of nodes in this column
    // All nodes should have these in the same order
    std::vector<int> nodelist;
    coordinate_vector min = allnodes[myrank].min;
    coordinate_vector size = allnodes[myrank].size;
    foralldir(d2){
      assert( min[d2] == lattice->min_coordinate()[d2] );
    }
    for( int rank=0; rank < allnodes.size(); rank++ ) {
      node_info node = allnodes[rank];
      bool in_column = true;
      foralldir(d2) if( d2 != dir && node.min[d2] != min[d2] ){
        in_column = false;
      }
      if( in_column ){
        if(rank==myrank)
          my_column_rank = nodelist.size();
        nodelist.push_back(rank);
        printf(" node %d: %d in my column\n", myrank, rank);
      }
    }

    // Create a communicator for the column
    MPI_Comm_split( MPI_COMM_WORLD, nodelist[0], my_column_rank, &column_communicator );

    assert( nnodes == nodelist.size() );

    // Count columns on this rank
    int cols = 1;
    foralldir(d2) if(d2!=dir) cols *= lattice->local_size(d2);
    printf(" node %d: %d columns\n", myrank, cols);

    // Buffers for sending and receiving a column
    std::vector<cmplx<double>> column(sites), send_buffer(sites);

    // Do transform in all columns
    int c=0;
    while( c < cols ) {
      coordinate_vector thiscol=min;
      int cc = c;
      foralldir(d2) if(d2!=dir) {
        thiscol[d2] += cc%size[d2];
        cc/=size[d2];
      }

      // Build a list of sites matching this column
      coordinate_vector site = thiscol;
      std::vector<unsigned> sitelist(local_sites);
      for(int i=0; i<local_sites; i++ ){
        site[dir] = min[dir] + i;
        sitelist[i] = lattice->site_index(site);
      }

      // Collect the data on this node
      char * sendbuf = (char*) send_buffer.data()+(c%nodelist.size())*local_sites;
      fs->payload.gather_elements(sendbuf, sitelist, lattice);

      // Send the data from each node to rank c in the column
      MPI_Gather( sendbuf, local_sites*sizeof(cmplx<double>), MPI_BYTE, 
                  column.data(), local_sites*sizeof(cmplx<double>), MPI_BYTE,
                  c%nodelist.size(), column_communicator);

      if(my_column_rank == c%nodelist.size()){
        printf("rank %d, col rank %d, data (",myrank,my_column_rank);
        fftw_complex *in, *out;
        in = (fftw_complex*) fftw_malloc(sizeof(fftw_complex) * sites);
        out = (fftw_complex*) fftw_malloc(sizeof(fftw_complex) * sites);
        for(int t=0;t<sites; t++){
          printf(" %g, ", column[t].re );
          in[t][0] = column[t].re;
          in[t][1] = column[t].im;
        }
        printf(")\n");

        fftw_plan plan = fftw_plan_dft_1d( sites, in, out, FFTW_FORWARD, FFTW_ESTIMATE);
        fftw_execute(plan);

        printf("rank %d, col rank %d, out (",myrank,my_column_rank);
        for(int t=0;t<sites; t++){
          printf(" (%g, %g) ", out[t][0], out[t][1] );
          column[t].re = out[t][0];
          column[t].im = out[t][1];
        }
        printf(")\n");

        fftw_destroy_plan(plan);
        fftw_free(in); fftw_free(out);
      }


      MPI_Scatter( column.data(), local_sites*sizeof(cmplx<double>), MPI_BYTE, 
                  sendbuf, local_sites*sizeof(cmplx<double>), MPI_BYTE,
                  c%nodelist.size(), column_communicator);
      fs->payload.place_elements(sendbuf, sitelist, lattice);
      c++;
    }

  }
}



#else

///* Trivial implementation when no MPI is used
#include "../plumbing/comm_vanilla.h"
template<typename T>
void field<T>::start_move(direction d, parity p) const {}
template<typename T>
void field<T>::wait_move(direction d, parity p) const {
  // Update local elements in the halo (necessary for vectorized version)
  // Does not need to happen every time; should use tracking like in MPI
  fs->set_local_boundary_elements(d, p);
}



#endif


#endif // FIELD_H


