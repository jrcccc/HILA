/** @file field.h */
#ifndef FIELD_H
#define FIELD_H

#include <sstream>
#include <iostream>
#include <string>
#include <cstring> //Memcpy is here...
#include <math.h>
#include <type_traits>

#include "plumbing/defs.h"
#include "plumbing/coordinates.h"
#include "plumbing/lattice.h"
#include "plumbing/field_storage.h"

#include "plumbing/backend_vector/vector_types.h"

#include "plumbing/com_mpi.h"


// This is a marker for hilapp -- will be removed by it
#define onsites(p) for (Parity par_dummy__(p); par_dummy__ == EVEN; par_dummy__ = ODD)

template <typename T>
class Field;

template <typename T>
void ensure_field_operators_exist();

#include "plumbing/ensure_loop_functions.h"

/**
 * @class Field
 * @brief The field class implements the standard methods for accessing fields
 *  Hilapp replaces the parity access patterns, Field[par] with a loop over
 *  the appropriate sites.
 *
 * @details The Field class also contains member functions used by hilapp, as well
 *  as members that may be useful for application developers.
 *
 *  The Field mainly implements the interface to the Field and not the
 *  content.
 *
 *  The Field contains a pointer to Field::field_struct, which implements
 *  MPI communication of the Field boundaries.
 *
 *  The Field::field_struct points to a field_storage, which is defined
 *  by each backend. It implements storing and accessing the Field data,
 *  including buffers for storing haloes returned from MPI communication.
 *
 * @tparam T
 */
template <typename T>
class Field {

  public:
    enum class gather_status_t : unsigned { NOT_DONE, STARTED, DONE };

  private:
    /**
     * @class field_struct
     * @brief Stores Field class data and communication parameters for said data
     *
     * @todo field-specific boundary conditions
     */
    class field_struct {
      public:
        field_storage<T> payload; // TODO: must be maximally aligned, modifiers - never null
        int lattice_id;
#ifdef VECTORIZED
        // get a direct ptr from here too, ease access
        vectorized_lattice_struct<hila::vector_info<T>::vector_size> *vector_lattice;
#endif
        unsigned assigned_to;                        // keeps track of first assignment to parities
        gather_status_t gather_status_arr[3][NDIRS]; // is communication done

        // neighbour pointers - because of boundary conditions, can be different for
        // diff. fields
        const unsigned *RESTRICT neighbours[NDIRS];
        hila::bc boundary_condition[NDIRS];

        MPI_Request receive_request[3][NDIRS];
        MPI_Request send_request[3][NDIRS];
#ifndef VANILLA
        // vanilla needs no special receive buffers
        T *receive_buffer[NDIRS];
#endif
        T *send_buffer[NDIRS];

        void initialize_communication() {
            for (int d = 0; d < NDIRS; d++) {
                for (int p = 0; p < 3; p++)
                    gather_status_arr[p][d] = gather_status_t::NOT_DONE;
                send_buffer[d] = nullptr;
#ifndef VANILLA
                receive_buffer[d] = nullptr;
#endif
            }
        }

        void free_communication() {
            for (int d = 0; d < NDIRS; d++) {
                if (send_buffer[d] != nullptr)
                    payload.free_mpi_buffer(send_buffer[d]);
#ifndef VANILLA
                if (receive_buffer[d] != nullptr)
                    payload.free_mpi_buffer(receive_buffer[d]);
#endif
            }
        }

        void allocate_payload() {
            payload.allocate_field(lattice);
        }
        void free_payload() {
            payload.free_field();
        }

#ifndef VECTORIZED
        /// Getter for an individual elements in a loop
        inline auto get(const unsigned i) const {
            return payload.get(i, lattice.field_alloc_size());
        }

        template <typename A>
        inline void set(const A &value, const unsigned i) {
            payload.set(value, i, lattice.field_alloc_size());
        }

        /// Getter for an element outside a loop. Used to manipulate the field directly
        /// outside loops.
        inline auto get_element(const unsigned i) const {
            return payload.get_element(i, lattice);
        }

        template <typename A>
        inline void set_element(const A &value, const unsigned i) {
            payload.set_element(value, i, lattice);
        }
#else
        template <typename vecT>
        inline vecT get_vector(const unsigned i) const {
            return payload.template get_vector<vecT>(i);
        }
        inline T get_element(const unsigned i) const {
            return payload.get_element(i);
        }

        template <typename vecT>
        inline void set_vector(const vecT &val, const unsigned i) {
            return payload.set_vector(val, i);
        }
        inline void set_element(const T &val, const unsigned i) {
            return payload.set_element(val, i);
        }
#endif

        /// Gather boundary elements for communication
        void gather_comm_elements(Direction d, Parity par, T *RESTRICT buffer,
                                  const lattice_struct::comm_node_struct &to_node) const;

        /// Place boundary elements from neighbour
        void place_comm_elements(Direction d, Parity par, T *RESTRICT buffer,
                                 const lattice_struct::comm_node_struct &from_node);

        /// Place boundary elements from local lattice (used in vectorized version)
        void set_local_boundary_elements(Direction dir, Parity par);

        /// Gather a list of elements to a single node
        void gather_elements(T *buffer, const std::vector<CoordinateVector> &coord_list,
                             int root = 0) const;
        void scatter_elements(T *buffer, const std::vector<CoordinateVector> &coord_list,
                              int root = 0);


        /// get the receive buffer pointer for the communication.
        T *get_receive_buffer(Direction d, Parity par,
                              const lattice_struct::comm_node_struct &from_node);
    };

    // static_assert( std::is_pod<T>::value, "Field expects only pod-type elements
    // (plain data): default constructor, copy and delete");
    static_assert(std::is_trivial<T>::value && std::is_standard_layout<T>::value,
                  "Field expects only pod-type elements (plain data): default "
                  "constructor, copy and delete");

  public:
    ////////////////////////////////////////////////
    /// Field::fs keeps all of the field content
    ////////////////////////////////////////////////

    field_struct *RESTRICT fs;

    /**
     * @name Constructor
     @{
     */
    /**
     * @brief Default construct a new Field object. Assigns field_struct fs to nullptr
     *
     */
    Field() {

        // put here some implementation checks for field vars
#ifdef VECTORIZED
        static_assert(sizeof(hila::scalar_type<T>) == 4 || sizeof(hila::scalar_type<T>) == 8,
                      "In vectorized arch (e.g. AVX2), only 4 or 8 byte (32 or 64 bit) numbers for "
                      "Field<> implemented, sorry!");
#endif
#if defined(CUDA) || defined(HIP)
        static_assert(!std::is_same<hila::scalar_type<T>, long double>::value,
                      "Type 'long double' numbers in Field<> not supported by cuda/hip");
#endif

        fs = nullptr; // lazy allocation on 1st use
    }
    /**
     * @brief Copy constructor with already initialised Field
     *
     * @param other
     */
    Field(const Field &other) : Field() {
        assert(other.is_initialized(ALL) && "Initializer Field value not set");

        (*this)[ALL] = other[X];
    }
    /**
     * @brief Copy constructor form Field of type A to field of type F if the conversion is defined
     *
     * @tparam A
     * @param other
     */
    template <typename A, std::enable_if_t<std::is_convertible<A, T>::value, int> = 0>
    Field(const Field<A> &other) : Field() {
        assert(other.is_initialized(ALL) && "Initializer Field value not set");

        (*this)[ALL] = other[X];
    }
    /**
     * @brief Construct a new Field object with scalar (val) of type A to a field of type F type if
     * the conversion is defined
     *
     * @tparam A
     * @param val
     */
    template <typename A,
              std::enable_if_t<
                  hila::is_assignable<T &, A>::value || std::is_convertible<A, T>::value, int> = 0>
    Field(const A &val) : Field() {
        (*this)[ALL] = val;
    }
    /**
     * @brief Construct a new Field object with scalar 0 with nullpointer trick
     *
     * @param z
     */
    Field(const std::nullptr_t z) : Field() {
        (*this)[ALL] = 0;
    }

