#!/bin/bash
set -e
if [ ! -d build ] ; then
    mkdir build
fi
cd build
cmake .. > /dev/null
make > /dev/null
cd ..
build/sampleGraphMaker testCase/test 10 10 1 > /dev/null
build/smartifier --memory 1024 testCase/test_profiles.csv profiles testCase/test_relations.csv country > /dev/null
cmp testCase/test_profiles.csv testCase/test_profiles_known.csv
cmp testCase/test_relations.csv testCase/test_relations_known.csv
rm testCase/test_profiles.csv testCase/test_relations.csv

