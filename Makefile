# Copyright 2011-2016 Carnegie Mellon University.  See LICENSE file for terms.

prefix = /usr
exec_prefix = $(prefix)
bindir = $(exec_prefix)/usr/bin
etcdir = $(prefix)/etc
mandir = $(prefix)/share/man
man1dir = $(mandir)/man1
man5dir = $(mandir)/man5
version=$(shell grep -e '^Version: ' biggrep.spec | cut -f 2 -d ' ' | tr -d '[:space:]')
#release=`grep -e '^Release: ' biggrep.spec | cut -f 2 -d ' ' | tr -d [:space:]`
docdir = $(prefix)/share/doc/biggrep-$(version)
sys_py_site_packages = $(shell python -c "from distutils.sysconfig import get_python_lib; print(get_python_lib())")
py_site_packages = $(prefix)$(sys_py_site_packages)

# external dependency, a (statically compiled) POCO installation:
#POCO_HOME = $(HOME)/utils/poco-1.4.3p1/inst
# or, check for symlink in current dir
POCO_HOME = $(wildcard $(CURDIR)/poco)
ifneq "$(POCO_HOME)" ""
POCO_INCLUDES = -I$(POCO_HOME)/include
POCO_LIB_DIR = $(POCO_HOME)/lib
POCO_LDFLAGS = -L$(POCO_LIB_DIR)  -Xlinker '-R$(POCO_LIB_DIR)'
endif
# want static executables (which sometimes works and sometimes doesn't, POCO
# static object initialization can fail with a segv on some systems with
# statically linked execs but you can try it if you want):
#STATIC = -static
#POCO_STATIC = -Wl,-Bstatic
#POCO_END_STATIC = -Wl,-Bdynamic
# for additional debug info from POCO:
#D=d
POCO_LIBS = $(POCO_LDFLAGS) $(POCO_STATIC) -lPocoUtil$(D) -lPocoFoundation$(D) $(POCO_END_STATIC)
#POCO_LIBS = $(POCO_LIB_DIR)/libPocoUtil$(D).a $(POCO_LIB_DIR)/libPocoFoundation$(D).a
# sadly the _DEBUG macro needs to be defined to have the poco_debug logger
# macros be able to be toggled on at runtime:
POCO_DEBUG = -D_DEBUG
POCO_FLAGS = $(POCO_DEBUG) $(POCO_INCLUDES)

#JEMALLOC_HOME = $(CURDIR)/jemalloc
#JEMALLOC = jemalloc/lib/libjemalloc.a
#JEMALLOC = jemalloc/lib/libjemalloc.so -Xlinker '-Rjemalloc/lib'

#BOOST_INCLUDES = -I/usr/include/boost141/
#BOOST_LDFLAGS = -L/usr/lib64/boost141
# to override Boost location:
# if symlink in current directory, use that version
BOOST_HOME = $(wildcard $(CURDIR)/boost)
ifneq "$(BOOST_HOME)" ""
BOOST_INCLUDES = -I$(BOOST_HOME)/include
BOOST_LDFLAGS = -L$(BOOST_HOME)/lib
endif

# RedHat derivatives use -mt suffix for boost libs that are multithread safe
# but ubuntu doesn't (not sure about other systems, you may have to override)
# so let's try and detect:
REDHAT_RELEASE = $(wildcard /etc/redhat-release)
ifneq "$(REDHAT_RELEASE)" ""
BOOST_LIBS = -lboost_system-mt -lboost_thread-mt
else
BOOST_LIBS = -lboost_system -lboost_thread
endif
BOOST_FLAGS = $(BOOST_INCLUDES)

OTHER_LIBS = $(JEMALLOC)

OPT = -g -O3
#valgrind friendly:
#OPT = -g -O0
#OPT = -g -Wall -O2
#OPT = -g -Wall
#OPT = -g -pg -fprofile-arcs -fprofile-correction -O2

CFLAGS = $(OPT) -pthread -I. $(BOOST_FLAGS)
CXXFLAGS = $(CFLAGS)

LDFLAGS = -pthread $(STATIC) $(BOOST_LDFLAGS)

CC = gcc
CXX = g++

INSTALL = install

# if you want to use fpm to build rpm or deb packages:
FPM = fpm

MKDIR = mkdir -p
RMDIR = $(RM) -rf

.PHONY:	all tests clean doc rpm deb install python_unit_tests src

all:	VERSION bgindex bgparse bgverify doc

tests:	VarByteTest PFORTest StrFormatTest

python_unit_tests:
	python tests/test_bgsearch.py

bgindex: src/bgindex_th.cpp src/bgi_header.hpp
	$(CXX) $(CXXFLAGS) $(POCO_FLAGS) $< -o $@ $(LDFLAGS) $(BOOST_LIBS) $(POCO_LIBS) $(OTHER_LIBS)

bgparse:	src/bgparse.cpp src/bgi_header.hpp
	$(CXX) $(CXXFLAGS) $(POCO_FLAGS) $< -o $@ $(LDFLAGS) $(BOOST_LIBS) $(OTHER_LIBS)

bgverify:	src/bgverify.cpp
	$(CXX) $(CXXFLAGS) $< -o $@ $(LDFLAGS) $(BOOST_LIBS)

VarByteTest:	src/VarByteTest.cpp src/VarByte.hpp
	$(CXX) $(CXXFLAGS) -o $@ src/VarByteTest.cpp

PFORTest:	src/PFORTest.cpp src/PFOR.hpp
	$(CXX) $(CXXFLAGS) -o $@ src/PFORTest.cpp --std=c++0x

StrFormatTest:	src/StrFormatTest.cpp src/StrFormat.hpp
	$(CXX) $(CXXFLAGS) -o $@ src/StrFormatTest.cpp
