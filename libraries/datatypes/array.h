#ifndef ARRAY_H_
#define ARRAY_H_

#include "datatypes/matrix.h"

////////////////////////////////////////////////////////////////
/// nxm Array type
////////////////////////////////////////////////////////////////
template <const int n, const int m, typename T>
class Array {
  public:
    static_assert(hila::is_complex_or_arithmetic<T>::value,
                  "Array requires Complex or arithmetic type");

    /// Same data as Matrix, a one dimensional array
    T c[n * m];

    /// std incantation for field types
    using base_type = hila::scalar_type<T>;
    using argument_type = T;

    /// define default constructors to ensure std::is_trivial
    Array() = default;
    ~Array() = default;
    Array(const Array<n, m, T> &v) = default;

    /// constructor from scalar - make this also explicit, consistency
    template <typename S, std::enable_if_t<hila::is_assignable<T &, S>::value, int> = 0>
    explicit inline Array(const S rhs) {
        for (int i = 0; i < n * m; i++) {
            this->c[i] = rhs;
        }
    }

    // and make non-explicit constructor from 0
#pragma hila loop_function
    inline Array(const std::nullptr_t &z) {
        for (int i = 0; i < n * m; i++)
            c[i] = static_cast<T>(0);
    }

    /// Construct array automatically from right-size initializer list
    /// This does not seem to be dangerous, so keep non-explicit

    template <typename S, std::enable_if_t<hila::is_assignable<T &, S>::value, int> = 0>
    inline Array(std::initializer_list<S> rhs) {
        assert(rhs.size() == n * m && "Array initializer list size must match variable size");
        int i = 0;
        for (auto it = rhs.begin(); it != rhs.end(); it++, i++) {
            c[i] = *it;
        }
    }

    /// Define constant methods rows(), columns() - may be useful in template code
    constexpr int rows() const {
        return n;
    }
    constexpr int columns() const {
        return m;
    }

    // define also method size() for 'vector arrays' and square arrays only!
    template <int q = n, int p = m, std::enable_if_t<q == 1, int> = 0>
    constexpr int size() const {
        return p;
    }

    template <int q = n, int p = m, std::enable_if_t<p == 1, int> = 0>
    constexpr int size() const {
        return q;
    }

    template <int q = n, int p = m, std::enable_if_t<q == p, int> = 0>
    constexpr int size() const {
        return q;
    }

    /// access operators .e(i,j) and .e(i) from Matrix
    inline T e(const int i, const int j) const {
        return c[i * m + j];
    }
    /// standard access ops m.e(i,j) - assume T is small, as it should
    inline T &e(const int i, const int j) const_function {
        return c[i * m + j];
    }

    /// declare single e here too in case we have a vector
    /// (one size == 1)
    template <int q = n, int p = m, std::enable_if_t<(q == 1 || p == 1), int> = 0>
    inline T e(const int i) const {
        return c[i];
    }

    template <int q = n, int p = m, std::enable_if_t<(q == 1 || p == 1), int> = 0>
    inline T &e(const int i) const_function {
        return c[i];
    }

    // cast from array to matrix
    Matrix<n, m, T> &asMatrix() const_function {
        return *reinterpret_cast<Matrix<n, m, T> *>(this);
    }

    const Matrix<n, m, T> &asMatrix() const {
        return *reinterpret_cast<const Matrix<n, m, T> *>(this);
    }

    // Define also asVector(), although asMatrix() already includes this
    Vector<n, T> &asVector() const_function {
        static_assert(1 == m, "asVector() only for column arrays");
        return *reinterpret_cast<Vector<n, T> *>(this);
    }

    const Vector<n, T> &asVector() const {
        static_assert(1 == m, "asVector() only for column arrays");
        return *reinterpret_cast<const Vector<n, T> *>(this);
    }


    /// casting from one Array (number) type to another
    /// TODO: CHECK AVX CONVERSIONS
    template <typename S, std::enable_if_t<hila::is_assignable<S &, T>::value, int> = 0>
    operator Array<n, m, S>() {
        Array<n, m, S> res;
        for (int i = 0; i < n * m; i++) {
            res.c[i] = c[i];
        }
        return res;
    }