    /**
     * @brief Construct a new Field object by stealing content from previous field (rhs) which will
     * be set to null
     *
     * @param rhs
     */
    Field(Field &&rhs) {
        fs = rhs.fs;
        rhs.fs = nullptr;
    }
    /** @} */

    ~Field() {
        free();

#ifdef HILAPP
        // Because destructor is instantiated for all fields,
        // use this to put in a hook for generating call to this function
        // in preprocessing stage
        ensure_field_operators_exist<T>();
#endif
    }

    /**
     * @brief  Sets up memory for field content and communication.
     */
    void allocate() {
        assert(fs == nullptr);
        if (lattice.volume() == 0) {
            hila::out0 << "Can not allocate Field variables before lattice.setup()\n";
            hila::terminate(0);
        }
        fs = (field_struct *)memalloc(sizeof(field_struct));
        fs->lattice_id = lattice.id();
        fs->allocate_payload();
        fs->initialize_communication();
        mark_changed(ALL);   // guarantees communications will be done
        fs->assigned_to = 0; // and this means that it is not assigned

        for (Direction d = (Direction)0; d < NDIRS; ++d) {

#if !defined(CUDA) && !defined(HIP)
            fs->neighbours[d] = lattice.neighb[d];
#else
            fs->payload.neighbours[d] = lattice.backend_lattice->d_neighb[d];
#endif
        }

#ifdef SPECIAL_BOUNDARY_CONDITIONS
        foralldir(dir) {
            fs->boundary_condition[dir] = hila::bc::PERIODIC;
            fs->boundary_condition[-dir] = hila::bc::PERIODIC;
        }
#endif

#ifdef VECTORIZED
        if constexpr (hila::is_vectorizable_type<T>::value) {
            fs->vector_lattice = lattice.backend_lattice
                                     ->get_vectorized_lattice<hila::vector_info<T>::vector_size>();
        } else {
            fs->vector_lattice = nullptr;
        }
#endif
    }

    /**
     * @brief Destroys field data
     *
     *
     */
    void free() {
        // don't call destructors when exiting - either MPI or cuda can already
        // be off.
        if (fs != nullptr && !hila::about_to_finish) {
            for (Direction d = (Direction)0; d < NDIRS; ++d)
                drop_comms(d, ALL);
            fs->free_payload();
            fs->free_communication();
            std::free(fs);
            fs = nullptr;
        }
    }

    /**
     * @brief Returns true if the Field data has been allocated
     *   \code{.cpp}
     *    class Cpp {};
     *   \endcode
     * @return true
     * @return false
     */
    bool is_allocated() const {
        return (fs != nullptr);
    }

    /**
     * @brief Returns true if the Field has been written
     *
     * @param p Field parity
     * @return bool
     */
    bool is_initialized(Parity p) const {
        return fs != nullptr && ((fs->assigned_to & parity_bits(p)) != 0);
    }

    /**
     * @brief Returns current gather_status_t
     *
     * @param p Field partiy
     * @param d Direction
     * @return gather_status_t
     */
    gather_status_t gather_status(Parity p, int d) const {
        assert(parity_bits(p) && d >= 0 && d < NDIRS);
        return fs->gather_status_arr[(int)p - 1][d];
    }
    void set_gather_status(Parity p, int d, gather_status_t stat) const {
        assert(parity_bits(p) && d >= 0 && d < NDIRS);
        fs->gather_status_arr[(int)p - 1][d] = stat;
    }

    /**
     * @brief  Allocate Field if it is not already allocated
     * @details check that Field is allocated, and if not do it (if not const)
     * Must be called BEFORE the var is actually used
     * "hilapp" will generate these calls as needed!
     *
     */
    void check_alloc() {
        if (!is_allocated())
            allocate();
    }

    /**
     * @brief If Field is const assert that the Field is allocated
     */
    void check_alloc() const {
        assert(is_allocated());
    }


    /**
     * @brief Bookkeeping for field communication
     * @details If ALL changes, both parities invalid; if p != ALL, then p and ALL.
     * @param p Field parity
     */
    void mark_changed(const Parity p) const {

        for (Direction i = (Direction)0; i < NDIRS; ++i) {
            // check if there's ongoing comms, invalidate it!
            drop_comms(i, opp_parity(p));

            set_gather_status(opp_parity(p), i, gather_status_t::NOT_DONE);
            if (p != ALL) {
                set_gather_status(ALL, i, gather_status_t::NOT_DONE);
            } else {
                set_gather_status(EVEN, i, gather_status_t::NOT_DONE);
                set_gather_status(ODD, i, gather_status_t::NOT_DONE);
            }
        }
        fs->assigned_to |= parity_bits(p);
    }

    /**
     * @brief Mark the Field already gathered, no need to communicate
     * @details Mark the field parity gathered from Direction
     * In case p=ALL we could mark everything gathered, but we'll be conservative here
     * and mark only this parity, because there might be other parities on the fly and
     * corresponding waits should be done,  This should never happen in automatically
     * generated loops. In any case start_gather, is_gathered, get_gather_parity has
     * intelligence to figure out the right thing to do
     *
     * @param dir
     * @param p
     */
    void mark_gathered(int dir, const Parity p) const {
        set_gather_status(p, dir, gather_status_t::DONE);
    }

    /**
     * @brief Check if the field has been gathered since the previous communication
     * @details par = ALL:   ALL or (EVEN+ODD) are OK\n
     *          par != ALL:  ALL or par are OK
     * @hilapponly
     * @param dir
     * @param par
     * @return true
     * @return false
     */
    bool is_gathered(int dir, Parity par) const {
        if (par != ALL) {
            return gather_status(par, dir) == gather_status_t::DONE ||
                   gather_status(ALL, dir) == gather_status_t::DONE;
        } else {
            return gather_status(ALL, dir) == gather_status_t::DONE ||
                   (gather_status(EVEN, dir) == gather_status_t::DONE &&
                    gather_status(ODD, dir) == gather_status_t::DONE);
        }
    }

