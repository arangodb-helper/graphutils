#!/bin/sh
if test ! -d docopt.cpp ; then
    git clone https://github.com/docopt/docopt.cpp
fi
if test ! -d velocypack ; then
    git clone https://github.com/arangodb/velocypack
    cd velocypack
    patch -p 1 < ../velocypack.diff
    cd ..
fi
