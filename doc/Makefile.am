## Process this file with automake to produce Makefile.in
## ------------------------------------------------------------------------
## Makefile.am (documentation)
## autotools build system for BigGrep applications
## ------------------------------------------------------------------------
## Copyright (C) 2012-2017 Carnegie Mellon University. All Rights Reserved.
## ------------------------------------------------------------------------
## Authors: CERT/CC
## ------------------------------------------------------------------------
## GNU General Public License (GPL) Rights pursuant to Version 2, June 1991
## Government Purpose License Rights (GPLR) pursuant to DFARS 252.227-7013
## ------------------------------------------------------------------------

PODFILES = bgindex.pod bgparse.pod bgsearch.pod bgverify.pod bgextractfile.pod biggrep.conf.pod

man1_MANS = bgindex.1 bgparse.1 bgsearch.1 bgverify.1 bgextractfile.1
man5_MANS = biggrep.conf.5

bgindex.1:
	-rm -f $@ $@.tmp
	@p=bgindex.pod srcdir='' ;\
	 test -f '$$p' || srcdir='${srcdir}/' ; \
	 echo pod2man --section=1 --center="${PACKAGE_NAME}" --release="${PACKAGE_VERSION}" "$${srcdir}$$p" ">" $@.tmp ;\
	 pod2man --section=1 --center="${PACKAGE_NAME}" --release="${PACKAGE_VERSION}" "$${srcdir}$$p" > $@.tmp
	mv $@.tmp $@

bgparse.1:
	-rm -f $@ $@.tmp
	@p=bgparse.pod srcdir='' ;\
	 test -f '$$p' || srcdir='${srcdir}/' ; \
	 echo pod2man --section=1 --center="${PACKAGE_NAME}" --release="${PACKAGE_VERSION}" "$${srcdir}$$p" ">" $@.tmp ;\
	 pod2man --section=1 --center="${PACKAGE_NAME}" --release="${PACKAGE_VERSION}" "$${srcdir}$$p" > $@.tmp
	mv $@.tmp $@

bgsearch.1:
	-rm -f $@ $@.tmp
	@p=bgsearch.pod srcdir='' ;\
	 test -f '$$p' || srcdir='${srcdir}/' ; \
	 echo pod2man --section=1 --center="${PACKAGE_NAME}" --release="${PACKAGE_VERSION}" "$${srcdir}$$p" ">" $@.tmp ;\
	 pod2man --section=1 --center="${PACKAGE_NAME}" --release="${PACKAGE_VERSION}" "$${srcdir}$$p" > $@.tmp
	mv $@.tmp $@

bgverify.1:
	-rm -f $@ $@.tmp
	@p=bgverify.pod srcdir='' ;\
	 test -f '$$p' || srcdir='${srcdir}/' ; \
	 echo pod2man --section=1 --center="${PACKAGE_NAME}" --release="${PACKAGE_VERSION}" "$${srcdir}$$p" ">" $@.tmp ;\
	 pod2man --section=1 --center="${PACKAGE_NAME}" --release="${PACKAGE_VERSION}" "$${srcdir}$$p" > $@.tmp
	mv $@.tmp $@

bgextractfile.1:
	-rm -f $@ $@.tmp
	@p=bgextractfile.pod srcdir='' ;\
	 test -f '$$p' || srcdir='${srcdir}/' ; \
	 echo pod2man --section=1 --center="${PACKAGE_NAME}" --release="${PACKAGE_VERSION}" "$${srcdir}$$p" ">" $@.tmp ;\
	 pod2man --section=1 --center="${PACKAGE_NAME}" --release="${PACKAGE_VERSION}" "$${srcdir}$$p" > $@.tmp
	mv $@.tmp $@

biggrep.conf.5:
	-rm -f $@ $@.tmp
	@p=bgparse.pod srcdir='' ;\
	 test -f '$$p' || srcdir='${srcdir}/' ; \
	 echo pod2man --section=5 --center="${PACKAGE_NAME}" --release="${PACKAGE_VERSION}" "$${srcdir}$$p" ">" $@.tmp ;\
	 pod2man --section=5 --center="${PACKAGE_NAME}" --release="${PACKAGE_VERSION}" "$${srcdir}$$p" > $@.tmp
	mv $@.tmp $@


MOSTLYCLEANFILES = $(man1_MANS) $(man5_MANS)

EXTRA_DIST = $(PODFILES)