    /**
     * @brief Mark communication has started
     * @hilapponly
     * @param dir
     * @param p
     */
    void mark_gather_started(int dir, Parity p) const {
        set_gather_status(p, dir, gather_status_t::STARTED);
    }

    /**
     * @brief Check if communication has started
     * @hilapponly
     * @param dir
     * @param par
     * @return bool
     */
    bool is_gather_started(int dir, Parity par) const {
        return gather_status(par, dir) == gather_status_t::STARTED;
    }

    bool gather_not_done(int dir, Parity par) const {
        return gather_status(par, dir) == gather_status_t::NOT_DONE;
    }

    /// function boundary_need_to_communicate(dir) returns false if there's special B.C. which
    /// does not need comms here, otherwise true

    bool boundary_need_to_communicate(const Direction dir) const {
#ifdef SPECIAL_BOUNDARY_CONDITIONS

        return hila::bc_need_communication(fs->boundary_condition[dir]) ||
               !lattice.mynode.is_on_edge(dir);
#else
        return true;
#endif
    }


    /**
     * @brief Set the boundary condition in a given Direction (periodic or antiperiodic)
     *
     * @param dir Direction of boundary condition
     * @param bc Field boundary condition
     */
    void set_boundary_condition(Direction dir, hila::bc bc) {

#ifdef SPECIAL_BOUNDARY_CONDITIONS


#ifdef HILAPP
        // this section is just to generate loop function calls for unary-.  Will removed by hilapp
        // from final code.  If type T does not have -, give error

        static_assert(has_unary_minus<T>::value, "BC possible only for types which implement unary "
                                                 "minus (-) -operator and are not unsigned");

        ensure_unary_minus_is_loop_function<T>();
#endif

        // TODO: This works as intended only for periodic/antiperiodic b.c.
        check_alloc();
        fs->boundary_condition[dir] = bc;
        fs->boundary_condition[-dir] = bc;
#if !defined(CUDA) && !defined(HIP)
        fs->neighbours[dir] = lattice.get_neighbour_array(dir, bc);
        fs->neighbours[-dir] = lattice.get_neighbour_array(-dir, bc);
#else
        if (bc == hila::bc::PERIODIC) {
            fs->payload.neighbours[dir] = lattice.backend_lattice->d_neighb[dir];
            fs->payload.neighbours[-dir] = lattice.backend_lattice->d_neighb[-dir];
        } else {
            fs->payload.neighbours[dir] = lattice.backend_lattice->d_neighb_special[dir];
            fs->payload.neighbours[-dir] = lattice.backend_lattice->d_neighb_special[-dir];
        }
#endif

        // Make sure boundaries get refreshed
        mark_changed(ALL);
#else
        assert(bc == hila::bc::PERIODIC &&
               "Only periodic bondary conditions when SPECIAL_BOUNDARY_CONDITIONS is undefined");
#endif
    }

    /**
     * @brief Get the boundary condition of the Field
     *
     * @param dir Boundary condition in certain direction
     * @return hila::bc The boundary condition of the Field
     */
    hila::bc get_boundary_condition(Direction dir) const {
#ifdef SPECIAL_BOUNDARY_CONDITIONS
        return fs->boundary_condition[dir];
#else
        return hila::bc::PERIODIC;
#endif
    }

    void print_boundary_condition() {
        check_alloc();
        hila::out0 << " ( ";
        for (int dir = 0; dir < NDIRS; dir++) {
            hila::out0 << (int)fs->boundary_condition[dir] << " ";
        }
        hila::out0 << ")\n";
    }

    /**
     * @brief Copy the boundary condition from another field
     *
     * @tparam A the type of the field which we are copying from
     * @param rhs the ohter Field
     */
    template <typename A>
    void copy_boundary_condition(const Field<A> &rhs) {
        foralldir(dir) {
            set_boundary_condition(dir, rhs.get_boundary_condition(dir));
        }
    }

    // Overloading []
    // declarations -- WILL BE implemented by hilapp, not written here
    // let there be const and non-const protos
    T operator[](const Parity p) const;           // f[EVEN]
    T operator[](const X_index_type) const;       // f[X]
    T operator[](const X_plus_direction p) const; // f[X+dir]
    T operator[](const X_plus_offset p) const;    // f[X+dir1+dir2] and others

    T &operator[](const Parity p);     // f[EVEN]
    T &operator[](const X_index_type); // f[X]

    T &operator[](const CoordinateVector &v);       // f[CoordinateVector]
    T &operator[](const CoordinateVector &v) const; // f[CoordinateVector]

    // TEMPORARY HACK: return ptr to bare array
    inline auto field_buffer() const {
        return fs->payload.get_buffer();
    }

#ifndef VECTORIZED
    /// Get an individual element outside a loop. This is also used as a getter in the
    /// vanilla code.
    inline auto get_value_at(const unsigned i) const {
        return fs->get_element(i);
    }
#else
    inline auto get_value_at(const unsigned i) const {
        return fs->get_element(i);
    }
    template <typename vecT>
    inline auto get_vector_at(unsigned i) const {
        return fs->template get_vector<vecT>(i);
    }
    inline auto get_value_at_nb_site(Direction d, unsigned i) const {
        return fs->get_element(fs->vector_lattice->site_neighbour(d, i));
    }
#endif

#ifndef VECTORIZED
    /// Set an individual element outside a loop. This is also used as a setter in the
    /// vanilla code.
    template <typename A>
    inline void set_value_at(const A &value, unsigned i) {
        fs->set_element(value, i);
    }

#else
    template <typename vecT>
    inline void set_vector_at(const vecT &value, unsigned i) {
        fs->set_vector(value, i);
    }

    template <typename A>
    inline void set_value_at(const A &value, unsigned i) {
        fs->set_element(value, i);
    }
#endif

    /**
     * @name Standard arithmetic operations
     * @brief Standard arithmetic operations which fields should implement
     *        Not all are always callable, e.g. division may not be
     *        implemented by all field types
     *  @{
     */
    /**
     * @brief Basic assignment operator
     *
     * @param rhs
     * @return Field<T>& Assigned field
     */
    Field<T> &operator=(const Field<T> &rhs) {
        (*this)[ALL] = rhs[X];
        return *this;
    }

    /**
     * @brief More general assignment operation if A can be casted into T
     *
     * @tparam A Type of element to be assigned
     * @param rhs Field to assign from
     * @return Field<T>& Assigned field
     */
    template <typename A,
              std::enable_if_t<
                  hila::is_assignable<T &, A>::value || std::is_convertible<A, T>::value, int> = 0>
    Field<T> &operator=(const Field<A> &rhs) {
        (*this)[ALL] = rhs[X];
        return *this;
    }

    /**
     * @brief Assginment from element
     *
     * @tparam A Type of element to be assigned
     * @param d element to assign to Field
     * @return Field<T>& Assigned Field
     */
    template <typename A,
              std::enable_if_t<
                  hila::is_assignable<T &, A>::value || std::is_convertible<A, T>::value, int> = 0>
    Field<T> &operator=(const A &d) {
        (*this)[ALL] = d;
        return *this;
    }

