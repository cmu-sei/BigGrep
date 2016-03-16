# Don't try fancy stuff like debuginfo, which is useless on binary-only
# packages. Don't strip binary too
# Be sure buildpolicy set to do nothing
%define        __spec_install_post %{nil}
%define          debug_package %{nil}
%define        __os_install_post %{_dbpath}/brp-compress

%{!?__python2: %global __python2 /usr/bin/python}
%{!?python2_sitelib: %global python2_sitelib %(%{__python2} -c "from distutils.sysconfig import get_python_lib; print(get_python_lib())")}
%{!?python2_sitearch: %global python2_sitearch %(%{__python2} -c "from distutils.sysconfig import get_python_lib; print(get_python_lib(1))")}


%define man1 /usr/share/man/man1
%define man5 /usr/share/man/man5

Summary: CERT BigGrep
Name: biggrep
Version: 2.6
Release: 1%{?dist}
License: Govt Right To Use + GPLv2
Group: Utilities

# if using the system poco libs instead of hand compiled:
#Requires: python >= 2.6, poco-foundation, poco-util, poco-xml
Requires: python >= 2.6

BuildRoot: %{_tmppath}/%{name}-%{version}-%{release}-root
#AutoReqProv: no

%description
%{summary} is used to create and query N-gram indexes of arbitrary binary files.

%define _curdir %(pwd)

%prep
rm -rf %{name}-%{version}
mkdir %{name}-%{version}
mkdir %{name}-%{version}/src
mkdir %{name}-%{version}/src/biggrep
mkdir %{name}-%{version}/doc
mkdir %{name}-%{version}/conf
mkdir %{name}-%{version}/conf/biggrep
cp %_curdir/Makefile %{name}-%{version}
cp %_curdir/src/*.cpp %{name}-%{version}/src
cp %_curdir/src/*.hpp %{name}-%{version}/src
cp %_curdir/src/*.h %{name}-%{version}/src
cp %_curdir/src/*.py %{name}-%{version}/src
cp %_curdir/src/biggrep/*.py %{name}-%{version}/src/biggrep
cp %_curdir/doc/Makefile %{name}-%{version}/doc/
cp %_curdir/doc/*.xml %{name}-%{version}/doc/
cp %_curdir/doc/*.pdf %{name}-%{version}/doc/
cp %_curdir/doc/*.txt %{name}-%{version}/doc/
cp %_curdir/LICENSE.txt %{name}-%{version}
cp %_curdir/GPL.txt %{name}-%{version}
cp %_curdir/biggrep.spec %{name}-%{version}
cp %_curdir/conf/biggrep/biggrep.conf %{name}-%{version}/conf/biggrep
ln -s %_curdir/poco %{name}-%{version}/poco
ln -s %_curdir/xml-catalog %{name}-%{version}/xml-catalog

%build
cd %{name}-%{version}
make

%install
rm -rf $RPM_BUILD_ROOT
mkdir -p $RPM_BUILD_ROOT
make DESTDIR=$RPM_BUILD_ROOT py_site_packages=%{python2_sitelib} install -C %{name}-%{version}

%clean
rm -rf $RPM_BUILD_ROOT

%files
%defattr(-,root,root,-)
#%config(noreplace) %{_sysconfdir}/%{name}/%{name}.conf
#%{_bindir}/*
%{man1}/bgverify.1.gz
%{man1}/bgparse.1.gz
%{man1}/bgindex.1.gz
%{man1}/bgsearch.1.gz
%{man5}/biggrep.conf.5.gz
/usr/bin/bgindex
/usr/bin/bgverify
/usr/bin/bgsearch
/usr/bin/bgparse
/usr/share/doc/%{name}-%{version}/LICENSE.txt
/usr/share/doc/%{name}-%{version}/GPL.txt
%{python2_sitelib}/biggrep/__init__.py
%{python2_sitelib}/biggrep/bgsearch.py
%{python2_sitelib}/biggrep/bgsearch_jobmanager.py
%{python2_sitelib}/biggrep/bgsearch_processor.py 
%{python2_sitelib}/biggrep/bgverify_processor.py
#/etc/biggrep/celeryconfig.py
%config /etc/biggrep/biggrep.conf

%changelog
#* xxxxxxxxxxxxxxx  mfcoates <mc-contact@cert.org> 2.6-0
#- User control over index file search queue order, user control over candidate throttle point
* Wed Oct 1 2014  mfcoates <mc-contact@cert.org> 2.5-2
- Packaging chage: RPM now enforces dependency on jobdispatch 0.0.3
* Thu Sep 18 2014  mfcoates <mc-contact@cert.org> 2.5-1
- Speed & memory consumption improvements for large candidate queries, and index generation.
- Metadata filtering bug fixes.
* Tue Apr 15 2014  mfcoates <mc-contact@cert.org> 2.4-1
- Yara matches now reported as metadata, terminating wildcard is now supported for metadata filtering, Yara verification is much faster, usability improvements.
* Fri Feb 28 2014  mfcoates <mc-contact@cert.org> 2.3-1
- Added Yara verification, metadata filtering, configurable index hint table (new index format), rejection during indexing of files that exceed an ngram threshold, bgsearch can now read defaults from a configuration file.
* Fri Nov 15 2013  mfcoates <mc-contact@cert.org> 2.2-1
- Rearchitect bgsearch to use Jobdispatch, greatly improved search/verification speed.
* Tue Oct 29 2013  mfcoates <mc-contact@cert.org> 2.1-2
- Staticically linked release for RHEL 5
* Tue Oct 15 2013  mfcoates <mc-contact@cert.org> 2.1-1
- Numerous indexing-related bug fixes, build/package cleanup/repair
* Mon Jun 24 2013  mfcoates <mc-contact@cert.org> 2.0-3
- Added MAN pages, renamed bgsearch.py to bgsearch, renamed bgindex_th to bgindex
* Tue Jan 22 2013  hines <mc-contact@cert.org> 2.0-1
- Intial RPM Build, static binaries only

