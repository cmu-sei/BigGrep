#!/bin/sh

AUTOCONF=$(which autoconf)
AUTOMAKE=$(which automake)
LIBTOOL=$(which libtool)
LIBTOOLIZE=$(which libtoolize)
AUTORECONF=$(which autoreconf)

if test -z $AUTOCONF; then
    echo "autoconf is required. Please install autoconf and try again"
    exit
fi

if test -z $AUTOMAKE; then
    echo "automake is required. Please install automake and try again"
    exit
fi

if test -z $LIBTOOL && test -z $LIBTOOLIZE ; then
    echo "libtool and libtoolize are required. Please install them and try again"
    exit
fi

if test -z $AUTORECONF; then
    echo "autoreconf is required. Please install it and try again"
    exit
fi

autoreconf -ivf