    /**
     * @brief assignment of 0 - nullptr, zero field
     *
     * @param z nullptr
     * @return Field<T>& Zero Field
     */
    Field<T> &operator=(const std::nullptr_t &z) {
        (*this)[ALL] = 0;
        return *this;
    }

    /**
     * @brief Move Assignment
     *
     * @param rhs
     * @return Field<T>&
     */
    Field<T> &operator=(Field<T> &&rhs) {
        if (this != &rhs) {
            free();
            fs = rhs.fs;
            rhs.fs = nullptr;
        }
        return *this;
    }

    /**
     * @brief += Operator between two fields if A and Field type T are compatible
     *
     * @tparam A Type of r.h.s element
     * @param rhs Field to sum
     * @return Field<T>&
     * \code{.cpp}
     * Field<MyType> f,g;
     * . . .
     * f += g;
     * \endcode
     */
    template <typename A,
              std::enable_if_t<std::is_convertible<hila::type_plus<T, A>, T>::value, int> = 0>
    Field<T> &operator+=(const Field<A> &rhs) {
        (*this)[ALL] += rhs[X];
        return *this;
    }

    /**
     * @brief -= Operator between two fields if A and Field type T are compatible
     *
     * @tparam A Type of r.h.s element
     * @param rhs Field to subtract
     * @return Field<T>&
     * \code{.cpp}
     * Field<MyType> f,g;
     * . . .
     * f -= g;
     * \endcode
     */
    template <typename A,
              std::enable_if_t<std::is_convertible<hila::type_minus<T, A>, T>::value, int> = 0>
    Field<T> &operator-=(const Field<A> &rhs) {
        (*this)[ALL] -= rhs[X];
        return *this;
    }

    /**
     * @brief *= Operator between two fields if A and Field type T are compatible
     *
     * @tparam A Type of r.h.s element
     * @param rhs Field to compute product with
     * @return Field<T>&
     * \code{.cpp}
     * Field<MyType> f,g;
     * . . .
     * f *= g;
     * \endcode
     */
    template <typename A,
              std::enable_if_t<std::is_convertible<hila::type_mul<T, A>, T>::value, int> = 0>
    Field<T> &operator*=(const Field<A> &rhs) {
        (*this)[ALL] *= rhs[X];
        return *this;
    }

    /**
     * @brief /= Operator between two fields if A and Field type T are compatible
     *
     * @tparam A Type of r.h.s element
     * @param rhs Field to divide with
     * @return Field<T>&
     * \code{.cpp}
     * Field<MyType> f,g;
     * . . .
     * f /= g;
     * \endcode
     */
    template <typename A,
              std::enable_if_t<std::is_convertible<hila::type_div<T, A>, T>::value, int> = 0>
    Field<T> &operator/=(const Field<A> &rhs) {
        (*this)[ALL] /= rhs[X];
        return *this;
    }

    /**
     * @brief += Operator between element and field if type of rhs and Field type T are compatible
     *
     * @tparam A Type of r.h.s element
     * @param rhs Element to sum
     * @return Field<T>&
     * \code{.cpp}
     * Field<MyType> f;
     * MyType a
     * . . .
     * f += a;
     * \endcode
     */
    template <typename A,
              std::enable_if_t<std::is_convertible<hila::type_plus<T, A>, T>::value, int> = 0>
    Field<T> &operator+=(const A &rhs) {
        (*this)[ALL] += rhs;
        return *this;
    }

    /**
     * @brief -= Operator between element and field if type of rhs and Field type T are compatible
     *
     * @tparam A Type of r.h.s element
     * @param rhs Element to subtract
     * @return Field<T>&
     * \code{.cpp}
     * Field<MyType> f;
     * MyType a
     * . . .
     * f -= a;
     * \endcode
     */
    template <typename A,
              std::enable_if_t<std::is_convertible<hila::type_minus<T, A>, T>::value, int> = 0>
    Field<T> &operator-=(const A &rhs) {
        (*this)[ALL] -= rhs;
        return *this;
    }

    /**
     * @brief *= Operator between element and field if type of rhs and Field type T are compatible
     *
     * @tparam A Type of r.h.s element
     * @param rhs Element to multiply with
     * @return Field<T>&
     * \code{.cpp}
     * Field<MyType> f;
     * MyType a
     * . . .
     * f *= a;
     * \endcode
     */
    template <typename A,
              std::enable_if_t<std::is_convertible<hila::type_mul<T, A>, T>::value, int> = 0>
    Field<T> &operator*=(const A &rhs) {
        (*this)[ALL] *= rhs;
        return *this;
    }

    /**
     * @brief /= Operator between element and field if type of rhs and Field type T are compatible
     *
     * @tparam A Type of r.h.s element
     * @param rhs Element to divide with
     * @return Field<T>&
     * \code{.cpp}
     * Field<MyType> f;
     * MyType a
     * . . .
     * f /= a;
     * \endcode
     */
    template <typename A,
              std::enable_if_t<std::is_convertible<hila::type_div<T, A>, T>::value, int> = 0>
    Field<T> &operator/=(const A &rhs) {
        (*this)[ALL] /= rhs;
        return *this;
    }

    // Unary + and -

    /**
     * @brief Unary + operator, acts as Identity
     *
     * @return Field<T> Returns itself
     */
    Field<T> operator+() const {
        return *this;
    }

    /**
     * @brief Unary - operator, acts as negation to all field elements
     *
     * @return Field<T>
     */
    Field<T> operator-() const {
        Field<T> f;
        f[ALL] = -(*this)[X];
        return f;
    }

    /**
     * @brief Field comparison operator.
     * @details Computes squarenorm of difference of two fields and checks if squarenorm is less
     * than tolerance epsilon=0.
     *
     * @param rhs Field to compare Field with
     * @return true
     * @return false
     */
    bool operator==(const Field<T> &rhs) const {
        hila::scalar_type<T> epsilon = 0;
        return ((*this) - rhs).squarenorm() <= epsilon;
    }

    /**
     * @brief Computes squarenorm of Field depending on how it is defined for Field type T
     *
     * @return double
     */
    double squarenorm() const {
        double n = 0;
        onsites(ALL) {
            n += ::squarenorm((*this)[X]);
        }
        return n;
    }

    /**
     * @brief Computes norm of Field depending on how it is defined for Field type T
     *
     * @return double
     */
    double norm() {
        return sqrt(squarenorm());
    }

    /**
     * @brief Computes conjugate of Field depending on how it is defined for Field type T
     *
     * @return Field<T>
     */
    Field<T> conj() const {
        Field<T> f;
        f[ALL] = ::conj((*this)[X]);
        return f;
    }

    /**
     * @brief Computes dagger or Hermitian conjugate of Field depending on how it is
     * defined for Field type T
     *
     * @return Field<T>
     */
    template <typename R = T, typename A = decltype(::dagger(std::declval<R>()))>
    Field<A> dagger() const {
        Field<A> f;
        f[ALL] = ::dagger((*this)[X]);
        return f;
    }

