#!/bin/sh

../../build/smartifier2 vertices --type csv --input v1.csv --output v1_smart.csv --smart-graph-attribute smart --separator "|"
../../build/smartifier2 vertices --type csv --input v2.csv --output v2_smart.csv --smart-graph-attribute smart --separator "|"

if ! cmp v1_smart.csv v1_expected.csv ; then
    echo Error in v1_smart.csv!
    exit 1
fi
if ! cmp v2_smart.csv v2_expected.csv ; then
    echo Error in v2_smart.csv!
    exit 2
fi


cp e1.csv e1_smart.csv
cp e2.csv e2_smart.csv
../../build/smartifier2 edges --type csv --vertices v1:v1_smart.csv --vertices v2:v2_smart.csv --edges e1_smart.csv:v1:v2 --edges e2_smart.csv:v2:v1 --separator "|"

if ! cmp e1_smart.csv e1_expected.csv ; then
    echo Error in e1_smart.csv!
    exit 3
fi
if ! cmp e2_smart.csv e2_expected.csv ; then
    echo Error in e2_smart.csv!
    exit 4
fi

rm v1_smart.csv v2_smart.csv e1_smart.csv e2_smart.csv
