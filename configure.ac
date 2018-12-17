AC_INIT([reportd], [0.1], [crash-catcher@fedorahosted.org], [reportd])

INTLTOOL_REQ=0.35.0

AC_CONFIG_MACRO_DIR([build/m4])
AC_CONFIG_AUX_DIR([build])

AM_INIT_AUTOMAKE([-Wall tar-ustar])
AM_SANITY_CHECK

# Support silent build rules. Disable by either passing --disable-silent-rules
# to ./configure or passing V=1 to make
m4_ifdef([AM_SILENT_RULES], [AM_SILENT_RULES])
# The 'yes' argument is undocumented but it is common knowledge that it works.
AM_SILENT_RULES([yes])
AM_MAINTAINER_MODE([enable])
AC_CONFIG_HEADERS([config.h])
AC_CONFIG_SRCDIR([service/report-daemon.cpp])


AM_PROG_AR
AC_PROG_LIBTOOL
AC_PROG_CC
AC_PROG_CXX
AM_PROG_CC_C_O
IT_PROG_INTLTOOL($INTLTOOL_REQ)
AC_PATH_PROG(PKG_CONFIG, pkg-config, no)

AC_DISABLE_STATIC

GETTEXT_PACKAGE=reportd
AC_SUBST([GETTEXT_PACKAGE])
    AC_DEFINE_UNQUOTED([GETTEXT_PACKAGE],["$GETTEXT_PACKAGE"],[The gettext domain name])
    AM_GLIB_GNU_GETTEXT

PKG_CHECK_MODULES([GLIB],
        glib-2.0 >= 2.47
        gio-2.0
        gio-unix-2.0)
PKG_CHECK_MODULES([GLIBMM],
        glibmm-2.4 >= 2.47
        giomm-2.4)
PKG_CHECK_MODULES([LIBREPORT], [libreport])

DBUS_SERVICES_DIR="${datadir}/dbus-1/services"
AC_SUBST(DBUS_SERVICES_DIR)

CFLAGS="$CFLAGS -Wall -Wextra -std=c11"
CXXFLAGS="$CXXFLAGS -Wall -Wextra -std=c++14"

AC_CONFIG_FILES([
        Makefile
        po/Makefile.in
        dbus/Makefile
        service/Makefile
])
AC_OUTPUT
