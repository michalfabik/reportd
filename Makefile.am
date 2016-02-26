# A sort of sentinel for argument lists
NULL =

SUBDIRS = dbus service

run: all
	service/reportd

debug: all
	libtool --mode=execute gdb --ex=run --args service/reportd
