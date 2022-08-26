#!/bin/sh

../../build/smartifier2 vertices --type csv --input v1.csv --output v1_smart.csv --smart-graph-attribute smart --separator "|" --smart-value _key --smart-index 2
../../build/smartifier2 vertices --type csv --input v2.csv --output v2_smart.csv --smart-graph-attribute smart --separator "|" --smart-value _key --smart-index 2

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
../../build/smartifier2 edges --type csv --edges e1_smart.csv:v1:v2 --edges e2_smart.csv:v2:v1 --separator "|" --smart-index 2

if ! cmp e1_smart.csv e1_expected.csv ; then
    echo Error in e1_smart.csv!
    exit 3
fi
if ! cmp e2_smart.csv e2_expected.csv ; then
    echo Error in e2_smart.csv!
    exit 4
fi

rm v1_smart.csv v2_smart.csv e1_smart.csv e2_smart.csv
