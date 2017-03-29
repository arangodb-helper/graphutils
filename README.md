Some tools to work with graph data for ArangoDB
===============================================

The following tools are here:

  1. `sampleGraphMaker`: Creates a sample graph that resembles a social
     network
  2. `smartifier`: Transfers graph data to smart graph format

Overview and usage scenario
---------------------------

The basic idea is to use `arangoexport` to extract graph collection data
from an ArangoDB instance (single server or cluster), run the `smartifier`
to transfer the resulting data files into smart graph format, and then
use `arangoimp` to import the data into an ArangoDB Enterprise instance
(cluster).

The basic commands to do this for a graph `G` with vertex collection `V` and
edge collection `E` are as follows, if `country` shall be the smart graph
attribute:

    arangoexport --server.endpoint tcp://OLDINSTANCE:PORT --collection V --collection E --type jsonl
    smartifier --type jsonl export/V.jsonl V export/E.jsonl country
    arangosh --server.endpoint tcp://NEWCLUSTER:PORT
        arangosh> var sg = require("@arangodb/smart-graph");
        arangosh> sg._create(...)
    arangoimport --server.endpoint tcp://NEWCLUSTER:PORT --input.file export/V.jsonl --type json --collection V
    arangoimport --server.endpoint tcp://NEWCLUSTER:PORT --input.file export/E.jsonl --type json --collection E

Note that the `arangoexport` utility is only available in ArangoDB >= 3.2,
therefore you either have to install one of the prereleases or use the
Docker image.

Docker image
------------

The programs are available as a Docker image `neunhoef/graphutils`.
Simply run

    docker run -it -v /outsideData:/data neunhoef/graphutils bash

You find yourself in a shell with the outsideData directory mounted
under `/data`. The executables are in `/`.

Usage of `smartifier`
---------------------

Assume you have a graph in the form of two (or more, see below) CSV or
JSONL files, one for vertices and one for edges. In the case of CSV,
both must have a header line for the column attribute names. The vertex
file must contain `_key`, the edge file must contain `_from` and `_to`,
and can optionally contain `_key` as well. You need to have a smart
graph attribute given with each vertex.

    Usage:
      smartifier [--type=<type>] [--separator=<separator>]
                 [--quoteChar=<quoteChar>] [--memory=MEMORY]
                 <vertexFile> <vertexColl> <edgeFile> <smartGraphAttr>

    Options:
      -h --help                Show this screen.
      --version                Show version.
      --type=<type>            Data type "csv" or "jsonl" [default: csv]
      --separator=<separator>  Column separator for csv type [default: ,]
      --quoteChar=<quoteChar>  Quote character for csv type [default: "]
      --memory=<memory>        Limit RAM usage in MiB [default: 4096]
      <vertexFile>             File for the vertices.
      <vertexColl>             Name of vertex collection.
      <edgeFile>               File for the edges.
      <smartGraphAttr>         Smart graph attribute.

where

  - `<vertexFile>` is the input file for the vertices,
  - `<vertexColl>` is the name of the vertex collection of the
    `<vertexFile>`
  - `<edgeFile>` is the input file for the edges,
  - `<smartGraphAttr>` is the name of the (string-valued!) attribute for
    the smart graph sharding, this must be one of the column names of
    the vertex file
  - `<memory>` is a positive number which tells the program to
    use at most that many MB of main memory (see below)
  - `<separator>` (only CSV case) is a single ASCII character which is
    used to separate the column entries, default is the comma ,
  - `<quoteChar>` is a single ASCII character which is used as the quote
    character, default is double quotes \"

Use the `--type` switch to switch to the JSONL format instead of CSV.

The algorithm runs once through the vertex file and transforms all
entries in the `_key` attribute by prepending the value of the smart graph
attribute and a colon. If the `_key` value already contains a colon no
transformation is done. The program remembers all smart graph attributes
of all keys. This is where the RAM limit comes in.

When the in memory data structures exceed the given limit (and once at
the end of the vertex file), execution of the vertex file is paused and
the edge file is transformed, according to the current translation table
in RAM. This means that all values in the `_from` and `_to` column of
the edge file are inspected, if they do not contain a slash, they are
simply kept, if they contain a slash (as they should do!) and the
collection name matches `<vertexColl>`, the key part behind the slash
is extracted and searched for a colon. If there is already one, no
action is performed. If there is not yet a colon, then the key is looked
up in the in memory tables and if it is found, the attribute value is
translated by inserting the smart graph attribute. If there is a `_key`
column and both `_from` and `_to` value contain a smart graph attribute,
then the `_key` value is transformed as well, as long as it is not yet
transformed. The resulting edge file is written next to the existing one,
and in the end moved over the original, if all went well.

This means that the time complexity is

    O(V) + O(max(1.0, V / L) * E)

where V is the size of the vertex collection, L is the memory limit
and E is the size of the edge collection. In the optimal case in which
all vertex keys and smart graph attributes fit into RAM this simplifies
to O(V + E).

If you have multiple vertex collection and/or multiple edge collections,
simply run the program on all pairs of vertex and edge collections.
Translations that have already been performed will not be done again
and edge `_key` translations will be done once when all required data
is available.

Usage of `sampleGraphMaker`
---------------------------

    Usage:
      sampleGraphMaker [--type=<type>] 
               <baseName> <numberVertices> <numberEdges> [<seed>]

    Options:
      -h --help                Show this screen.
      --version                Show version.
      --type=<type>            Data type "csv" or "jsonl" [default: csv].
      <baseName>               Name prefix for files.
      <numberVertices>         Number of vertices.
      <numberEdges>            Number of edges.
      <seed>                   Smart graph attribute [default: 1].


where `<baseName>` is a base name for the resulting files. The program will
produce two files by appending `"_profiles.csv"` and `"_relations.csv"`
respectively. The former will contain `<numberVertices>` profiles and the
latter will contain `<numberEdges>` relations between two profiles. All is
random and the files can directly be imported to ArangoDB using
`arangoimp`. The `<seed>` must be a number and seeds the pseudo random
number generator for reproducible results. Use the `--type` switch to switch
to the JSONL format instead of CSV.

Build
-----

Simply say

    cd build
    cmake ..
    make
    cd ..

Test
----

Some tests are performed when you do

    make test