    /// unary -
    inline Array<n, m, T> operator-() const {
        Array<n, m, T> res;
        for (int i = 0; i < n * m; i++) {
            res.c[i] = -c[i];
        }
        return res;
    }

    /// unary +
    inline Array<n, m, T> operator+() const {
        return *this;
    }

    /// Assign from scalar to array
#pragma hila loop_function
    template <typename S, std::enable_if_t<hila::is_assignable<T &, S>::value, int> = 0>
    inline Array<n, m, T> &operator=(const S rhs) {
        for (int i = 0; i < n * m; i++) {
            c[i] = rhs;
        }
        return *this;
    }

    /**
     * @brief Compare equality of arrays
     * 
     * Two arrays are equal iff arrays are of same dimension and all elements compare to equal
     * Note: Complex == scalar if arithmetic value is equal
     * 
     * @tparam S 
     * @param rhs 
     * @return true if equal
     */

    template <typename S,int n1, int m1>
    bool operator==(const Array<n1, m1, S> &rhs) const {
        if constexpr (n != n1 || m != m1) 
            return false;

        for (int i = 0; i < n; i++)
            for (int j = 0; j < m; j++) {
                if (e(i, j) != rhs.e(i, j))
                    return false;
            }
        return true;
    }

    /**
     * @brief Compare non-equality of two arrays
     * 
     * Negation of operator==()
     * 
     * @tparam S 
     * @tparam n1 
     * @tparam m1 
     * @param rhs 
     * @return true 
     * @return false 
     */

    template <typename S, int n1, int m1>
    bool operator!=(const Array<n1, m1, S> &rhs) const {
        return !(*this == rhs);
    }

    /// add assign an Array
#pragma hila loop_function
    template <typename S, std::enable_if_t<std::is_convertible<S, T>::value, int> = 0>
    Array<n, m, T> &operator+=(const Array<n, m, S> &rhs) {
        for (int i = 0; i < n * m; i++) {
            c[i] += rhs.c[i];
        }
        return *this;
    }

    /// subtract assign an Array
    template <typename S, std::enable_if_t<std::is_convertible<S, T>::value, int> = 0>
    Array<n, m, T> &operator-=(const Array<n, m, S> &rhs) {
        for (int i = 0; i < n * m; i++) {
            c[i] -= rhs.c[i];
        }
        return *this;
    }

    /// add assign type T and convertible
    template <typename S,
              std::enable_if_t<std::is_convertible<hila::type_plus<T, S>, T>::value, int> = 0>
    Array<n, m, T> &operator+=(const S rhs) {
        for (int i = 0; i < n * m; i++) {
            c[i] += rhs;
        }
        return *this;
    }

    /// subtract assign type T and convertible
    template <typename S,
              std::enable_if_t<std::is_convertible<hila::type_minus<T, S>, T>::value, int> = 0>
    Array<n, m, T> &operator-=(const S rhs) {
        for (int i = 0; i < n * m; i++) {
            c[i] -= rhs;
        }
        return *this;
    }

    /// multiply assign with Array
    template <typename S,
              std::enable_if_t<std::is_convertible<hila::type_mul<T, S>, T>::value, int> = 0>
    Array<n, m, T> &operator*=(const Array<n, m, S> &rhs) {
        for (int i = 0; i < n * m; i++) {
            c[i] *= rhs.c[i];
        }
        return *this;
    }

    /// multiply assign with scalar
    template <typename S,
              std::enable_if_t<std::is_convertible<hila::type_mul<T, S>, T>::value, int> = 0>
    Array<n, m, T> &operator*=(const S rhs) {
        for (int i = 0; i < n * m; i++) {
            c[i] *= rhs;
        }
        return *this;
    }

    /// divide assign by Array
    template <typename S,
              std::enable_if_t<std::is_convertible<hila::type_div<T, S>, T>::value, int> = 0>
    Array<n, m, T> &operator/=(const Array<n, m, S> &rhs) {
        for (int i = 0; i < n * m; i++) {
            c[i] /= rhs.c[i];
        }
        return *this;
    }

    /// divide assign with scalar
    template <typename S,
              std::enable_if_t<std::is_convertible<hila::type_div<T, S>, T>::value, int> = 0>
    Array<n, m, T> &operator/=(const S rhs) {
        for (int i = 0; i < n * m; i++) {
            c[i] /= rhs;
        }
        return *this;
    }

