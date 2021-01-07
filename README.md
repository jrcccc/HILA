
# Description 

The Hila framework consists of 

1. the hilapp and 
2. a lattice simulation library.

The library is currently found in `programs/plumbing`.

Transformer contains a C++ preprocessing tool and framework for programming lattice field theory simulations: the main method for getting measurements from non-perturbative quantum field theories.  

Lattice field theory simulations involve up to 4 dimensional grids whose points are updated continuously according to some Monte Carlo update algorithm. The update at each grid point depends on the data stored at neighboring lattice points, and the ideal 
update order depends on the parity of the lattice points. Efficient parallel implementations using MPI and GPU's can be quite complicated to implement, modify and debug. Some kind of simplification was clearly needed. 

Transformer aims to make it easier for researchers to implement a broad class of these simulations by abstracting a lot of the technicalities involved, and by bringing the syntax from CUDA kernels and MPI calls to the essentials. The approach given 
here involves new datatypes and a preprocessing tool that converts c++ code with the new simplified syntax for loops and element accessors into working c++ code and gpu kernels. 

# Instructions

## Generating documentation

Build the documentation (with the git hash as the version number) using
~~~ bash
PROJECT_NUMBER=$(git rev-parse --short HEAD) doxygen
~~~

## Compiling the preprocessing tool and using it on c++ code

In short, the framework can be used in these steps: 

1. Write c++ code using the syntax and datatypes laid out below
2. Use the hilapp excecutable to convert this code into .cpt code 
3. Compile the new .cpt code into the final excecutable

![Workflow illustration](/docs/workflowV1.png)
 

To compile the hilapp, first create a build directory inside the main directory if it doesn't exist. 
Then, compile the hilapp by typing `make` in the main folder.
This will create an executable called `hilapp` in the build folder.

You can then use it to compile an extended C++ file into standard C++ using
~~~ bash
bin/hilapp path/to/program.cpp
~~~
This will create a `cpt` file written in standard C++.

The `cpt` can be compiled with any c++ compiler, but must be linked against the headers and c++ files in the plumbing directory.

Check the example programs in the programs folder. You can use almost any standard C++ code, by there are a couple of new reserved names: the variable `X` and the function `onsites()`. In addition the framework defines a global `lattice` variable, which you should not overwrite.

In order to use the additional features for field type variables, you should inlude `plumbing/field.h` in you program. You can also include one or more of the files in the `datatypes` folder, which contains predefined datatypes that can be used to construct a field.



### Compiling on Puhti

There is a separate makefile for compiling hilapp on Puhti.
To use it, run
~~~
module load gcc
make -f Makefile_puhti
~~~

This will link against the llvm installation in the hila development project folder.


## Using the Makefile system

Each of the example applications has a makefile for compiling the application with a
given target backend. To compile it, run
~~~ bash
make TARGET=target program_name
~~~
The lower case target should be replaced by one of 'vanilla', 'AVX' or 'CUDA'. This
will create a 'build' directory and compile the application there.

An application makefile should define any target files and include the main makefile.
Here is an example with comments:
~~~
# Give the location of the top level distribution directory wrt. this.
# Can be absolute or relative
TOP_DIR := ../..

# Add an application specific header to the dependencies
APP_HEADERS := application.h

# Read in the main makefile contents, incl. platforms
include $(TOP_DIR)/libraries/main.mk

# With multiple targets we want to use "make target", not "make build/target".
# This is needed to carry the dependencies to build-subdir
application: build/application ; @:

# Now the linking step for each target executable
build/application: Makefile build/application.o $(HILA_OBJECTS) $(HEADERS) 
	$(LD) -o $@ build/application.o $(HILA_OBJECTS) $(LDFLAGS) $(LDLIBS)
~~~


## Syntax - What works

### Single line statements

You can operate on fields using statements like
~~~ C++
my_field[ALL] = my_other_field[X] + my_third_field[X];
~~~
On the left-hand side of the statement you should specify
either `[ALL]` lattice sites, `[EVEN]` sites or `[ODD]` sites.
The statement will apply only to this collection of sites.
On the right hand side, use `[X]` to refer to this collection
of sites.

You can refer to neighbouring sites by adding a direction (`e_x`, `-e_x`, `e_y`, `-e_y`, `e_z`, `-e_z`, `e_t`, `-e_t`, ...):
~~~ C++
my_field[EVEN] = my_field[X+e_y];
~~~

You can also operate on fields directly,
~~~ C++
my_field = my_field + 1;
~~~
This will operate on all sites and is equivalent to 
~~~ C++
my_field[ALL] = my_field[X] + 1;
~~~


### General loops 
Loops over all sites or a parity:
~~~ C++
forsites(ALL){}
forsites(EVEN){}
forsites(ODD){}
~~~
Inside the loop, refer to the sites using X:
~~~ C++
forsites(ALL){
    my_field[X] = 1;
}
~~~

As before, you can refer to neighbouring sites by adding a direction:
~~~ C++
forsites(EVEN){
    my_field[X] = my_field[X+e_y];
}
~~~



## What doesn't work (as expected)

Random numbers in a vectorized loop return the same number for every entry in a vector.


## Testing

In the `programs/test_cases` folder you can find a collection of simple test programs. To test whether the translations work on the cpu, type:

~~~ bash
./test.sh 
~~~

This tests the transform, compilation and run process for the test_*.cpp files for dimensions from 1 to 4, and outputs the exit status of each step. 
If you're on a machine with GPU's, you can test the GPU transformations with:

~~~ bash
./test_GPU.sh
~~~

# Goals

 1. Write tests for existing and new features
     * Test also things that fail. The test basically defines how things should work.
 2. Extend both to support:
     * Fourier transform of field variable
     * If (or where) statements in a loop
     * Reduction on dimension (so f(t) = sum_x g(x,t))
     * Array-of-Struct-of-Arrays layout
     * MPI
 3. Implement OpenACC once the compiler version is updated
 4. Get rid of NDIM
 5. Extend field to allow lattice as input
 6. Document the library
