Summary: Lowlevel DNS(SEC) library with API
Name: ldns
Version: 1.0.0
Release: 4
License: LGPL
Url: http://open.nlnetlabs.nl/%{name}/
Source: http://open.nlnetlabs.nl/downloads/%{name}-%{version}.tar.gz
Group: System Environment/Libraries
BuildRoot: %{_tmppath}/%{name}-%{version}-%{release}-root-%(%{__id_u} -n)
Requires: openssl
BuildRequires: libtool, autoconf, automake, gcc-c++, openssl-devel, doxygen

%description
ldns is a library with the aim to simplify DNS programing in C. All
lowlevel DNS/DNSSEC operations are supported. We also define a higher
level API which allows a programmer to (for instance) create or sign
packets.

%package devel
Summary: Development package that includes the ldns header files
Group: Development/Libraries
Requires: %{name} = %{version}-%{release}, openssl-devel

%description devel
The devel package contains the ldns library and the include files

%prep
rm -rf %{buildroot}
%setup -q 
libtoolize
autoreconf

%configure


%build
%{__make} %{?_smp_mflags} allautoconf
%{__make} %{?_smp_mflags}
%{__make} %{?_smp_mflags} drill
%{__make} %{?_smp_mflags} examples
%{__make} %{?_smp_mflags} doc

%install

export DESTDIR=%{buildroot}
%{__make} install
%{__make} examples-install
%{__make} install-doc
%{__make} drill-install
#remove doc stubs
rm -rf doc/.svn
#remove double set of man pages
rm -rf doc/man

%clean
rm -rf %{buildroot}

%files 
%defattr(-,root,root)
%{_libdir}/libldns*so
%{_bindir}/drill
%{_bindir}/ldns-*
%doc README LICENSE ROADMAP TODO 
%doc %{_mandir}/*/*

%files devel
%defattr(-,root,root,-)
%{_libdir}/libldns.la
%{_libdir}/libldns.a
%dir %{_includedir}/ldns/*
%doc doc
%doc Changelog COMPILE 

%pre

%post 
/sbin/ldconfig

%postun
/sbin/ldconfig

%changelog
* Wed Oct  5 2005 Paul Wouters <paul@xelerance.com> 0.70_1205
- reworked for svn version

* Sun Sep 25 2005 Paul Wouters <paul@xelerance.com> - 0.70
- Initial version