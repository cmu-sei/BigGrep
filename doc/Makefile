.PHONY: clean

#XML_CATALOG_FILES=../xml-catalog/xsl-ns-stylesheets-1.75.2/catalog.xml
#XSLTPROC_NONET = --nonet
XSLTPROC = xsltproc
XSLTPROC_FLAGS = $(XSLTPROC_NONET) --catalogs --xinclude http://docbook.sourceforge.net/release/xsl-ns/current/manpages/docbook.xsl

GZIP = gzip
GZIP_FLAGS = -f

MAN =	bgindex.1 \
	bgsearch.1 \
	bgverify.1 \
	bgparse.1 \
	biggrep.conf.5

.INTERMEDIATE: $(MAN)

man: $(MAN:=.gz)

#%.gz: %
$(MAN:=.gz): %.gz: %
	$(GZIP) $(GZIP_FLAGS) $<

$(MAN): %: %.xml
	XML_CATALOG_FILES=$(XML_CATALOG_FILES) $(XSLTPROC) $(XSLTPROC_FLAGS) $<

clean:
	$(RM) $(MAN:=.gz)
