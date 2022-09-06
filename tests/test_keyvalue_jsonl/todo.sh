#!/bin/sh

../../build/smartifier2 vertices --type jsonl --input profiles.jsonl --output profiles_smart.jsonl --smart-graph-attribute country --key-value id

if ! cmp profiles_smart.jsonl profiles_expected.jsonl ; then
    echo Error in profiles_smart.jsonl!
    exit 1
fi

cp relations.jsonl relations_smart.jsonl
../../build/smartifier2 edges --type jsonl --vertices profiles:profiles_smart.jsonl --edges relations_smart.jsonl:profiles:profiles

if ! cmp relations_smart.jsonl relations_expected.jsonl ; then
    echo Error in relations.jsonl!
    exit 2
fi

rm profiles_smart.jsonl relations_smart.jsonl