    /// complex conjugate
    inline Array<n, m, T> conj() const {
        Array<n, m, T> res;
        for (int i = 0; i < n * m; i++) {
            res.c[i] = ::conj(c[i]);
        }
        return res;
    }
    /// return real part
    inline Array<n, m, hila::scalar_type<T>> real() const {
        Array<n, m, hila::scalar_type<T>> res;
        for (int i = 0; i < m * n; i++) {
            res.c[i] = ::real(c[i]);
        }
        return res;
    }

    /// return imaginary part
    inline Array<n, m, hila::scalar_type<T>> imag() const {
        Array<n, m, hila::scalar_type<T>> res;
        for (int i = 0; i < m * n; i++) {
            res.c[i] = ::imag(c[i]);
        }
        return res;
    }

    /// calculate square norm - sum of squared elements
    hila::scalar_type<T> squarenorm() const {
        hila::scalar_type<T> result = 0;
        for (int i = 0; i < n * m; i++) {
            result += ::squarenorm(c[i]);
        }
        return result;
    }

    /// Generate random elements
    Array<n, m, T> &random() out_only {
        for (int i = 0; i < n * m; i++) {
            hila::random(c[i]);
        }
        return *this;
    }

    /// Generate gaussian random elements
    Array<n, m, T> &gaussian_random(double width = 1.0) out_only {
        for (int i = 0; i < n * m; i++) {
            hila::gaussian_random(c[i], width);
        }
        return *this;
    }

    /// Convert to string for printing
    std::string str() const {
        return this->asMatrix().str();
    }
};

/// conjugate
template <const int n, const int m, typename T>
inline Array<n, m, T> conj(const Array<n, m, T> &arg) {
    return arg.conj();
}
/// real part
template <const int n, const int m, typename T>
inline Array<n, m, hila::scalar_type<T>> real(const Array<n, m, T> &arg) {
    return arg.real();
}
/// imaginary part
template <const int n, const int m, typename T>
inline Array<n, m, hila::scalar_type<T>> imag(const Array<n, m, T> &arg) {
    return arg.imag();
}

/// Now Array additions: Array + Array
template <int n, int m, typename T>
inline Array<n, m, T> operator+(Array<n, m, T> a, const Array<n, m, T> &b) {
    a += b;
    return a;
}

/// Array subtract
template <int n, int m, typename T>
inline Array<n, m, T> operator-(Array<n, m, T> a, const Array<n, m, T> &b) {
    a -= b;
    return a;
}

/// Array + scalar
template <int n, int m, typename T, typename S,
          std::enable_if_t<std::is_convertible<hila::type_plus<T, S>, T>::value, int> = 0>
inline Array<n, m, T> operator+(Array<n, m, T> a, const S b) {
    a += b;
    return a;
}

/// scalar + Array
template <int n, int m, typename T, typename S,
          std::enable_if_t<std::is_convertible<hila::type_plus<T, S>, T>::value, int> = 0>
inline Array<n, m, T> operator+(const S b, Array<n, m, T> a) {
    a += b;
    return a;
}

/// Array - scalar
template <int n, int m, typename T, typename S,
          std::enable_if_t<std::is_convertible<hila::type_minus<T, S>, T>::value, int> = 0>
Array<n, m, T> operator-(Array<n, m, T> a, const S b) {
    a -= b;
    return a;
}

/// scalar - Array
template <int n, int m, typename T, typename S,
          std::enable_if_t<std::is_convertible<hila::type_minus<S, T>, T>::value, int> = 0>
inline Array<n, m, T> operator-(const S b, Array<n, m, T> a) {
    for (int i = 0; i < n * m; i++)
        a.c[i] = static_cast<T>(b) - a.c[i];
    return a;
}

/// and Array*Array
template <int n, int m, typename T>
inline Array<n, m, T> operator*(Array<n, m, T> a, const Array<n, m, T> &b) {
    a *= b;
    return a;
}

/// and Array/Array
template <int n, int m, typename T>
inline Array<n, m, T> operator/(Array<n, m, T> a, const Array<n, m, T> &b) {
    a /= b;
    return a;
}

/// Array * scalar
template <int n, int m, typename T, typename S,
          std::enable_if_t<std::is_convertible<hila::type_mul<T, S>, T>::value, int> = 0>
