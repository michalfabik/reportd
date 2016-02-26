
servicedir       = $(DBUS_SERVICES_DIR)
service_in_files = \
	org.freedesktop.reportd.service.in
service_DATA     = $(service_in_files:.service.in=.service)

noinst_LIBRARIES = libreportd-dbus.a

BUILT_SOURCES = \
	report-dbus-generated.c \
	report-dbus-generated.h \
	$(NULL)

libreportd_dbus_a_SOURCES = \
	report-dbus-constants.h \
	$(NULL)

nodist_libreportd_dbus_a_SOURCES = \
	$(BUILT_SOURCES) \
	$(NULL)

libreportd_dbus_a_CFLAGS = \
	$(GLIB_CFLAGS) \
	-Wno-unused-parameter
	$(NULL)

DBUS_XML_DEFINITIONS = \
	org.freedesktop.reportd.xml

DBUS_GENERATED = \
	report-dbus-generated.c \
	report-dbus-generated.h \
	$(NULL)

$(DBUS_GENERATED): org.freedesktop.reportd.xml
	$(AM_V_GEN) gdbus-codegen --interface-prefix org.freedesktop.reportd. \
		--generate-c-code report-dbus-generated \
		--c-namespace ReportDbus $<

EXTRA_DIST = \
	$(DBUS_XML_DEFINITIONS) \
	$(service_in_files) \
	$(NULL)

CLEANFILES = \
	$(DBUS_GENERATED) \
	$(service_DATA) \
	$(NULL)
