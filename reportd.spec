%global _hardened_build 1

Name:           reportd
Version:        0.7.2
Release:        1%{?dist}
Summary:        Service reporting org.freedesktop.Problems2 entries

License:        GPLv2+
URL:            https://github.com/abrt/%{name}
Source0:        https://github.com/abrt/%{name}/archive/%{version}/%{name}-%{version}.tar.gz

BuildRequires:  gcc
BuildRequires:  pkgconfig(glib-2.0)
BuildRequires:  pkgconfig(libreport)
BuildRequires:  meson
BuildRequires:  systemd

%if 0%{?centos}
Recommends:     libreport-centos
%endif

%if 0%{?fedora}
Recommends:     libreport-fedora
%endif

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
