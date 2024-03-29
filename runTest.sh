#!/bin/bash

echo Testing version 1...

echo First trivial test with CSV files:
build/sampleGraphMaker --type csv testCase/test 10 10 1 > /dev/null
build/smartifier --memory 1024 --type csv testCase/test_profiles.csv profiles testCase/test_relations.csv country > /dev/null
cmp testCase/test_profiles.csv testCase/test_profiles_known.csv
cmp testCase/test_relations.csv testCase/test_relations_known.csv
rm testCase/test_profiles.csv testCase/test_relations.csv

echo Second trivial test with JSONL files:
build/sampleGraphMaker --type jsonl testCase/test 10 10 1 > /dev/null
build/smartifier --memory 1024 --type jsonl testCase/test_profiles.jsonl profiles testCase/test_relations.jsonl country > /dev/null
cmp testCase/test_profiles.jsonl testCase/test_profiles_known.jsonl
cmp testCase/test_relations.jsonl testCase/test_relations_known.jsonl
rm testCase/test_profiles.jsonl testCase/test_relations.jsonl

echo Third test with JSONL and a few non-standard cases:
cp testCase/test_profiles_special.jsonl testCase/test_profiles.jsonl
cp testCase/test_relations_special.jsonl testCase/test_relations.jsonl
build/smartifier --memory 1024 --type jsonl testCase/test_profiles.jsonl profiles testCase/test_relations.jsonl country > /dev/null 2> /dev/null
cmp testCase/test_profiles.jsonl testCase/test_profiles_special_known.jsonl
cmp testCase/test_relations.jsonl testCase/test_relations_special_known.jsonl
rm testCase/test_profiles.jsonl testCase/test_relations.jsonl

echo Fourth test with JSONL and a few non-standard cases and default attribute:
cp testCase/test_profiles_special.jsonl testCase/test_profiles.jsonl
cp testCase/test_relations_special.jsonl testCase/test_relations.jsonl
build/smartifier --memory 1024 --smartDefault=Max --type jsonl testCase/test_profiles.jsonl profiles testCase/test_relations.jsonl country > /dev/null 2> /dev/null
cmp testCase/test_profiles.jsonl testCase/test_profiles_special_withdefault_known.jsonl
cmp testCase/test_relations.jsonl testCase/test_relations_special_withdefault_known.jsonl
rm testCase/test_profiles.jsonl testCase/test_relations.jsonl

echo
echo