    /**
     * @brief Returns real part of Field
     *
     * @tparam R Field current type
     * @tparam A Field real part type
     * @return Field<A>
     */
    template <typename R = T, typename A = decltype(::real(std::declval<R>()))>
    Field<A> real() const {
        Field<A> f;
        f[ALL] = ::real((*this)[X]);
        return f;
    }

    /**
     * @brief Returns imaginary part of Field
     *
     * @tparam R Field current type
     * @tparam A Field imaginary part type
     * @return Field<A>
     */
    template <typename R = T, typename A = decltype(::imag(std::declval<R>()))>
    Field<A> imag() const {
        Field<A> f;
        f[ALL] = ::imag((*this)[X]);
        return f;
    }
    /** @} */

    /// Communication routines
    dir_mask_t start_gather(Direction d, Parity p = ALL) const;
    void wait_gather(Direction d, Parity p) const;
    void gather(Direction d, Parity p = ALL) const;
    void drop_comms(Direction d, Parity p) const;
    void cancel_comm(Direction d, Parity p) const;

    /**
     * @name Shift operations
     * @{
     */

    /**
     * @brief Create a periodically shifted copy of the field
     * @details  this is currently OK only for short moves, very inefficient for longer moves
     * @param v CoordinateVector to shift field with
     * @param r Field to store result in
     * @param par Parity
     * @return Field<T>&
     */
    Field<T> &shift(const CoordinateVector &v, Field<T> &r, Parity par) const;
    /**
     * @brief Create a periodically shifted copy of the field
     * @details this is currently OK only for short moves, very inefficient for longer moves.
     * If Parity is not given to shift, then it is called with Parity ALL
     * @param v CoordinateVector to shift field with
     * @param r Field to store result in
     * @return Field<T>&
     */
    Field<T> &shift(const CoordinateVector &v, Field<T> &r) const {
        return shift(v, r, ALL);
    }
    /**
     * @brief Create a periodically shifted copy of the field
     * @details This is currently OK only for short moves, very inefficient for longer moves
     * @param v CoordinateVector to shift field with
     * @param par Parity
     * @return Field<T>
     */
    Field<T> shift(const CoordinateVector &v, Parity par) const;
    /** @} */

    // General getters and setters

    /// Set a single element. Assuming that each node calls this with the same value, it
    /// is sufficient to set the element locally

    template <typename A, std::enable_if_t<std::is_assignable<T &, A>::value, int> = 0>
    void set_element(const CoordinateVector &coord, const A &value) {
        if (lattice.is_on_mynode(coord)) {
            T element;
            element = value;
            set_value_at(element, lattice.site_index(coord));
        }
        mark_changed(coord.parity());
    }

    /**
     * @brief Get singular element which will be broadcast to all nodes
     *
     * @param coord coordinate of which the element is fetched with
     * @return const T
     */
    const T get_element(const CoordinateVector &coord) const {
        T element;

        int owner = lattice.node_rank(coord);

        if (hila::myrank() == owner) {
            element = get_value_at(lattice.site_index(coord));
        }


        MPI_Bcast(&element, sizeof(T), MPI_BYTE, owner, lattice.mpi_comm_lat);

        return element;
    }

    /**
     * @brief Set an array of elements in the field
     * @remark Assuming that each node calls this with the same value,
     * it is sufficient to set the elements locally
     *
     * @param elements @typedef vector<T> of elements to set
     * @param coord_list @typedef vector<CoordinateVector> of coordinates to set
     */
    void set_elements(const std::vector<T> &elements,
                      const std::vector<CoordinateVector> &coord_list);
    /**
     * @brief Retrieves list of elements to all nodes.
     * @param coord_list vector of coordinates which will be fetched
     * @param broadcast if true then elements retrieved to root node will be broadcast to all nodes
     * @return std::vector<T> list of all elements
     */
    std::vector<T> get_elements(const std::vector<CoordinateVector> &coord_list,
                                bool broadcast = false) const;

    std::vector<T> get_subvolume(const CoordinateVector &cmin, const CoordinateVector &cmax,
                                 bool broadcast = false) const;

    std::vector<T> get_slice(const CoordinateVector &c, bool broadcast = false) const;

    void copy_local_data(std::vector<T> &buffer) const;
    void set_local_data(const std::vector<T> &buffer);

    void copy_local_data_with_halo(std::vector<T> &buffer) const;


    // inline void set_element_at(const CoordinateVector &coord, const A &elem) {
    //     T e;
    //     e = elem;
    //     set_element(e, coord);
    // }

    // inline void set_element_at(const CoordinateVector &coord, std::nullptr_t elem) {
    //     T e;
    //     e = 0;
    //     set_element(e, coord);
    // }

    template <typename A,
              std::enable_if_t<std::is_assignable<T &, hila::type_plus<T, A>>::value, int> = 0>
    inline void compound_add_element(const CoordinateVector &coord, const A &av) {
        if (lattice.is_on_mynode(coord)) {
            auto i = lattice.site_index(coord);
            auto v = get_value_at(i);
            v += av;
            set_value_at(v, i);
        }
        mark_changed(coord.parity());
    }

    template <typename A,
              std::enable_if_t<std::is_assignable<T &, hila::type_minus<T, A>>::value, int> = 0>
    inline void compound_sub_element(const CoordinateVector &coord, const A &av) {
        if (lattice.is_on_mynode(coord)) {
            auto i = lattice.site_index(coord);
            auto v = get_value_at(i);
            v -= av;
            set_value_at(v, i);
        }
        mark_changed(coord.parity());
    }

    template <typename A,
              std::enable_if_t<std::is_assignable<T &, hila::type_mul<T, A>>::value, int> = 0>
    inline void compound_mul_element(const CoordinateVector &coord, const A &av) {
        if (lattice.is_on_mynode(coord)) {
            auto i = lattice.site_index(coord);
            auto v = get_value_at(i);
            v *= av;
            set_value_at(v, i);
        }
        mark_changed(coord.parity());
    }

    template <typename A,
              std::enable_if_t<std::is_assignable<T &, hila::type_div<T, A>>::value, int> = 0>
    inline void compound_div_element(const CoordinateVector &coord, const A &av) {
        if (lattice.is_on_mynode(coord)) {
            auto i = lattice.site_index(coord);
            auto v = get_value_at(i);
            v /= av;
            set_value_at(v, i);
        }
        mark_changed(coord.parity());
    }


    // Fourier transform declarations
    Field<T> FFT(fft_direction fdir = fft_direction::forward) const;
    Field<T> FFT(const CoordinateVector &dirs, fft_direction fdir = fft_direction::forward) const;

    Field<Complex<hila::scalar_type<T>>>
    FFT_real_to_complex(fft_direction fdir = fft_direction::forward) const;
    Field<hila::scalar_type<T>>
    FFT_complex_to_real(fft_direction fdir = fft_direction::forward) const;


