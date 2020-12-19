#include <sstream>
#include <iostream>
#include <string>
#include <math.h>
#include <assert.h>
#include <sys/time.h>
#include <ctime>

#include "plumbing/defs.h"
#include "datatypes/matrix.h"
#include "plumbing/field.h"


// Minimum time to run each benchmark
// in microseconds
constexpr double mintime = 1000;



// Direct output to stdout
// std::ostream &hila::output = std::cout;


// Calculate time difference in milliseconds
static inline double timediff(timeval start, timeval end){
  long long t1 = (long long)(start.tv_usec) + 1000000*(long long)(start).tv_sec;
  long long t2 = (long long)(end.tv_usec) + 1000000*(long long)(end).tv_sec;
  return 1e-3*(double)(t2-t1);
}


#ifndef MSIZE
#define MSIZE 3
#endif

#ifndef SEED
#define SEED 100
#endif

#define MADD(x) (MSIZE + x)

const int latsize[4] = { 32, 32, 32, 32 };

///////////////////////////////////////
// benchmark conjugate operations for 
// increasing matrix sizes starting 
// from msize.  
///////////////////////////////////////

using ntype = float;

int main(int argc, char **argv){
    int n_runs=1;
    double msecs;
    struct timeval start, end;
    double timing;
    double sum;


    hila::initialize(argc, argv);

    lattice->setup(latsize);

    

    seed_random(SEED);

    timer timer1("Timer1");
    
    field<Matrix<MADD(0),MADD(0), cmplx<ntype>> > matrix1;
    field<Matrix<MADD(1),MADD(1), cmplx<ntype>> > matrix2;
    field<Matrix<MADD(3),MADD(3), cmplx<ntype>> > matrix3;
    field<Matrix<MADD(6),MADD(6), cmplx<ntype>> > matrix4;

    onsites(ALL){
      matrix1[X].random();
      matrix2[X].random();
      matrix3[X].random();
      matrix4[X].random();
    }

    // Time conj(matrix) * matrix * conj(matrix) 
    timing = 0;
    for(n_runs=1; timing < mintime; ){
      n_runs*=2;
      gettimeofday(&start, NULL);
      for( int i=0; i<n_runs; i++){
          matrix1[ALL] = conj(matrix1[X])*matrix1[X]*conj(matrix1[X]);
      }
      synchronize();
      gettimeofday(&end, NULL);
      timing = timediff(start, end);
    }
    timing = timing / (double)n_runs;
    output0 << "matrix size " << (int) MSIZE << "*"  << (int) MSIZE << " : "<< timing << " ms \n";

    // timer1.start();
    
    // Time conj(matrix) * matrix * conj(matrix) 
    timing = 0;
    for(n_runs=1; timing < mintime; ){
      n_runs*=2;
      gettimeofday(&start, NULL);
      for( int i=0; i<n_runs; i++){
          matrix2[ALL] = conj(matrix2[X])*matrix2[X]*conj(matrix2[X]);
      }
      synchronize();
      gettimeofday(&end, NULL);
      timing = timediff(start, end);
    }
    timing = timing / (double)n_runs;
    output0 << "matrix size " << (int) MSIZE + 1 << "*"  << (int) MSIZE + 1 << " : "<< timing << " ms \n";

    // timer1.end();
    
    // Time conj(matrix) * matrix * conj(matrix) 
    timing = 0;
    for(n_runs=1; timing < mintime; ){
      n_runs*=2;
      gettimeofday(&start, NULL);
      for( int i=0; i<n_runs; i++){
          matrix3[ALL] = conj(matrix3[X])*matrix3[X]*conj(matrix3[X]);
      }
      synchronize();
      gettimeofday(&end, NULL);
      timing = timediff(start, end);
    }
    timing = timing / (double)n_runs;
    output0 << "matrix size " << (int) MSIZE + 3  << "*"  << (int) MSIZE + 3  << " : "<< timing << " ms \n";

    timing = 0;
    for(n_runs=1; timing < mintime; ){
      n_runs*=2;
      gettimeofday(&start, NULL);
      for( int i=0; i<n_runs; i++){
          matrix4[ALL] = conj(matrix4[X])*matrix4[X]*conj(matrix4[X]);
      }
      synchronize();
      gettimeofday(&end, NULL);
      timing = timediff(start, end);
    }
    timing = timing / (double)n_runs;
    output0 << "matrix size " << (int) MSIZE + 6 << "*"  << (int) MSIZE + 6 << " : "<< timing << " ms \n";


    //------------------------------------------------

    // Time conj(matrix) * matrix * conj(matrix) 
    timing = 0;
    for(n_runs=1; timing < mintime; ){
      n_runs*=2;
      gettimeofday(&start, NULL);
      for( int i=0; i<n_runs; i++){
          matrix1[ALL] = matrix1[X].adjoint()*matrix1[X]*matrix1[X].adjoint();
      }
      synchronize();
      gettimeofday(&end, NULL);
      timing = timediff(start, end);
    }
    timing = timing / (double)n_runs;
    output0 << "matrix size " << (int) MSIZE << "*"  << (int) MSIZE << " : "<< timing << " ms \n";

    // Time matrix) * .adjoint()atrix * matrix) 
    timing = 0;
    for(n_runs=1; timing < mintime; ){
      n_runs*=2;
      gettimeofday(&start, NULL);
      for( int i=0; i<n_runs; i++){
          matrix2[ALL] = matrix2[X].adjoint()*matrix2[X]*matrix2[X].adjoint();
      }
      synchronize();
      gettimeofday(&end, NULL);
      timing = timediff(start, end);
    }
    timing = timing / (double)n_runs;
    output0 << "matrix size " << (int) MSIZE + 1 << "*"  << (int) MSIZE + 1 << " : "<< timing << " ms \n";

    // Time matrix) * .adjoint()atrix * matrix) 
    timing = 0;
    for(n_runs=1; timing < mintime; ){
      n_runs*=2;
      gettimeofday(&start, NULL);
      for( int i=0; i<n_runs; i++){
          matrix3[ALL] = matrix3[X].adjoint()*matrix3[X]*matrix3[X].adjoint();
      }
      synchronize();
      gettimeofday(&end, NULL);
      timing = timediff(start, end);
    }
    timing = timing / (double)n_runs;
    output0 << "matrix size " << (int) MSIZE + 3  << "*"  << (int) MSIZE + 3  << " : "<< timing << " ms \n";

    timing = 0;
    for(n_runs=1; timing < mintime; ){
      n_runs*=2;
      gettimeofday(&start, NULL);
      for( int i=0; i<n_runs; i++){
          matrix4[ALL] = matrix4[X].adjoint()*matrix4[X]*matrix4[X].adjoint();
      }
      synchronize();
      gettimeofday(&end, NULL);
      timing = timediff(start, end);
    }
    timing = timing / (double)n_runs;
    output0 << "matrix size " << (int) MSIZE + 6 << "*"  << (int) MSIZE + 6 << " : "<< timing << " ms \n";

    hila::finishrun();
}



