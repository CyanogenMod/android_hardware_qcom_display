# configure.ac -- Autoconf script for libgralloc
#
# Process this file with autoconf to produce a configure script

# Requires autoconf tool later than 2.61
AC_PREREQ(2.61)
# Initialize the gps loc_api package version 1.0.0
AC_INIT([libgralloc],1.0.0)
# Does not strictly follow GNU Coding standards
AM_INIT_AUTOMAKE([-Wall -Werror foreign])
m4_ifdef([AM_PROG_AR], [AM_PROG_AR])
LT_INIT
# Disables auto rebuilding of configure, Makefile.ins
AM_MAINTAINER_MODE
# defines some macros variable to be included by source
AC_CONFIG_HEADERS([config.h])
AC_CONFIG_MACRO_DIR([m4])

AC_ENABLE_SHARED

# Checks for programs.
AC_PROG_LIBTOOL
AC_PROG_CXX
AC_PROG_CC
AM_PROG_CC_C_O
AC_PROG_AWK
AC_PROG_CPP
AC_PROG_INSTALL
AC_PROG_LN_S
AC_PROG_MAKE_SET
PKG_PROG_PKG_CONFIG

AC_ARG_WITH([sanitized-headers],
                [AS_HELP_STRING([--with-sanitized-headers=DIR],[location of the sanitized Linux headers])],
                    [CPPFLAGS="$CPPFLAGS -I$withval"])


AC_ARG_WITH([libhardware_includes],
      AC_HELP_STRING([--with-libhardware-includes=@<:@dir@:>@],
         [Specify the location of the libhardware headers]),
      [libhardware_incdir=$withval],
      with_libhardware_includes=no)

if test "x$with_libhardware_includes" != "xno"; then
   CPPFLAGS="${CPPFLAGS} -I${libhardware_incdir}"
fi

AC_ARG_WITH([core_includes],
      AC_HELP_STRING([--with-core-includes=@<:@dir@:>@],
         [Specify the location of the core headers]),
      [core_incdir=$withval],
      with_core_includes=no)

if test "x$with_core_includes" != "xno"; then
   CPPFLAGS="${CPPFLAGS} -I${core_incdir}"
fi

AC_CONFIG_FILES([ \
        Makefile \
        libqdutils/Makefile \
        libgralloc/Makefile \
        ])

AC_OUTPUT