    // Reflect the field along all or 1 coordinate
    Field<T> reflect() const;
    Field<T> reflect(Direction dir) const;
    Field<T> reflect(const CoordinateVector &dirs) const;

    // Writes the Field to disk
    void write(std::ofstream &outputfile, bool binary = true, int precision = 8) const;
    void write(const std::string &filename, bool binary = true, int precision = 8) const;

    void read(std::ifstream &inputfile);
    void read(const std::string &filename);

    void write_subvolume(std::ofstream &outputfile, const CoordinateVector &cmin,
                         const CoordinateVector &cmax, int precision = 6) const;
    void write_subvolume(const std::string &filenname, const CoordinateVector &cmin,
                         const CoordinateVector &cmax, int precision = 6) const;

    template <typename Out>
    void write_slice(Out &outputfile, const CoordinateVector &slice, int precision = 6) const;

    /**
     * @brief Sum reduction of Field
     * @details The sum in the reduction is defined by the Field type T
     * @param par Parity
     * @param allreduce Switch to turn on or off MPI allreduce
     * @return T Field element type reduced to one element
     */
    T sum(Parity par = Parity::all, bool allreduce = true) const;

    /**
     * @brief Product reduction of Field
     * @details The product in the reduction is defined by the Field type T.
     * @param par Parity
     * @param allreduce Switch to turn on or off MPI allreduce
     * @return T Field element type reduced to one element
     */
    T product(Parity par = Parity::all, bool allreduce = true) const;

    /**
     * @brief Declare gpu_reduce here, defined only for GPU targets
     * @internal
     * @param min_or_max
     * @param par
     * @param loc
     * @return T
     */
    T gpu_minmax(bool min_or_max, Parity par, CoordinateVector &loc) const;

    /**
     * @brief Minimum value of Field. If CoordinateVector is passed to function
     * then location of minimum value will be stored in said CoordinateVector
     * @name Min functions
     * @param par ::Parity
     * @return T Minimum value of Field
     */
    /** @{ */
    T min(Parity par = ALL) const;
    T min(CoordinateVector &loc) const;
    T min(Parity par, CoordinateVector &loc) const;
    /** @} */

    /**
     * @brief Maximum value of Field. If CoordinateVector is passed to function
     * then location of maximum value will be stored in said CoordinateVector
     * @name Max functions
     * @param par ::Parity
     * @return T Minimum value of Field
     */
    /** @{ */
    T max(Parity par = ALL) const;
    T max(CoordinateVector &loc) const;
    T max(Parity par, CoordinateVector &loc) const;
    /** @} */

    /**
     * @brief Function to perform min or max operations
     * @internal
     *
     * @param is_min if true we compute min
     * @param par Parity
     * @param loc Location of min or max
     * @return T
     */
    T minmax(bool is_min, Parity par, CoordinateVector &loc) const;

    void random();
    void gaussian_random(double width = 1.0);


}; // End of class Field<>

///////////////////////////////
// operators +-*/
// these operators rely on SFINAE, OK if field_hila::type_plus<A,B> exists i.e. A+B is
// OK
// There are several versions of the operators, depending if one of the arguments is
// Field or scalar, and if the return type is the same as the Field argument.  This can
// enable the compiler to avoid extra copies of the args

///////////////////////////////
/// operator +  (Field + Field) -generic
template <typename A, typename B,
          std::enable_if_t<!std::is_same<hila::type_plus<A, B>, A>::value &&
                               !std::is_same<hila::type_plus<A, B>, B>::value,
                           int> = 0>
auto operator+(const Field<A> &lhs, const Field<B> &rhs) -> Field<hila::type_plus<A, B>> {
    Field<hila::type_plus<A, B>> tmp;
    tmp[ALL] = lhs[X] + rhs[X];
    return tmp;
}

// (Possibly) optimzed version where the 1st argument can be reused
template <typename A, typename B,
          std::enable_if_t<std::is_same<hila::type_plus<A, B>, A>::value, int> = 0>
auto operator+(Field<A> lhs, const Field<B> &rhs) {
    lhs[ALL] += rhs[X];
    return lhs;
}

// Optimzed version where the 2nd argument can be reused
template <typename A, typename B,
          std::enable_if_t<!std::is_same<hila::type_plus<A, B>, A>::value &&
                               std::is_same<hila::type_plus<A, B>, B>::value,
                           int> = 0>
auto operator+(const Field<A> &lhs, Field<B> rhs) {
    rhs[ALL] += lhs[X];
    return rhs;
}

//////////////////////////////
/// operator + (Field + scalar)

template <typename A, typename B,
          std::enable_if_t<!std::is_same<hila::type_plus<A, B>, A>::value, int> = 0>
auto operator+(const Field<A> &lhs, const B &rhs) -> Field<hila::type_plus<A, B>> {
    Field<hila::type_plus<A, B>> tmp;
    tmp[ALL] = lhs[X] + rhs;
    return tmp;
}

template <typename A, typename B,
          std::enable_if_t<std::is_same<hila::type_plus<A, B>, A>::value, int> = 0>
Field<A> operator+(Field<A> lhs, const B &rhs) {
    lhs[ALL] += rhs;
    return lhs;
}


//////////////////////////////
/// operator + (scalar + Field)

template <typename A, typename B,
          std::enable_if_t<!std::is_same<hila::type_plus<A, B>, B>::value, int> = 0>
auto operator+(const A &lhs, const Field<B> &rhs) -> Field<hila::type_plus<A, B>> {
    return rhs + lhs;
}

template <typename A, typename B,
          std::enable_if_t<std::is_same<hila::type_plus<A, B>, B>::value, int> = 0>
Field<B> operator+(const A &lhs, Field<B> rhs) {
    return rhs + lhs;
}


//////////////////////////////
/// operator - Field - Field -generic
template <typename A, typename B,
          std::enable_if_t<!std::is_same<hila::type_minus<A, B>, A>::value &&
                               !std::is_same<hila::type_minus<A, B>, B>::value,
                           int> = 0>
auto operator-(const Field<A> &lhs, const Field<B> &rhs) -> Field<hila::type_minus<A, B>> {
    Field<hila::type_minus<A, B>> tmp;
    tmp[ALL] = lhs[X] - rhs[X];
    return tmp;
}

// Optimzed version where the 1st argument can be reused
template <typename A, typename B,
          std::enable_if_t<std::is_same<hila::type_minus<A, B>, A>::value, int> = 0>
auto operator-(Field<A> lhs, const Field<B> &rhs) {
    lhs[ALL] -= rhs[X];
    return lhs;
}

// Optimzed version where the 2nd argument can be reused
template <typename A, typename B,
          std::enable_if_t<!std::is_same<hila::type_minus<A, B>, A>::value &&
                               std::is_same<hila::type_minus<A, B>, B>::value,
                           int> = 0>
auto operator-(const Field<A> &lhs, Field<B> rhs) {
    rhs[ALL] = lhs[X] - rhs[X];
    return rhs;
}

//////////////////////////////
/// operator - (Field - scalar)

template <typename A, typename B,
          std::enable_if_t<!std::is_same<hila::type_minus<A, B>, A>::value, int> = 0>
