#%%global commit 6ea9f4f098461dd3eb376d2f7d5ce5cb3b2ab037
#%%global shortcommit %(c=%{commit}; echo ${c:0:7})

Name:		reportd
Version:	0.1
Release:	2%{?dist}
Summary:	Service reporting org.freedesktop.Problems2 entries.

Group:		Applications/System
License:	GPLv2+
URL:		https://github.com/jfilak/%{name}
#Source0:	https://github.com/jfilak/%{name}/archive/%{commit}/%{name}-%{version}-%{shortcommit}.tar.gz
Source0:	https://github.com/jfilak/%{name}/archive/%{commit}/%{name}-%{version}.tar.gz

BuildRequires:	libtool
BuildRequires:	intltool
BuildRequires:	automake
BuildRequires:	autoconf
BuildRequires:	glibmm24-devel
BuildRequires:	libreport-devel

%description
A D-Buse service that exports libreport functionality in D-Bus interface.


%prep
%setup -q


%build
autoreconf -f
%configure
make %{?_smp_mflags}


%install
%make_install


%files
#%doc LICENSE README AUTHORS
%{_libexecdir}/reportd
%{_datadir}/dbus-1/services/org.freedesktop.reportd.service


%changelog
* Thu May 19 2016 Jakub Filak <jfilak@redhat.com> - 0.1-2
- Add all BuildRequires
- Verbose command line argument
- Cache moved to /var/run/user/reportd

* Thu Apr 14 2016 Jakub Filak <jfilak@redhat.com> - 0.1-1
- Initial packaging
