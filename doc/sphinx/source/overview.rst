:tocdepth: 2

.. highlight:: c

Overview
========

This section explains how to install Yices, and how to compile and
link your code with the Yices library.

Installation
------------

Yices 2 can be downloaded at http://yices.csl.sri.com. You can either get a source
distribution, or a binary distribution for Linux, Mac OS X, or Windows.

Installing from the Source
..........................

Compiling Yices from the source requires the `GNU Multiple
Precision <http://gmplib.org>`_ library (GMP) and the `gperf
<http://www.gnu.org/software/gperf>`_ utility.
Assuming you have both, then building and installing
Yices is straightforward:

.. code-block:: sh

   ./configure
   make -j
   sudo make install

This installs the binaries in :file:`/usr/local/bin`, the header files
in :file:`/usr/local/include`, and the library in
:file:`/usr/local/lib`. You can change the installation location by
giving the option ``--prefix=<directory>`` to the
``configure`` script.

For a detailed explanation of the build process and options, check the
file :file:`doc/COMPILING` included in the distribution.


MCSAT and Nonlinear Arithmetic
..............................

Yices now includes a solver for nonlinear arithmetic based on the
Model Constructing Satisfiability Calculus (MCSAT). This solver
depends on an external library for manipulating polynomials. If you
need nonlinear arithmetic and want to compile Yices from the source,
you must install this library first. Get it from our `GitHub
repository <https://github.com/SRI-CSL/libpoly>`_ and follow the build
instructions there.  Make sure to get the latest libpoly release
(v0.1.3).

Once you have installed libpoly, you can compile Yices with MCSAT
support as follows:

.. code-block:: sh

   ./configure --enable-mcsat
   make -j
   sudo make install

You may need to set CPPFLAGS and LDFLAGS if the libpoly library is not
in a standard location.



Binary Distribution
...................

The binary distributions contain pre-compiled binaries and
libraries. These distributions are self-contained. The binaries and
libraries are linked statically against GMP and libpoly. They include
support for nonlinear arithmetic and MCSAT.

The binary distributions for Linux and Mac OS X include a shell script
to install the binaries, headers, and library in
:file:`/usr/local`. You can run this script as follows:

.. code-block:: sh

   sudo ./install-yices

If you want a different installation directory, type

.. code-block:: sh

   ./install-yices <directory>

(use *sudo* if necessary).


Headers and Compilation
-----------------------

The Yices API is defined in three header files:

 - :file:`yices.h` declares all functions and constants

 - :file:`yices_types.h` defines the types and data structures used in the API

 - :file:`yices_limits.h` defines hard-coded limits

For a standard installation, these files are in directory :file:`/usr/local/include`.

To use the API, you should add the following line to your code::

  #include <yices.h>

and link with the Yices library using option ``-lyices``.

Several functions in the API take GMP numbers (e.g., ``mpq_t`` or
``mpz_t``) as arguments. To use these functions, make sure to include
the GMP header *before* you include ``yices.h`` as in::

  #include <gmp.h>
  #include <yices.h>

.. note:: Yices requires the C99 header ``stdint.h``.
   This header may not be available on old versions of Microsoft's Visual
   Studio. If it is missing, open-source versions of ``stdint.h`` can be 
   downloaded at

   - https://code.google.com/p/msinttypes (for Visual Studio only)
   - http://www.azillionmonkeys.com/qed/pstdint.h

   A copy of the latter file is included in the Yices distributions (in
   :file:`etc/pstdint.h`).


Minimal Example
---------------

Here is a minimal example::

   #include <stdio.h>
   #include <yices.h>

   int main(void) {
      printf("Testing Yices %s (%s, %s)\n", yices_version,
              yices_build_arch, yices_build_mode);
      return 0;
   }

Assuming that Yices is installed in the standard location, this example
should compile with:

.. code-block:: sh

  gcc minimal.c -o minimal -lyices

Other compilers than GCC can be used. If Yices is installed in a different
location, give appropriate flags to the compilation command. For example:

.. code-block:: sh

  gcc -I${HOME}/yices-2.3.1/include -L${HOME}/yices-2.3.1/lib \
     minimal.c -o minimal -lyices

Running the program should print something like this:

.. code-block:: none

  Testing Yices 2.3.1 (x86_64-unknown-linux-gnu, release)

You may need to play with environment variable ``LD_LIBRARY_PATH`` (or
``DYLD_LIBRARY_PATH`` on Mac OS X) if the runtime Yices library is not
found.