inline Array<n, m, T> operator*(Array<n, m, T> a, const S b) {
    a *= b;
    return a;
}

/// scalar * Array
template <int n, int m, typename T, typename S,
          std::enable_if_t<std::is_convertible<hila::type_mul<T, S>, T>::value, int> = 0>
inline Array<n, m, T> operator*(const S b, Array<n, m, T> a) {
    a *= b;
    return a;
}

/// Array / scalar
template <int n, int m, typename T, typename S,
          std::enable_if_t<std::is_convertible<hila::type_div<T, S>, T>::value, int> = 0>
inline Array<n, m, T> operator/(Array<n, m, T> a, const S b) {
    a /= b;
    return a;
}

/// scalar / Array
template <int n, int m, typename T, typename S,
          std::enable_if_t<std::is_convertible<hila::type_div<S, T>, T>::value, int> = 0>
inline Array<n, m, T> operator/(const S b, Array<n, m, T> a) {
    for (int i = 0; i < n * m; i++)
        a.c[i] = b / a.c[i];
    return a;
}

/// Stream operator
template <int n, int m, typename T>
std::ostream &operator<<(std::ostream &strm, const Array<n, m, T> &A) {
    return operator<<(strm, A.asMatrix());
}

namespace hila {

template <int n, int m, typename T>
std::string to_string(const Array<n, m, T> &A, int prec = 8, char separator = ' ') {
    return to_string(A.asMatrix(), prec, separator);
}

template <int n, int m, typename T>
std::string prettyprint(const Array<n, m, T> &A, int prec = 8) {
    return prettyprint(A.asMatrix(), prec);
}

} // namespace hila


/// Norm squared function
template <int n, int m, typename T>
inline hila::scalar_type<T> squarenorm(const Array<n, m, T> &rhs) {
    return rhs.squarenorm();
}


////////////////////////////////////////////////////////////////////////////////
/// Standard arithmetic functions - do element by element
////////////////////////////////////////////////////////////////////////////////

template <int n, int m, typename T>
inline Array<n, m, T> sqrt(Array<n, m, T> a) {
    for (int i = 0; i < n * m; i++)
        a.c[i] = sqrt(a.c[i]);
    return a;
}

template <int n, int m, typename T>
inline Array<n, m, T> cbrt(Array<n, m, T> a) {
    for (int i = 0; i < n * m; i++)
        a.c[i] = cbrt(a.c[i]);
    return a;
}

template <int n, int m, typename T>
inline Array<n, m, T> exp(Array<n, m, T> a) {
    for (int i = 0; i < n * m; i++)
        a.c[i] = exp(a.c[i]);
    return a;
}

template <int n, int m, typename T>
inline Array<n, m, T> log(Array<n, m, T> a) {
    for (int i = 0; i < n * m; i++)
        a.c[i] = log(a.c[i]);
    return a;
}

template <int n, int m, typename T>
inline Array<n, m, T> sin(Array<n, m, T> a) {
    for (int i = 0; i < n * m; i++)
        a.c[i] = sin(a.c[i]);
    return a;
}

template <int n, int m, typename T>
inline Array<n, m, T> cos(Array<n, m, T> a) {
    for (int i = 0; i < n * m; i++)
        a.c[i] = cos(a.c[i]);
    return a;
}

template <int n, int m, typename T>
inline Array<n, m, T> tan(Array<n, m, T> a) {
    for (int i = 0; i < n * m; i++)
        a.c[i] = tan(a.c[i]);
    return a;
}

template <int n, int m, typename T>
inline Array<n, m, T> asin(Array<n, m, T> a) {
    for (int i = 0; i < n * m; i++)
        a.c[i] = asin(a.c[i]);
    return a;
}

template <int n, int m, typename T>
inline Array<n, m, T> acos(Array<n, m, T> a) {
    for (int i = 0; i < n * m; i++)
        a.c[i] = acos(a.c[i]);
    return a;
}

template <int n, int m, typename T>
inline Array<n, m, T> atan(Array<n, m, T> a) {
    for (int i = 0; i < n * m; i++)
        a.c[i] = atan(a.c[i]);
    return a;
}

