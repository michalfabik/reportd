bin_PROGRAMS = \
	reportd

reportd_SOURCES = \
	report-daemon.h \
	report-daemon.cpp \
	report-task.h \
	report-task.cpp \
	report-service.h \
	report-service.cpp \
	$(NULL)

reportd_CXXFLAGS = \
	-I$(top_srcdir)/dbus \
	$(GLIBMM_CFLAGS) \
	$(GLIB_CFLAGS) \
	$(LIBREPORT_CFLAGS) \
	$(NULL)

reportd_LDADD = \
	$(top_srcdir)/dbus/libreportd-dbus.a \
	$(GLIBMM_LIBS) \
	$(GLIB_LIBS) \
	$(LIBREPORT_LIBS) \
	$(NULL)
