
#include "../plumbing/defs.h"
#include "../datatypes/cmplx.h"
#include "../plumbing/field.h"

// extern field<int> glob;

cmplx<double> d(cmplx<double> x) {return x;}
cmplx<double> e(cmplx<double> x) {return d(x);}
cmplx<double> f(cmplx<double> x) {return e(x);}

using ft = cmplx<double>;


int main()
{
  
  field<cmplx<double>> a;
  field<double> t(1.0);

  onsites(ALL) {
    ft d(2,2);
    element<ft> t;
    t = a[X];
    a[X] = t;
  }
  
  return 0;
}