doc:
	$(MAKE) -C doc
clean:
	$(RM) bgindex bgparse bgverify VarByteTest PFORTest StrFormatTest
	$(RM) *~ src/*~ .*~ core.* *.rpm *.deb
	$(MAKE) -C doc clean
	$(RM) -rf build dist rpminst
	$(RM) VERSION
build:
	mkdir -p build/{RPMS,SRPMS,BUILD,SOURCES,SPECS,tmp}
dist:
	mkdir dist
#rpm: VERSION build dist
rpm deb:	VERSION
	$(MKDIR) rpminst
	$(MAKE) prefix=`/bin/pwd`/rpminst install
	$(FPM) -s dir -t $@ -n bg -v $(version) -C ./rpminst -p bg_VERSION_ARCH.$@ -d 'python >= 2.6' etc share usr
	$(RMDIR) rpminst

src: biggrep-$(version).tar
biggrep-$(version).tar: ./README.md ./Makefile ./src/*.cpp ./src/*.hpp ./src/*.h ./src/*.py ./src/biggrep/*.py ./doc/Makefile ./doc/*.xml ./doc/*.txt ./doc/*.pdf ./LICENSE.txt ./GPL.txt ./biggrep.spec ./conf/biggrep/biggrep.conf ./python26_virtual.spec
	tar cvf $@ $^

install:	all
	$(INSTALL) -D bgindex $(DESTDIR)$(bindir)/bgindex
	$(INSTALL) -D bgparse $(DESTDIR)$(bindir)/bgparse
	$(INSTALL) -D bgverify $(DESTDIR)$(bindir)/bgverify
	$(INSTALL) -D src/bgsearch.py $(DESTDIR)$(bindir)/bgsearch
	$(INSTALL) -D src/bgsearch_old.py $(DESTDIR)$(bindir)/bgsearch_old.py
	$(INSTALL) -D src/bg-extract-or-replace-fileids.py $(DESTDIR)$(bindir)/bg-extract-or-replace-fileids.py
	# Ubuntu install cmd will work with wildcards, but RedHat won't, so
	# let's improvise:
	$(MKDIR) $(DESTDIR)$(man1dir)
	$(MKDIR) $(DESTDIR)$(man5dir)
	for f in doc/*.1.gz; do $(INSTALL) -D $$f -t $(DESTDIR)$(man1dir)/. ;done
	for f in doc/*.5.gz; do $(INSTALL) -D $$f -t $(DESTDIR)$(man5dir)/. ;done
	$(INSTALL) -D LICENSE.txt $(DESTDIR)$(docdir)/LICENSE.txt
	$(INSTALL) -D GPL.txt $(DESTDIR)$(docdir)/GPL.txt
	$(INSTALL) -D README.md $(DESTDIR)$(docdir)/README.md
	$(MKDIR) $(DESTDIR)$(docdir)
	for f in doc/*.pdf doc/*.txt; do $(INSTALL) -D $$f -t $(DESTDIR)$(docdir)/. ;done
	$(INSTALL) -D src/biggrep/__init__.py $(DESTDIR)$(py_site_packages)/biggrep/__init__.py
	$(INSTALL) -D src/biggrep/bgsearch.py $(DESTDIR)$(py_site_packages)/biggrep/bgsearch.py
	$(INSTALL) -D src/biggrep/bgsearch_jobmanager.py $(DESTDIR)$(py_site_packages)/biggrep/bgsearch_jobmanager.py
	$(INSTALL) -D src/biggrep/bgsearch_processor.py $(DESTDIR)$(py_site_packages)/biggrep/bgsearch_processor.py
	$(INSTALL) -D src/biggrep/bgverify_processor.py $(DESTDIR)$(py_site_packages)/biggrep/bgverify_processor.py
	$(MKDIR) $(DESTDIR)$(py_site_packages)/jobdispatch
	for f in src/jobdispatch/*.py; do $(INSTALL) -D $$f -t $(DESTDIR)$(py_site_packages)/jobdispatch/. ;done
	$(INSTALL) -D conf/biggrep/biggrep.conf $(DESTDIR)$(etcdir)/biggrep/biggrep.conf

VERSION:	biggrep.spec
	echo $(version) > $@
	sed -i -e "s/Version: [0-9]\.[0-9]/Version: $(version)/" src/bgparse.cpp
	sed -i -e "s/Version: [0-9]\.[0-9]/Version: $(version)/" src/bgverify.cpp
	sed -i -e "s/Version: [0-9]\.[0-9]/Version: $(version)/" src/bgindex_th.cpp
	sed -i -e "s/version=\"[0-9]\.[0-9]\"/version=\"$(version)\"/" src/bgsearch.py
	sed -i -e "s/<releaseinfo>[0-9]\.[0-9]<\/releaseinfo>/<releaseinfo>$(version)<\/releaseinfo>/" doc/bgindex.1.xml
	sed -i -e "s/<releaseinfo>[0-9]\.[0-9]<\/releaseinfo>/<releaseinfo>$(version)<\/releaseinfo>/" doc/bgparse.1.xml
	sed -i -e "s/<releaseinfo>[0-9]\.[0-9]<\/releaseinfo>/<releaseinfo>$(version)<\/releaseinfo>/" doc/bgsearch.1.xml
	sed -i -e "s/<releaseinfo>[0-9]\.[0-9]<\/releaseinfo>/<releaseinfo>$(version)<\/releaseinfo>/" doc/bgverify.1.xml
	sed -i -e "s/<releaseinfo>[0-9]\.[0-9]<\/releaseinfo>/<releaseinfo>$(version)<\/releaseinfo>/" doc/biggrep.conf.5.xml
