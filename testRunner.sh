#!/bin/sh
echo Starting test runner
echo

# To be able to run in `build` for coverage:
if [ ! -d tests ] ; then
    cd ..
fi

if ! build/smartifier2 --test ; then
    echo Unit tests failed!
    exit 2
fi

echo

for t in tests/* ; do
    echo Running test in $t...
    cd $t
    if ! ./todo.sh ; then
        echo "Bad test"
        exit 1
    fi
    cd ../..
    echo
done

