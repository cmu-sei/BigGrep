dnl Copyright (C) 2017 by Carnegie Mellon University.  
dnl
dnl See license information in ../LICENSE.txt



AC_DEFUN([BGCHECK_PYTHON],
[AM_PATH_PYTHON([2.6])

   AC_ARG_WITH([python-prefix],
        [AS_HELP_STRING([[--with-python-prefix[=DIR]]],
            [install Python modules under this prefix instead of in the Python site directory (e.g., biggrep location will \
be DIR/lib/python*/biggrep/).  An empty argument means to use the value of PREFIX])[]dnl                                      
        ],[
        if test "x$withval" = "xyes"
	then
            bg_PYTHONPREFIX='${prefix}'
        elif test "x$withval" != "xno"
        then
            bg_PYTHONPREFIX="$withval"
        fi
        ])

 AC_CACHE_CHECK([for $am_display_PYTHON install directory],
 [bg_cv_python_inc],
 [if test "x$bg_PYTHONPREFIX" != "x"
  then
     bg_cv_python_inc=`$PYTHON -c "from distutils.sysconfig import get_python_lib; print(get_python_lib(1,0,\"$bg_PYTHONPREFIX\"))" 2>/dev/null`
  else
     bg_cv_python_inc=`$PYTHON -c "from distutils.sysconfig import get_python_lib; print(get_python_lib())" 2>/dev/null`
  fi
  ])
AC_SUBST([pythondir], [$bg_cv_python_inc])
AC_SUBST([pkgpythondir], [$bg_cv_python_inc/$PACKAGE])])