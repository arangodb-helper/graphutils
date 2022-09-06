#!/bin/sh

../../build/smartifier2 vertices --type csv --input profiles.csv --output profiles_smart.csv --smart-graph-attribute smartKey --key-value id --smart-value id --smart-index 2

if ! cmp profiles_smart.csv profiles_expected.csv ; then
    echo Error in profiles_smart.csv!
    exit 1
fi

cp relations.csv relations_smart.csv
../../build/smartifier2 edges --type csv --vertices profiles:profiles_smart.csv --edges relations_smart.csv:profiles:profiles

if ! cmp relations_smart.csv relations_expected.csv ; then
    echo Error in relations.csv!
    exit 2
fi

rm profiles_smart.csv relations_smart.csv
