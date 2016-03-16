# BigGrep

BigGrep is a tool to index and search a large corpus of binary files that uses
a probabalistic N-gram based approach to balance index size and search speed.

## Quickstart

Here's a quick & dirty intro to building, assuming you have Boost installed on
your system (any version >= 1.41 is probably fine, and it should be available
in most package management systems) something like this should work (if your
system has a recent Poco in package form you could install that too instead of
building it like I show here):

```
mkdir build  
cd build  
wget http://pocoproject.org/releases/poco-1.6.1/poco-1.6.1.tar.gz  
tar xvf poco-1.6.1.tar.gz  
cd poco-1.6.1  
./configure --prefix=`/bin/pwd`/inst --static --shared  
make -j8 && make install  
cd ..  
git clone https://github.com/cmu-sei/BigGrep.git  
cd BigGrep  
ln -s ../poco-1.6.1/inst ./poco  
make prefix=`/bin/pwd`/inst -j8 install  
```

Note that the above worked for me on my Ubuntu 15.10 development workstation
and in a CentOS 7.2 VM.

Now let's make a couple of test indexes out of some Windows EXE files:

```
mkdir /tmp/bgi  
ls -1 /some/test/files/*.exe | ./inst/bin/bgindex -p /tmp/bgi/testidx1 -v  
ls -1 /some/more/test/files/*.exe | ./inst/bin/bgindex -p /tmp/bgi/testidx2 -v  
```

And now that we have executables and test indexes, here's some sample search
usage with verification (in this case, searching for a typical function entry
point byte sequence, not overly intersting but shows how simple it is to look
for the existance of an abitrary byte sequence) using the two different
included bgsearch python scripts:

```
./inst/bin/bgsearch_old.py -d /tmp/bgi/ -v 8bff558bec  
./inst/bin/bgsearch_old.py -d /tmp/bgi/ -v program  
env PYTHONUSERBASE=`/bin/pwd`/inst/usr PATH=$PATH:`/bin/pwd`/inst/bin ./inst/bin/bgsearch -d /tmp/bgi/ -v 8bff558bec  
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
  - The current search code can also filter on metadata values to further trim
    down the candidate list before verification.  The man pages (Docbook
    format in repo) give details on that.

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
  - bgsearch_old.py: older standalone multiprocessing based version of the
    search wrapper code, a little simpler starting point, same functionality
    (although it can only use bgverify for verification).
  - bgparse: the executable bgsearch calls that actually reads the indexes to
    do the candidate list for the files with N-grams of the byte values.
  - bgverify: a Boyer-Moore-Horspool fast string search based verification
    tool to make sure the full strings of byte values exist in the candidate
    files found by bgparse.  Good for simple verifications, but as mentioned
    above you can also use Yara instead (see bgsearch docs).

## Building

A couple of minor notes about the code & building it:

  - The code depends on Boost, POCO, and Python (2.6 or 2.7, untested with
    3.x), and is known to build on various versions of RedHat, CentOS, and
    Ubuntu Linux distros.  Docbook is needed to build the man pages.  You'll
    probably need to hand edit the Makefile (and possibly some Python code,
    and maybe the spec files if you are trying to build an RPM) for your setup
    until we get around to making the build process and code a little more
    generic & robust...  The Makefile defaults to looking for symlinks to
    install dirs for POCO and Boost in the current directory by default, but
    it should build against system installed versions fine.  The Boost
    libraries on some distros have '-mt' suffixes and don't on others, so
    BOOST_MT may have to be adjusted to account for that.  The Makefile is old
    and ugly and definitely needs some TLC, sorry.
  - Static executables can be built (see the STATIC and POCO_STATIC macros in
    the Makefile), but we encountered issues with POCO and newer versions of
    GCC and Linux distros where a coredump would occur during C++ static
    object initialization, so YMMV there (seems to work in Ubuntu flavors but
    not in RedHat flavored past RHEL5).  Note that the POCO library is only
    used for the indexing code, and will probably be replaced eventually
    (likely with Boost and/or standard C++ constructs).
  - There are 2 python packages included in here that support the bgsearch.py
    script: biggrep and jobdispatch.  The latter is part of another in house
    package, stripped down to just what is needed for this script.
    Installation of them needs to be done manually for the time being (likely
    just a symlink into ~/.local/lib/python2*/site-packages/ should suffice,
    or set PYTHONUSERBASE like in the example in the quickstart section
    above).


## Problems?

Feel free to write an Issue for any problems you encounter.


Copyright 2011-2016 Carnegie Mellon University.  See LICENSE file for terms.

DM-0001480
