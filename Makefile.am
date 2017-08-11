## Process this file with automake to produce Makefile.in
## ------------------------------------------------------------------------
## Makefile.am (toplevel)
## autotools build system for BigGrep
## ------------------------------------------------------------------------
## Copyright (C) 2015-2017 Carnegie Mellon University. All Rights Reserved.
## ------------------------------------------------------------------------
## Authors:
## ------------------------------------------------------------------------
## GNU General Public License (GPL) Rights pursuant to Version 2, June 1991
## Government Purpose License Rights (GPLR) pursuant to DFARS 252.227-7013
## ------------------------------------------------------------------------


AUTOMAKE_OPTIONS = subdir-objects

ACLOCAL_AMFLAGS = -I m4

dist_sysconf_DATA = conf/biggrep/biggrep.conf

# Avoid picking up MacOS X resource forks in "make dist"
am__tar = COPY_EXTENDED_ATTRIBUTES_DISABLE=1 COPYFILE_DISABLE=1 ${AMTAR} cof - "$$tardir"

MOSTLYCLEANFILES = $(DX_CLEANFILES)

SUBDIRS = src doc

EXTRA_DIST = \
    README.txt \
    README.md \
    autogen.sh \
    LICENSE.txt \
    doc/INDEX_DETAILS.txt \
    biggrep.spec \
    biggrep.spec.in \
    src/bgsearch.py \
    src/biggrep/__init__.py \
    src/biggrep/bgsearch.py \
    src/biggrep/bgsearch_processor.py \
    src/biggrep/bgsearch_jobmanager.py \
    src/biggrep/bgverify_processor.py \
    src/jobdispatch/__init__.py \
    src/jobdispatch/jobdispatch.py \
    src/jobdispatch/jobmanager.py \
    src/jobdispatch/mockjobmanager.py \
    src/jobdispatch/processor.py 

install-exec-local:
	$(INSTALL) $(srcdir)/src/bgsearch.py                     -D $(DESTDIR)$(bindir)/bgsearch
	$(INSTALL) $(srcdir)/src/jobdispatch/jobdispatch.py      -D $(DESTDIR)$(pythondir)/jobdispatch/jobdispatch.py
	$(INSTALL) $(srcdir)/src/jobdispatch/jobmanager.py       -D $(DESTDIR)$(pythondir)/jobdispatch/jobmanager.py
	$(INSTALL) $(srcdir)/src/jobdispatch/mockjobmanager.py   -D $(DESTDIR)$(pythondir)/jobdispatch/mockjobmanager.py
	$(INSTALL) $(srcdir)/src/jobdispatch/processor.py        -D $(DESTDIR)$(pythondir)/jobdispatch/processor.py
	$(INSTALL) $(srcdir)/src/jobdispatch/__init__.py         -D $(DESTDIR)$(pythondir)/jobdispatch/__init__.py
	$(INSTALL) $(srcdir)/src/biggrep/__init__.py             -D $(DESTDIR)$(pkgpythondir)/__init__.py
	$(INSTALL) $(srcdir)/src/biggrep/bgsearch.py             -D $(DESTDIR)$(pkgpythondir)/bgsearch.py
	$(INSTALL) $(srcdir)/src/biggrep/bgsearch_processor.py   -D $(DESTDIR)$(pkgpythondir)/bgsearch_processor.py
	$(INSTALL) $(srcdir)/src/biggrep/bgsearch_jobmanager.py  -D $(DESTDIR)$(pkgpythondir)/bgsearch_jobmanager.py
	$(INSTALL) $(srcdir)/src/biggrep/bgverify_processor.py     -D $(DESTDIR)$(pkgpythondir)/bgverify_processor.py


uninstall-local:
	rm -f $(DESTDIR)$(bindir)/bgsearch
	rm -rf $(DESTDIR)$(pythondir)/jobdispatch
	rm -rf $(DESTDIR)$(pkgpythondir)