auto operator-(const Field<A> &lhs, const B &rhs) -> Field<hila::type_minus<A, B>> {
    Field<hila::type_minus<A, B>> tmp;
    tmp[ALL] = lhs[X] - rhs;
    return tmp;
}

template <typename A, typename B,
          std::enable_if_t<std::is_same<hila::type_minus<A, B>, A>::value, int> = 0>
Field<A> operator-(Field<A> lhs, const B &rhs) {
    lhs[ALL] -= rhs;
    return lhs;
}

//////////////////////////////
/// operator - (scalar - Field)

template <typename A, typename B,
          std::enable_if_t<!std::is_same<hila::type_minus<A, B>, B>::value, int> = 0>
auto operator-(const A &lhs, const Field<B> &rhs) -> Field<hila::type_minus<A, B>> {
    Field<hila::type_minus<A, B>> tmp;
    tmp[ALL] = lhs - rhs[X];
    return tmp;
}

template <typename A, typename B,
          std::enable_if_t<std::is_same<hila::type_minus<A, B>, B>::value, int> = 0>
Field<B> operator-(const A &lhs, Field<B> rhs) {
    rhs[ALL] = lhs - rhs[X];
    return rhs;
}

///////////////////////////////
/// operator * (Field * Field)
/// generic
template <typename A, typename B,
          std::enable_if_t<!std::is_same<hila::type_mul<A, B>, A>::value &&
                               !std::is_same<hila::type_mul<A, B>, B>::value,
                           int> = 0>
auto operator*(const Field<A> &lhs, const Field<B> &rhs) -> Field<hila::type_mul<A, B>> {
    Field<hila::type_mul<A, B>> tmp;
    tmp[ALL] = lhs[X] * rhs[X];
    return tmp;
}

/// reuse 1st
template <typename A, typename B,
          std::enable_if_t<std::is_same<hila::type_mul<A, B>, A>::value, int> = 0>
Field<A> operator*(Field<A> lhs, const Field<B> &rhs) {
    lhs[ALL] = lhs[X] * rhs[X];
    return lhs;
}

/// reuse 2nd
template <typename A, typename B,
          std::enable_if_t<!std::is_same<hila::type_mul<A, B>, A>::value &&
                               std::is_same<hila::type_mul<A, B>, B>::value,
                           int> = 0>
Field<B> operator*(const Field<A> &lhs, Field<B> rhs) {
    rhs[ALL] = lhs[X] * rhs[X];
    return rhs;
}

/////////////////////////////////
/// operator * (scalar * field)

template <typename A, typename B,
          std::enable_if_t<!std::is_same<hila::type_mul<A, B>, B>::value, int> = 0>
auto operator*(const A &lhs, const Field<B> &rhs) -> Field<hila::type_mul<A, B>> {
    Field<hila::type_mul<A, B>> tmp;
    tmp[ALL] = lhs * rhs[X];
    return tmp;
}

template <typename A, typename B,
          std::enable_if_t<std::is_same<hila::type_mul<A, B>, B>::value, int> = 0>
Field<B> operator*(const A &lhs, Field<B> rhs) {
    rhs[ALL] = lhs * rhs[X];
    return rhs;
}

/////////////////////////////////
/// operator * (field * scalar)

template <typename A, typename B,
          std::enable_if_t<!std::is_same<hila::type_mul<A, B>, A>::value, int> = 0>
auto operator*(const Field<A> &lhs, const B &rhs) -> Field<hila::type_mul<A, B>> {
    Field<hila::type_mul<A, B>> tmp;
    tmp[ALL] = lhs[X] * rhs;
    return tmp;
}

template <typename A, typename B,
          std::enable_if_t<std::is_same<hila::type_mul<A, B>, A>::value, int> = 0>
Field<A> operator*(Field<A> lhs, const B &rhs) {
    lhs[ALL] = lhs[X] * rhs;
    return lhs;
}

///////////////////////////////
/// operator / (Field / Field)
/// generic
template <typename A, typename B,
          std::enable_if_t<!std::is_same<hila::type_div<A, B>, A>::value &&
                               !std::is_same<hila::type_div<A, B>, B>::value,
                           int> = 0>
auto operator/(const Field<A> &l, const Field<B> &r) -> Field<hila::type_div<A, B>> {
    Field<hila::type_div<A, B>> tmp;
    tmp[ALL] = l[X] / r[X];
    return tmp;
}

/// reuse 1st
template <typename A, typename B,
          std::enable_if_t<std::is_same<hila::type_div<A, B>, A>::value, int> = 0>
Field<A> operator/(Field<A> l, const Field<B> &r) {
    l[ALL] = l[X] / r[X];
    return l;
}

/// reuse 2nd
template <typename A, typename B,
          std::enable_if_t<!std::is_same<hila::type_div<A, B>, A>::value &&
                               std::is_same<hila::type_div<A, B>, B>::value,
                           int> = 0>
Field<B> operator/(const Field<A> &l, Field<B> r) {
    r[ALL] = l[X] / r[X];
    return r;
}

//////////////////////////////////
/// operator /  (scalar/Field)
template <typename A, typename B,
          std::enable_if_t<!std::is_same<hila::type_div<A, B>, B>::value, int> = 0>
auto operator/(const A &lhs, const Field<B> &rhs) -> Field<hila::type_div<A, B>> {
    Field<hila::type_div<A, B>> tmp;
    tmp[ALL] = lhs / rhs[X];
    return tmp;
}

template <typename A, typename B,
          std::enable_if_t<std::is_same<hila::type_div<A, B>, B>::value, int> = 0>
Field<B> operator/(const A &lhs, Field<B> rhs) {
    rhs[ALL] = lhs / rhs[X];
    return rhs;
}

//////////////////////////////////
/// operator /  (Field/scalar)
template <typename A, typename B,
          std::enable_if_t<!std::is_same<hila::type_div<A, B>, A>::value, int> = 0>
auto operator/(const Field<A> &lhs, const B &rhs) -> Field<hila::type_div<A, B>> {
    Field<hila::type_div<A, B>> tmp;
    tmp[ALL] = lhs[X] / rhs;
    return tmp;
}

template <typename A, typename B,
          std::enable_if_t<std::is_same<hila::type_div<A, B>, A>::value, int> = 0>
auto operator/(Field<A> lhs, const B &rhs) {
    lhs[ALL] = lhs[X] / rhs;
    return lhs;
}

/**
 * @brief std:swap() for Fields
 *
 */
namespace std {
template <typename T>
void swap(Field<T> &A, Field<T> &B) {
    std::swap(A.fs, B.fs);
}
} // namespace std

///////////////////////////////////////////////////////////////////////
/// Allow some arithmetic functions if implemented

template <typename T, typename R = decltype(exp(std::declval<T>()))>
Field<R> exp(const Field<T> &arg) {
    Field<R> res;
    onsites(ALL) {
        res[X] = exp(arg[X]);
    }
    return res;
}

