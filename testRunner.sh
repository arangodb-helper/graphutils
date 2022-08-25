#!/bin/sh
echo Starting test runner
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