template <int n, int m, typename T>
inline Array<n, m, T> sinh(Array<n, m, T> a) {
    for (int i = 0; i < n * m; i++)
        a.c[i] = sinh(a.c[i]);
    return a;
}

template <int n, int m, typename T>
inline Array<n, m, T> cosh(Array<n, m, T> a) {
    for (int i = 0; i < n * m; i++)
        a.c[i] = cosh(a.c[i]);
    return a;
}

template <int n, int m, typename T>
inline Array<n, m, T> tanh(Array<n, m, T> a) {
    for (int i = 0; i < n * m; i++)
        a.c[i] = tanh(a.c[i]);
    return a;
}

template <int n, int m, typename T>
inline Array<n, m, T> asinh(Array<n, m, T> a) {
    for (int i = 0; i < n * m; i++)
        a.c[i] = asinh(a.c[i]);
    return a;
}

template <int n, int m, typename T>
inline Array<n, m, T> acosh(Array<n, m, T> a) {
    for (int i = 0; i < n * m; i++)
        a.c[i] = acosh(a.c[i]);
    return a;
}

template <int n, int m, typename T>
inline Array<n, m, T> atanh(Array<n, m, T> a) {
    for (int i = 0; i < n * m; i++)
        a.c[i] = atanh(a.c[i]);
    return a;
}

template <int n, int m, typename T>
inline Array<n, m, T> pow(Array<n, m, T> a, int b) {
    for (int i = 0; i < n * m; i++)
        a.c[i] = pow(a.c[i], b);
    return a;
}

template <int n, int m, typename T>
inline Array<n, m, T> pow(Array<n, m, T> a, T b) {
    for (int i = 0; i < n * m; i++)
        a.c[i] = pow(a.c[i], b);
    return a;
}

template <int n, int m, typename T>
inline Array<n, m, T> pow(Array<n, m, T> a, const Array<n, m, T> &b) {
    for (int i = 0; i < n * m; i++)
        a.c[i] = pow(a.c[i], b.c[i]);
    return a;
}

template <int n, int m, typename T, std::enable_if_t<hila::is_arithmetic<T>::value, int> = 0>
inline Array<n, m, T> round(Array<n, m, T> a) {
    for (int i = 0; i < n * m; i++)
        a.c[i] = round(a.c[i]);
    return a;
}

template <int n, int m, typename T, std::enable_if_t<hila::is_arithmetic<T>::value, int> = 0>
inline Array<n, m, T> floor(Array<n, m, T> a) {
    for (int i = 0; i < n * m; i++)
        a.c[i] = floor(a.c[i]);
    return a;
}

template <int n, int m, typename T, std::enable_if_t<hila::is_arithmetic<T>::value, int> = 0>
inline Array<n, m, T> ceil(Array<n, m, T> a) {
    for (int i = 0; i < n * m; i++)
        a.c[i] = ceil(a.c[i]);
    return a;
}

template <int n, int m, typename T, std::enable_if_t<hila::is_arithmetic<T>::value, int> = 0>
inline Array<n, m, T> trunc(Array<n, m, T> a) {
    for (int i = 0; i < n * m; i++)
        a.c[i] = trunc(a.c[i]);
    return a;
}


// Cast operators to different number or Complex type
// cast_to<double>(a);
// Cast from number->number, number->Complex, Complex->Complex OK,
//     Complex->number not.

template <typename Ntype, typename T, int n, int m,
          std::enable_if_t<hila::is_arithmetic<T>::value, int> = 0>
Array<n, m, Ntype> cast_to(const Array<n, m, T> &mat) {
    Array<n, m, Ntype> res;
    for (int i = 0; i < n * m; i++)
        res.c[i] = mat.c[i];
    return res;
}

template <typename Ntype, typename T, int n, int m,
          std::enable_if_t<hila::is_complex<T>::value, int> = 0>
Array<n, m, Ntype> cast_to(const Array<n, m, T> &mat) {
    Array<n, m, Ntype> res;
    for (int i = 0; i < n * m; i++)
        res.c[i] = cast_to<Ntype>(mat.c[i]);
    return res;
}


/// Array1d and Array2d are just aliased to Array
template <int n, typename T = double>
using Array1d = Array<n, 1, T>;

template <int n, int m, typename T = double>
using Array2d = Array<n, m, T>;

#endif