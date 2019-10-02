#include <iostream>
#include <string>
#include <math.h>

// Include the lattice field definition
#include "../plumbing/field.h"
#include "../datatypes/scalar.h"

// Direct output to stdout
std::ostream &hila::output = std::cout;
std::ostream &output = std::cout;

// Define the lattice global variable
lattice_struct my_lattice;
lattice_struct * lattice = & my_lattice;


double beta = 0.5;
int n_measurements=100;
int n_updates_per_measurement=10;

int main()
{
  // Basic setup
  lattice->setup( 8, 8 );

  // Define a field
  field<scalar<double>> spin;
  
  // Set to 1
  spin[ALL] = 1;

  parity p = EVEN;

  // Just a test, calculate magnetisation
  double M=0;
  onsites(ALL){
    M += spin[X];
  }

  printf("Magnetisation %f\n", M);
  
  return 0;
}