template <typename T, typename R = decltype(log(std::declval<T>()))>
Field<R> log(const Field<T> &arg) {
    Field<R> res;
    onsites(ALL) {
        res[X] = log(arg[X]);
    }
    return res;
}

template <typename T, typename R = decltype(sin(std::declval<T>()))>
Field<R> sin(const Field<T> &arg) {
    Field<R> res;
    onsites(ALL) {
        res[X] = sin(arg[X]);
    }
    return res;
}

template <typename T, typename R = decltype(cos(std::declval<T>()))>
Field<R> cos(const Field<T> &arg) {
    Field<R> res;
    onsites(ALL) {
        res[X] = cos(arg[X]);
    }
    return res;
}

template <typename T, typename R = decltype(tan(std::declval<T>()))>
Field<R> tan(const Field<T> &arg) {
    Field<R> res;
    onsites(ALL) {
        res[X] = tan(arg[X]);
    }
    return res;
}

template <typename T, typename R = decltype(asin(std::declval<T>()))>
Field<R> asin(const Field<T> &arg) {
    Field<R> res;
    onsites(ALL) {
        res[X] = asin(arg[X]);
    }
    return res;
}

template <typename T, typename R = decltype(acos(std::declval<T>()))>
Field<R> acos(const Field<T> &arg) {
    Field<R> res;
    onsites(ALL) {
        res[X] = acos(arg[X]);
    }
    return res;
}

template <typename T, typename R = decltype(atan(std::declval<T>()))>
Field<R> atan(const Field<T> &arg) {
    Field<R> res;
    onsites(ALL) {
        res[X] = atan(arg[X]);
    }
    return res;
}

template <typename T, typename R = decltype(abs(std::declval<T>()))>
Field<R> abs(const Field<T> &arg) {
    Field<R> res;
    onsites(ALL) {
        res[X] = abs(arg[X]);
    }
    return res;
}

template <typename T, typename P, typename R = decltype(pow(std::declval<T>()), std::declval<P>())>
Field<R> pow(const Field<T> &arg, const P p) {
    Field<R> res;
    onsites(ALL) {
        res[X] = pow(arg[X], p);
    }
    return res;
}

template <typename T>
double squarenorm(const Field<T> &arg) {
    double r = 0;
    onsites(ALL) {
        r += squarenorm(arg[X]);
    }
    return r;
}

template <typename T>
double norm(const Field<T> &arg) {
    return sqrt(squarenorm(arg));
}

template <typename T>
Field<T> conj(const Field<T> &arg) {
    return arg.conj();
}

template <typename T, typename A = decltype(::dagger(std::declval<T>()))>
Field<A> dagger(const Field<T> &arg) {
    return arg.dagger();
}

template <typename T, typename A = decltype(::real(std::declval<T>()))>
Field<A> real(const Field<T> &arg) {
    return arg.real();
}

template <typename T, typename A = decltype(::imag(std::declval<T>()))>
Field<A> imag(const Field<T> &arg) {
    return arg.imag();
}


template <typename A, typename B, typename R = decltype(std::declval<A>() - std::declval<B>())>
double squarenorm_relative(const Field<A> &a, const Field<B> &b) {
    double res = 0;
    onsites(ALL) {
        res += squarenorm(a[X] - b[X]);
    }
    return res;
}


template <typename T>
Field<T> Field<T>::shift(const CoordinateVector &v, const Parity par) const {
    Field<T> res;
    shift(v, res, par);
    return res;
}


///  drop_comms():  if field is changed or deleted,
///  cancel ongoing communications.  This should happen very seldom,
///  only if there are "by-hand" start_gather operations and these are not needed
template <typename T>
void Field<T>::drop_comms(Direction d, Parity p) const {

    if (is_comm_initialized()) {
        if (is_gather_started(d, ALL))
            cancel_comm(d, ALL);
        if (p != ALL) {
            if (is_gather_started(d, p))
                cancel_comm(d, p);
        } else {
            if (is_gather_started(d, EVEN))
                cancel_comm(d, EVEN);
            if (is_gather_started(d, ODD))
                cancel_comm(d, ODD);
        }
    }
}

/// cancel ongoing send and receive

template <typename T>
void Field<T>::cancel_comm(Direction d, Parity p) const {
    if (lattice.nn_comminfo[d].from_node.rank != hila::myrank()) {
        cancel_receive_timer.start();
        MPI_Cancel(&fs->receive_request[(int)p - 1][d]);
        cancel_receive_timer.stop();
    }
    if (lattice.nn_comminfo[d].to_node.rank != hila::myrank()) {
        cancel_send_timer.start();
        MPI_Cancel(&fs->send_request[(int)p - 1][d]);
        cancel_send_timer.stop();
    }
}


/// And a convenience combi function
template <typename T>
void Field<T>::gather(Direction d, Parity p) const {
    start_gather(d, p);
    wait_gather(d, p);
}


// Read in the "technical" communication bits

#include "field_comm.h"


template <typename T>
void Field<T>::random() {

#if defined(CUDA) || defined(HIP)

    if (!hila::is_device_rng_on()) {

        std::vector<T> rng_buffer(lattice.mynode.volume());
        for (auto &element : rng_buffer)
            hila::random(element);
        (*this).set_local_data(rng_buffer);

    } else {
        onsites(ALL) {
            hila::random((*this)[X]);
        }
    }
#else

    onsites(ALL) {
        hila::random((*this)[X]);
    }

#endif
}


#ifdef HILAPP

////////////////////////////////////////////////////////////////////////////////
// A couple of placeholder functions, not included in produced code.
// These are here in order for hilapp to generate explicitly
// some Direction and CoordinateVector operations, which may not exist in
// original code as such.  It is easiest to let the general hilapp
// code generation to do it using this hack, instead of hard-coding these to
// hilapp.
//
// These are needed because hilapp changes X+d-d -> +d-d, which may involve
// an operator not met before

inline void dummy_X_f() {
    Direction d1 = e_x;
    CoordinateVector v1(0);
    onsites(ALL) {
        Direction d;
        d = +d1;
        d = -d1; // unaryops
        CoordinateVector vec;
        vec = +v1;
        vec = -v1;

        // Direction + vector combos
        vec = d + d1;
        vec = d - d1;
        vec = v1 + d1;
        vec = v1 - d1;
        vec = d1 + v1;
        vec = d1 - v1;
        vec = vec + v1;
        vec = vec - v1;

        // and Direction index func
        vec[e_x] = vec[0] + vec[e_y] + vec.e(e_y);
    }
}

/**
 * @internal
 * @brief  Dummy function including Field<T> functions and methods which
 * need to be explicitly seen by hilapp during 1st pass in order to
 * generater necessary functions.  Add here ops as needed
 */
template <typename T>
inline void ensure_field_operators_exist() {

    ensure_unary_minus_is_loop_function<T>();
    ensure_assign_zero_is_loop_function<T>();

    // make shift also explicit
    CoordinateVector v = 0;
    Field<T> f;
    f = f.shift(v, ALL);
}

#endif

#endif // FIELD_H
