# BigGrep

BigGrep is a tool to index and search a large corpus of binary files that uses
a probabalistic N-gram based approach to balance index size and search speed.

## Quickstart

BigGrep requires Boost version 1.48 or later. Boost should be available
in most package management systems. Build and install Boost before 
building BigGrep.  To use the Boost lockfree queue, version 1.53 or greater
should be installed.  This may or may not give you a performance boost
when indexing. 

bgsearch requires jobdispatch, a python package that is included and installed
automatically with biggrep

```
git clone https://github.com/cmu-sei/BigGrep.git  
cd BigGrep  
./autogen.sh
./configure
make
make install
```

Now let's make a couple of test indexes out of some Windows EXE files:

```
mkdir /tmp/bgi  
ls -1 /some/test/files/*.exe | bgindex -p /tmp/bgi/testidx1 -v  
ls -1 /some/more/test/files/*.exe | bgindex -p /tmp/bgi/testidx2 -v  
```

And now that we have executables and test indexes, here's some sample search
usage with verification (in this case, searching for a typical function entry
point byte sequence, not overly interesting but shows how simple it is to look
for the existence of an abitrary byte sequence) using bgsearch:

```
bgsearch -d /tmp/bgi/ -v 8bff558bec  
bgsearch -d /tmp/bgi/ -v program  

```

## More Info

For additional details, see the original paper (also found in this repository
in the doc directory):

> Jin, W.; Hines, C.; Cohen, C.; Narasimhan, P., "A scalable search index for
> binary files," Malicious and Unwanted Software (MALWARE), 2012 7th
> International Conference on , vol., no., pp.94,103, 16-18 Oct. 2012  
> doi: 10.1109/MALWARE.2012.6461014  
> URL: http://ieeexplore.ieee.org/xpl/articleDetails.jsp?arnumber=6461014  

There is also a whitepaper in the doc directory of this repo that was done a
little later and describes more implementation decisions and some of our real
world usage details at the time (including some things that changed after the
paper for MALWARE 2012 was written based on things discovered during our daily
usage, such as switching to 3-grams for the indexes to save on disk space).

## Later Changes

More minor changes to the code and our usage have also occurred since that
whitepaper was written, such as:

  - The current "hints" list in the indexes was modified to get us within 16
    N-grams instead of 256 to help further reduce I/O at the expense of
    slightly increasing the index size (this is configurable at index
    generation time)
  - We now have gone to a mixed collection of 3-gram and 4-gram indexes to
    help eliminate some more I/O issues while continuing to balance disk space
    and speed.  Basically, for a small percentage of the files (just the ones
    that fill up a large portion of the 3-gram space, making them frequently
    appear as false positives in the candidate searches with 3-gram indexes)
    we generate 4-gram indexes and use 3-grams for the rest of them.
    By using the -O and -m options in bgindex, a file can be rejected from 
    a 3-gram index if it fills a large portion of the 3-gram space. By using
    the -O option, bgindex will write the file path to the file given to -O 
    and you can then generate 4-gram indexes for the denser files.
  - The current search code can also filter on metadata values to further trim
    down the candidate list before verification.  The man pages
     give details on that.

## Major Components

A quick overview of the various components (see the docs and source for more info):

  - bgindex: to create an index from a list of files
  - bgsearch: Python wrapper to search the indexes for the desired byte
    sequence(s) and optionally invoke the verification step and filter on
    metadata embedded in the indexes.  It tries to guess what you are
    searching for by inspecting the seach strings (if you don't explicitly
    tell it via a cmd line option): hex byte values or an ASCII string (which
    it converts to hex byte values to do the search).  Can use bgverify or
    Yara (with a supplied rule file) to do the verification.
  - bgparse: the executable bgsearch calls that actually reads the indexes to
    do the candidate list for the files with N-grams of the byte values.
  - bgverify: a Boyer-Moore-Horspool fast string search based verification
    tool to make sure the full strings of byte values exist in the candidate
    files found by bgparse.  Good for simple verifications, but as mentioned
    above you can also use Yara instead (see bgsearch docs).
  - bgextractfile: removes or replaces a file from an index.  This is useful
    if files have been purged or moved but you don't want to re-index.

## Building

A couple of minor notes about the code & building it:

  - If installing from the tarball, you should not have to run ./autogen.sh.
  ./configure; make; make install should configure, build, and install this
  package.
  - The code depends on Boost and Python (2.6 or 2.7, untested with
    3.x), and is known to build on various versions of RedHat, CentOS, 
    MAC OSX and Ubuntu Linux distros. 
  - By default, the biggrep and jobdispatch python packages are installed in the 
    site-packages directory returned by disutils.sysconfig get_python_lib(). You
    can change this behavior by using --with-python-prefix{=DIR} to install
    the Python modules under this prefix (location will be DIR/lib/python*/biggrep).
    If DIR is not provided, it will use the value of PREFIX (--prefix).  
  - If using a prefix when installing BigGrep, you may need to use
    --with-python-prefix to install the required Python modules.
  - If using --with-python-prefix you may need to set your PYTHONPATH environment
    variable to the location where the biggrep python module was installed in order
    to run bgsearch.
  - Boost 1.48 is required. On RHEL6 you may be able to find this package in
    the Software Collections Library, however, it may install in an alternative 
    path. Try the following when linking against the boost148 package:

    ```
    ./autogen.sh
    LDFLAGS=-L/usr/lib64/boost148 LIBS="-lboost_system-mt -lboost_chrono-mt" ./configure BOOST_ROOT=/usr/include
    make
    make install
    ```

## Problems?

Feel free to write an Issue for any problems you encounter.

Copyright 2011-2017 Carnegie Mellon University.  See LICENSE file for terms.

DM17-0473