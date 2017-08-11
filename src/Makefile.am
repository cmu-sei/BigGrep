## Process this file with automake to produce Makefile.in
## ------------------------------------------------------------------------
## Makefile.am (source)
## autotools build system for BigGrep
## ------------------------------------------------------------------------
## Copyright (C) 2015-2017 Carnegie Mellon University. All Rights Reserved.
## ------------------------------------------------------------------------
## Authors:
## ------------------------------------------------------------------------
## GNU General Public License (GPL) Rights pursuant to Version 2, June 1991
## Government Purpose License Rights (GPLR) pursuant to DFARS 252.227-7013
## ------------------------------------------------------------------------

bin_PROGRAMS = bgindex bgparse bgcount bgverify bgextractfile

#all: version

check_PROGRAMS = VarByteTest StrFormatTest bgdump

if C0XENABLE
check_PROGRAMS += PFORTest
endif

AM_CPPFLAGS = $(BOOST_CPPFLAGS)
AM_LDFLAGS = $(BOOST_THREAD_LDFLAGS) $(BOOST_SYSTEM_LDFLAGS) $(BOOST_DATE_TIME_LDFLAGS) $(BOOST_TIMER_LDFLAGS) $(BOOST_IOSTREAMS_LDFLAGS) $(BOOST_CHRONO_LDFLAGS)
LIBS = $(BOOST_THREAD_LIBS) $(BOOST_SYSTEM_LIBS) $(BOOST_DATE_TIME_LIBS) $(BOOST_TIMER_LIBS) $(BOOST_IOSTREAMS_LIBS) $(BOOST_CHRONO_LIBS)

bgindex_SOURCES = bgindex_th.cpp bgi_header.hpp BGLogging.hpp

bgcount_SOURCES = bgindex_th.cpp bgi_header.hpp BGLogging.hpp

bgparse_SOURCES = bgparse.cpp bgi_header.hpp BGLogging.hpp

bgverify_SOURCES = bgverify.cpp

bgdump_SOURCES = bgdump.cpp

bgextractfile_SOURCES = bgextractfile.cpp bgi_header.hpp BGLogging.hpp

#bgcount_SOURCES = bgcount.cpp bgi_header.hpp
#bgcount_LDFLAGS = @POCO_LDFLAGS@

#bgindex_th_via_maps_SOURCES = bgindex_th_via_maps.cpp
#bgindex_th_via_maps_LDFLAGS = @POCO_LDFLAGS@
#bgindex_th_via_maps_CFLAGS = @POCO_CFLAGS@

VarByteTest_SOURCES = VarByteTest.cpp VarByte.hpp

PFORTest_SOURCES = PFORTest.cpp PFOR.hpp
PFORTest_CPPFLAGS = --std=c++0x

StrFormatTest_SOURCES = StrFormatTest.cpp StrFormat.hpp

noinst_HEADERS = Logger.hpp BGAtomicCounter.hpp bgstopwatch.hpp

edit = sed -i -e "s/version=\"[0-9]\.[0-9]\"/version=\"$(PACKAGE_VERSION)\"/"

bgsearch.py: bgsearch.py
	${edit} "$<"



