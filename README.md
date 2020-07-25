pSAscan - Parallel external memory suffix array construction
============================================================


Description
-----------

pSAscan is an implementation of a parallel external-memory suffix
array construction algorithm. The algorithm was described in the paper

    @inproceedings{kkp15cpm,
      author =    {Juha K{\"{a}}rkk{\"{a}}inen and Dominik Kempa
                   and Simon J. Puglisi},
      title =     {Parallel External Memory Suffix Sorting},
      booktitle = {26th Annual Symposium on Combinatorial Pattern
                   Matching (CPM 2015)},
      pages     = {329--342},
      year      = {2015},
      doi       = {10.1007/978-3-319-19929-0\_28},
    }

The latest version of pSAscan is available from
https://github.com/dkempa/psascan.



Compilation and usage
---------------------

1. Download https://github.com/y-256/libdivsufsort/archive/2.0.1.tar.gz
and install. Make sure to compile libdivsufsort to static 64-bit
libraries, i.e., change the values of the following options in the
main `CMakeLists.txt` from default values to

    ```
    option(BUILD_SHARED_LIBS "Set to OFF to build static libraries" OFF)
    option(BUILD_DIVSUFSORT64 "Build libdivsufsort64" ON)
    ```

When installing libdivsufsort, pay attention to install them so that
they are visible during compilation of pSAscan. We recommend installing
libdivsufsort in the home directory, and adding

    ```
    export CPLUS_INCLUDE_PATH=$CPLUS_INLUDE_PATH:~/include
    export LIBRARY_PATH=$LIBRARY_PATH:~/lib
    ```

to the .bashrc file (remember to `source .bashrc` to apply the update)
in the home directory.

2. The package contains a single Makefile in the main directory.
Type `make` to build the executable. For usage instructions, run the
program without any arguments.

### Example

The simplest usage of pSAscan is as follows. Suppose the text is located
in `/data/input.txt`. Then, to compute the suffix array of `input.txt`
using 8GiB of RAM, type:

    $ ./psascan /data/input.txt -m 8192

By default, the resulting suffix array is written to a file matching
the filename of the input text with the .sa5 extension
(`/data/input.txt.sa5` in this case). To write the suffix array to a
different file, use the -o flag, e.g.,

    $ ./psascan /data/input.txt -m 8192 -o /data2/sa.out

The current implementation encodes the output suffix array using
unsigned 40-bit integers. For further processing of the suffix array,
one should use the same or compatible encoding. The class implementing
the unsigned 40-bit integers is located in the
`src/psascan_src/uint40.h` file.



Disk space requirements
-----------------------

To compute the suffix array of an n-byte input text, pSAscan needs
about 7.5n bytes of disk space. This includes the input (n bytes) and
output (5n bytes). In the default mode, the pSAscan assumes, that
there is 6.5n bytes of free disk space available in the location used
as the destination for the suffix array. This space is used for
auxiliary files created during the computation and to accommodate the
output.

The above disk space requirement may in some cases prohibit the use of
algorithm, e.g., if there is enough space (5n) on one physical disk to
hold the suffix array, but not enough (6.5n) to run the algorithm. To
still allow the computation in such cases, the `psascan` program
implements the -g flag. With this flag, one can force pSAscan to use
disk space from two physically different locations (e.g., on two
disks). More precisely, out of 6.5n bytes of disk space used by
pSAscan, about n bytes is used to store the so-called "gap array". By
default, the gap array is stored along with the suffix array. The -g
flag allows explicitly specifying the location of the gap array. This
way, it suffices that there is only 5.5n bytes of disk space in the
location specified as the destination of the suffix array. The
remaining n bytes can be allocated in other location specified with
the -g flag.

### Example

Assume the location of input/output files and RAM usage as in the
example from the previous section. To additionally specify the
location of the gap array as `/data3/tmp` run the `psascan`
command as:

    $ ./psascan /data/input.txt -m 8192 -o /data2/sa.out -g /data3/tmp



RAM requirements
----------------

The algorithm does not have a fixed memory requirements. In principle,
it can run with any amount of RAM (though there is some minimal
per-thread amount necessary in the streaming phase). However, since
the time complexity (without logarithmic factors) of the algorithm is
O(n^2 / M), where M is the amount of RAM used in the computation,
using more RAM decreases the runtime.  Thus, the best performance is
achieved when nearly all unused RAM available in the system (as shown
by the Linux `free` command) is used for the computation. Leaving
about 5% (but not more than 2GiB) of RAM free is advised to prevent
thrashing.

### Example

On a machine with 12 physical cores and Hyper-Threading (and thus
capable of simultaneously running 24 threads) it takes about a week to
compute a suffix array of a 200GiB file using 3.5GiB of RAM. Using
120GiB of RAM reduces the time to less than 12 hours.



Troubleshooting
---------------

1. I am getting an error about the exceeded number of opened files.

Solution: The error is caused by the operating system imposing a limit
on the maximum number of files opened by a program. The limit can be
increased with the `ulimit -n newlimit` command. However, in Linux the
limit cannot be increased beyond the so-called "hard limit", which is
usually only few times larger. Furthermore, this is a temporary
solution that needs to repeated every time a new session is
started. To increase the limits permanently, edit (as a root) the file
`/etc/security/limits.conf` and add the following lines at the end
(including the asterisks):

    * soft nofile 128000
    * hard nofile 128000

This increases the limit to 128000 (use larger values if necessary).
The new limits apply (check with `ulimit -n`) after starting new
session.

2. Program stops without any error message.

Solution: Most likely the problem occurred during internal-memory
sorting.  Re-running the program with the -v flag should show the
error message.



Limitations
-----------

1. The maximum size of input text is 1TiB (2^40 bytes).
2. The current implementation supports only inputs over byte alphabet.
3. Only texts not containing bytes with value 255 are handled
   correctly.  The 255-bytes can be removed from the input text using
   the tool located in the directory tools/delete-bytes-255/ of this
   package.
4. The current internal-memory suffix sorting algorithm used
   internally in pSAscan works only if the input text is split into
   segments of size at most 2GiB each. Therefore, pSAscan will fail,
   if the memory budget X for the computation (specified with the -m
   flag) satisfies X / p > 10 * 2^31, where p is the number of threads
   used during the computation. On most systems, this is not a severe
   limitation, e.g., for a regular 4-core machine supporting
   Hyper-Threading (and thus capable of simultaneously running 8
   threads), pSAscan can utilize up to 160GiB of RAM.

The above limitations (except possibly 2) are not inherent to the
algorithm but rather the current implementation. Future releases will
most likely overcome these limitations.



Third-party code
----------------

The pSAscan implementation makes use of some third-party code:
- The internal suffix-sorting routine is divsufsort 2.0.1.
  See: https://github.com/y-256/libdivsufsort



Terms of use
------------

pSAscan is released under the MIT/X11 license. See the file LICENCE
for more details. If you use this code, please cite the paper
mentioned above.



Authors
-------

pSAscan was implemented by:
- [Dominik Kempa](https://scholar.google.com/citations?user=r0Kn9IUAAAAJ)
- [Juha Karkkainen](https://scholar.google.com/citations?user=oZepo1cAAAAJ)
