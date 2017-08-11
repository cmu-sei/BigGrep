dnl Copyright (C) 2004-2015 by Carnegie Mellon University.
dnl
dnl @OPENSOURCE_HEADER_START@
dnl
dnl Use of the BigGrep system and related source code is subject to the terms
dnl of the following licenses:
dnl
dnl GNU Public License (GPL) Rights pursuant to Version 2, June 1991
dnl Government Purpose License Rights (GPLR) pursuant to DFARS 252.227.7013
dnl
dnl NO WARRANTY
dnl
dnl ANY INFORMATION, MATERIALS, SERVICES, INTELLECTUAL PROPERTY OR OTHER
dnl PROPERTY OR RIGHTS GRANTED OR PROVIDED BY CARNEGIE MELLON UNIVERSITY
dnl PURSUANT TO THIS LICENSE (HEREINAFTER THE "DELIVERABLES") ARE ON AN
dnl "AS-IS" BASIS. CARNEGIE MELLON UNIVERSITY MAKES NO WARRANTIES OF ANY
dnl KIND, EITHER EXPRESS OR IMPLIED AS TO ANY MATTER INCLUDING, BUT NOT
dnl LIMITED TO, WARRANTY OF FITNESS FOR A PARTICULAR PURPOSE,
dnl MERCHANTABILITY, INFORMATIONAL CONTENT, NONINFRINGEMENT, OR ERROR-FREE
dnl OPERATION. CARNEGIE MELLON UNIVERSITY SHALL NOT BE LIABLE FOR INDIRECT,
dnl SPECIAL OR CONSEQUENTIAL DAMAGES, SUCH AS LOSS OF PROFITS OR INABILITY
dnl TO USE SAID INTELLECTUAL PROPERTY, UNDER THIS LICENSE, REGARDLESS OF
dnl WHETHER SUCH PARTY WAS AWARE OF THE POSSIBILITY OF SUCH DAMAGES.
dnl LICENSEE AGREES THAT IT WILL NOT MAKE ANY WARRANTY ON BEHALF OF
dnl CARNEGIE MELLON UNIVERSITY, EXPRESS OR IMPLIED, TO ANY PERSON
dnl CONCERNING THE APPLICATION OF OR THE RESULTS TO BE OBTAINED WITH THE
dnl DELIVERABLES UNDER THIS LICENSE.
dnl
dnl Licensee hereby agrees to defend, indemnify, and hold harmless Carnegie
dnl Mellon University, its trustees, officers, employees, and agents from
dnl all claims or demands made against them (and any related losses,
dnl expenses, or attorney's fees) arising out of, or relating to Licensee's
dnl and/or its sub licensees' negligent use or willful misuse of or
dnl negligent conduct or willful misconduct regarding the Software,
dnl facilities, or other rights or assistance granted by Carnegie Mellon
dnl University under this License, including, but not limited to, any
dnl claims of product liability, personal injury, death, damage to
dnl property, or violation of any laws or regulations.
dnl
dnl Carnegie Mellon University Software Engineering Institute authored
dnl documents are sponsored by the U.S. Department of Defense under
dnl Contract FA8721-05-C-0003. Carnegie Mellon University retains
dnl copyrights in all material produced under this contract. The U.S.
dnl Government retains a non-exclusive, royalty-free license to publish or
dnl reproduce these documents, or allow others to do so, for U.S.
dnl Government purposes only pursuant to the copyright license under the
dnl contract clause at 252.227.7013.
dnl
dnl @OPENSOURCE_HEADER_END@

dnl RCSIDENT("$Id$")

# ---------------------------------------------------------------------------
# AX_CHECK_LIBZ
#
#    Determine how to use the zlib (gzip) compression library
#
#    Substitutions: ENABLE_ZLIB
#    Output defines: BG_ENABLE_ZLIB

AC_DEFUN([AX_CHECK_LIBZ],[
    ENABLE_ZLIB=0

    AC_ARG_WITH([zlib],[AS_HELP_STRING([--with-zlib=ZLIB_DIR],
            [specify location of the zlib file compression library; find "zlib.h" in ZLIB_DIR/include/; find "libz.so" in ZLIB_DIR/lib/ [auto]])[]dnl
        ],[
            if test "x$withval" != "xyes"
            then
                zlib_dir="$withval"
                zlib_includes="$zlib_dir/include"
                zlib_libraries="$zlib_dir/lib"
            fi
    ])
    AC_ARG_WITH([zlib-includes],[AS_HELP_STRING([--with-zlib-includes=DIR],
            [find "zlib.h" in DIR/ (overrides ZLIB_DIR/include/)])[]dnl
        ],[
            if test "x$withval" = "xno"
            then
                zlib_dir=no
            elif test "x$withval" != "xyes"
            then
                zlib_includes="$withval"
            fi
    ])
    AC_ARG_WITH([zlib-libraries],[AS_HELP_STRING([--with-zlib-libraries=DIR],
            [find "libz.so" in DIR/ (overrides ZLIB_DIR/lib/)])[]dnl
        ],[
            if test "x$withval" = "xno"
            then
                zlib_dir=no
            elif test "x$withval" != "xyes"
            then
                zlib_libraries="$withval"
            fi
    ])

    if test "x$zlib_dir" != "xno"
    then
        # Cache current values
        bg_save_LDFLAGS="$LDFLAGS"
        bg_save_LIBS="$LIBS"
        bg_save_CFLAGS="$CFLAGS"
        bg_save_CPPFLAGS="$CPPFLAGS"

        if test "x$zlib_libraries" != "x"
        then
            ZLIB_LDFLAGS="-L$zlib_libraries"
            LDFLAGS="$ZLIB_LDFLAGS $bg_save_LDFLAGS"
        fi

        if test "x$zlib_includes" != "x"
        then
            ZLIB_CFLAGS="-I$zlib_includes"
            CPPFLAGS="$ZLIB_CFLAGS $bg_save_CPPFLAGS"
        fi

        AC_CHECK_LIB([z], [gzopen],
            [ENABLE_ZLIB=1 ; ZLIB_LDFLAGS="$ZLIB_LDFLAGS -lz"])

        if test "x$ENABLE_ZLIB" = "x1"
        then
            AC_CHECK_HEADER([zlib.h], , [
                AC_MSG_WARN([Found libz but not zlib.h.  Maybe you should install zlib-devel?])
                ENABLE_ZLIB=0])
        fi

        # Restore cached values
        LDFLAGS="$bg_save_LDFLAGS"
        LIBS="$bg_save_LIBS"
        CFLAGS="$bg_save_CFLAGS"
        CPPFLAGS="$bg_save_CPPFLAGS"
    fi

    if test "x$ENABLE_ZLIB" != "x1"
    then
        ZLIB_CFLAGS=
        ZLIB_LDFLAGS=
    else
        LIBS="$LIBS $ZLIB_LDFLAGS"
        CFLAGS="$ZLIB_CFLAGS $CFLAGS"
    fi

    AC_DEFINE_UNQUOTED([BG_ENABLE_ZLIB], [$ENABLE_ZLIB],
        [Define to 1 build with support for zlib compression.  Define
         to 0 otherwise.  Requires the libz library and the <zlib.h>
         header file.])
    AC_SUBST([ENABLE_ZLIB], [$ENABLE_ZLIB])
])# AX_CHECK_LIBZ

dnl Local Variables:
dnl mode:autoconf
dnl indent-tabs-mode:nil
dnl End:
