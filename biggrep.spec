# Don't try fancy stuff like debuginfo, which is useless on binary-only
# packages. Don't strip binary too
# Be sure buildpolicy set to do nothing
%define        __spec_install_post %{nil}
%define          debug_package %{nil}
%define        __os_install_post %{_dbpath}/brp-compress
%define version 2.7.2
%define release 1

Summary: CERT biggrep
Name: biggrep
Version: %{version}
Release: %{release}%{?dist}
License: GPLv2
Group: Applications/System
Source: %{name}-%{version}.tar.gz
URL: http://github.com/cmu-sei/BigGrep
#for rhel5:
#Requires: python26
#Requires: python >= 2.6, poco-foundation, poco-util, poco-xml
Requires: python >= 2.6
BuildRoot: %{_tmppath}/%{name}-%{version}-%{release}-root
#AutoReqProv: no

%description
%{summary} is used to create and query N-gram 
indexes of arbitrary binary files.

%define _curdir %(pwd)
%define pythondir %(%{__python} -c 'from distutils import sysconfig; print sysconfig.get_python_lib()')

%prep
%setup -q -n %{name}-%{version}

%build
./configure --prefix=/usr
%{__make}

%install
rm -rf $RPM_BUILD_ROOT
mkdir -p $RPM_BUILD_ROOT
#make DESTDIR=$RPM_BUILD_ROOT install -C %{name}-%{version}
make DESTDIR=$RPM_BUILD_ROOT install
mkdir -p $RPM_BUILD_ROOT%{_sysconfdir}/biggrep
install --mode=0644 conf/biggrep/biggrep.conf $RPM_BUILD_ROOT%{_sysconfdir}/biggrep

%clean
rm -rf $RPM_BUILD_ROOT

%files
%defattr(-,root,root,-)
#%%config(noreplace) %{_sysconfdir}/%{name}/%{name}.conf
%{_mandir}/man1/bgverify.1
%{_mandir}/man1/bgparse.1
%{_mandir}/man1/bgindex.1
%{_mandir}/man1/bgsearch.1
%{_mandir}/man1/bgextractfile.1
%{_mandir}/man5/biggrep.conf.5
%{_bindir}/bgindex
%{_bindir}/bgverify
%{_bindir}/bgsearch
%{_bindir}/bgparse
%{_bindir}/bgextractfile
%{_bindir}/bgcount
#%/usr/share/doc/%{name}-%{version}/LICENSE.txt
%{pythondir}/biggrep/__init__.py
%{pythondir}/biggrep/bgsearch.py
%{pythondir}/biggrep/bgsearch_jobmanager.py
%{pythondir}/biggrep/bgsearch_processor.py 
%{pythondir}/biggrep/bgverify_processor.py
%{pythondir}/jobdispatch/__init__.py
%{pythondir}/jobdispatch/jobdispatch.py
%{pythondir}/jobdispatch/jobmanager.py
%{pythondir}/jobdispatch/mockjobmanager.py
%{pythondir}/jobdispatch/processor.py

%config %{_sysconfdir}/biggrep/biggrep.conf
%config %{_prefix}/etc/biggrep.conf

%changelog

* Fri Aug 11 2017 ecoff <mc-contact@cert.org> 2.7-1
- New autotools build system.
- Remove libpoco dependency.
- New bgextractfile program.
- New logging.
- Add Boost lockfree queue option.
- Add -O overflow option to work with max unique ngram limit
- Add compression support to bgindex and decompression support bgparse/bgdump
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

