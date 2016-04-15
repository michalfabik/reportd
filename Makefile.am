# A sort of sentinel for argument lists
NULL =

SUBDIRS = dbus service po

run: all
	service/reportd

debug: all
	libtool --mode=execute gdb --ex=run --args service/reportd

RPM_DIRS = --define "_sourcedir $(abs_top_srcdir)" \
	--define "_specdir $(abs_top_srcdir)/package" \
	--define "_builddir $(abs_top_srcdir)/package/rpmbuild/BUILD" \
	--define "_rpmdir $(abs_top_srcdir)/package/rpmbuild/RPMS" \
	--define "_srcrpmdir $(abs_top_srcdir)/package/rpmbuild/SRPMS"

rpm:
	autoconf --force
	$(MAKE) dist
	rpmbuild $(RPM_DIRS) -ba package/reportd.spec

srpm:
	autoconf --force
	$(MAKE) dist
	rpmbuild $(RPM_DIRS) -bs package/reportd.spec
