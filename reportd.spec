%global _hardened_build 1

Name:           reportd
Version:        0.6.7
Release:        1%{?dist}
Summary:        Service reporting org.freedesktop.Problems2 entries

License:        GPLv2+
URL:            https://github.com/abrt/%{name}
Source0:        https://github.com/abrt/%{name}/archive/%{version}/%{name}-%{version}.tar.gz

BuildRequires:  gcc
BuildRequires:  glib2-devel
BuildRequires:  libreport-devel
BuildRequires:  meson
BuildRequires:  systemd

%description
A D-Bus service that exports libreport functionality.


%prep
%autosetup


%build
%meson
%meson_build


%install
%meson_install


%check
%meson_test


%files
%doc NEWS README
%license COPYING
%{_libexecdir}/%{name}
%{_datadir}/dbus-1/services/org.freedesktop.%{name}.service
%{_datadir}/dbus-1/system-services/org.freedesktop.%{name}.service
%{_datadir}/dbus-1/system.d/org.freedesktop.%{name}.conf
%{_unitdir}/%{name}.service
%{_userunitdir}/%{name}.service


%changelog
* Mon May 6 2019 Ernestas Kulik <ekulik@redhat.com> - 0.6.7-1
- Update to 0.6.7

* Fri Apr 12 2019 Ernestas Kulik <ekulik@redhat.com> - 0.6.6-1
- Update to 0.6.6

* Sun Apr 7 2019 Ernestas Kulik <ekulik@redhat.com> - 0.6.5-1
- Update to 0.6.5

* Thu Mar 21 2019 Ernestas Kulik <ekulik@redhat.com> - 0.6.4-1
- Update to 0.6.4

* Wed Mar 20 2019 Ernestas Kulik <ekulik@redhat.com> - 0.6.3-2
- Add back systemd BuildRequires

* Wed Mar 20 2019 Ernestas Kulik <ekulik@redhat.com> - 0.6.3-1
- Update to 0.6.3

* Thu Mar 7 2019 Ernestas Kulik <ekulik@redhat.com> - 0.6.2-1
- Update to 0.6.2

* Mon Mar 4 2019 Ernestas Kulik <ekulik@redhat.com> - 0.6.1-1
- Update to 0.6.1

* Fri Feb 22 2019 Ernestas Kulik <ekulik@redhat.com> - 0.6-1
- Update to 0.6

* Mon Feb 4 2019 Ernestas Kulik <ekulik@redhat.com> - 0.5-1
- Update to 0.5

* Sat Feb 02 2019 Fedora Release Engineering <releng@fedoraproject.org> - 0.4.1-2
- Rebuilt for https://fedoraproject.org/wiki/Fedora_30_Mass_Rebuild

* Wed Jan 2 2019 Ernestas Kulik <ekulik@redhat.com> - 0.4.1-1
- Update to 0.4.1
- Move to Meson
- Add dummy check section

* Thu Dec 20 2018 Ernestas Kulik <ekulik@redhat.com> - 0.2.1-1
- Update to 0.2.1

* Thu Dec 20 2018 Ernestas Kulik <ekulik@redhat.com> - 0.2-2
- Fix Source0 URL yet again

* Tue Dec 18 2018 Ernestas Kulik <ekulik@redhat.com> - 0.2-1
- Drop patches, fixes are upstream
- Fix summary formatting

* Mon Dec 17 2018 Ernestas Kulik <ekulik@redhat.com> - 0.1-3
- Fix Source0 URL
- Use more modern macros
- Fix autoreconf invocation to install files

* Thu May 19 2016 Jakub Filak <jfilak@redhat.com> - 0.1-2
- Add all BuildRequires
- Verbose command line argument
- Cache moved to /var/run/user/reportd

* Thu Apr 14 2016 Jakub Filak <jfilak@redhat.com> - 0.1-1
- Initial packaging
