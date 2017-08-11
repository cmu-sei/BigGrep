dnl Process this file with autoconf to produce a configure script
dnl ------------------------------------------------------------------------
dnl configure.ac
dnl ------------------------------------------------------------------------
dnl Copyright (C) 2011-2017 Carnegie Mellon University. All Rights Reserved.
dnl ------------------------------------------------------------------------
dnl Authors: CERT <netsa-help@cert.org>
dnl ------------------------------------------------------------------------
dnl GNU General Public License (GPL) Rights pursuant to Version 2, June 1991
dnl Government Purpose License Rights (GPLR) pursuant to DFARS 252.227-7013
dnl ------------------------------------------------------------------------


AC_PREREQ(2.59)
AC_INIT([biggrep], [2.7], [https://github.com/cmu-sei/BigGrep/issues],[biggrep])
AC_CONFIG_SRCDIR([.])
AC_CONFIG_HEADERS(src/bgconfig.h)
AC_COPYRIGHT([Copyright (C) 2011-2017 by Carnegie Mellon University
Government Purpose License Rights (GPLR) pursuant to DFARS 252.227-7013])
AC_REVISION([$Id:  $])
AC_CONFIG_AUX_DIR([autoconf])
AC_CONFIG_MACRO_DIR([m4])
AM_INIT_AUTOMAKE([1.9 foreign check-news subdir-objects])

AC_PROG_CXX
AC_PROG_INSTALL
AC_PROG_MAKE_SET
AC_HEADER_STDC
AC_LANG_C

AC_SUBST(AM_CPPFLAGS)
AC_SUBST([AM_CXXFLAGS])
AC_SUBST(AM_CFLAGS)
AC_SUBST(AM_LDFLAGS)
LT_INIT

#AM_PATH_PYTHON([2.6])

AC_LANG([C++])

RPM_CONFIG_FLAGS=""
CPPFLAGS='-I$(top_builddir)/include -I$(top_srcdir)/include'" $CPPFLAGS"
AC_HEADER_STDC
AC_CHECK_HEADERS([string])
AC_CHECK_HEADERS([iostream])

AC_CHECK_HEADERS([pthread.h machine/endian.h endian.h stdint.h unistd.h errno.h stdlib.h string.h limits.h fcntl.h malloc.h getopt.h sys/stat.h sys/mman.h sys/time.h sys/resource.h sys/types.h])

dnl ----------------------------------------------------------------------
dnl Set up pod2man
dnl ----------------------------------------------------------------------

AC_CHECK_PROGS(POD2MAN, [pod2man true])

BG_POD2MAN_ARGS="--center='biggrep' --release='"${PACKAGE_VERSION}"' --date='"`date +"%v"`"'"

AC_SUBST(BG_POD2MAN_ARGS)

dnl ------------------------------------------------------------------
dnl Check for baseline language coverage in the compiler for C++0x
dnl ------------------------------------------------------------------
AX_CXX_COMPILE_STDCXX_0X

dnl --------------------------------------------------------------------
dnl Check for python and appropriate install locations
dnl -------------------------------------------------------------------
BGCHECK_PYTHON

dnl ----------------------------------------------------------------------
dnl Check for libPoco
dnl ----------------------------------------------------------------------
#AX_LIB_POCO([1.4])

dnl ---------------------------------------------------------------------
dnl Check for [de]compression support
dnl ---------------------------------------------------------------------
AX_CHECK_LIBZ

dnl ----------------------------------------------------------------------
dnl Check for Boost
dnl ----------------------------------------------------------------------
BOOST_REQUIRE([1.48])
BOOST_SYSTEM
BOOST_THREAD
#BOOST_LOG
#BOOST_LOG_SETUP
BOOST_IOSTREAMS
BOOST_DATE_TIME
BOOST_CHRONO
BOOST_TIMER

if test $BG_BOOST_VERSION -ge 153; then
   AC_DEFINE(BG_BOOST_LOCKLESS, 1, [Define to 1 if Boost version 1.53 or greater is installed and lockfree queues are available])
fi


AC_SUBST(RPM_CONFIG_FLAGS)

AC_SUBST([abs_top_srcdir])
AC_SUBST([abs_top_builddir])


AC_CONFIG_FILES([
    Makefile
    src/Makefile
    doc/Makefile
    biggrep.spec
])

dnl ----------------------------------------------------------------------
dnl Do output
dnl ----------------------------------------------------------------------

AC_OUTPUT
