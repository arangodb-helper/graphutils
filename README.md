Some tools to work with graph data for ArangoDB
===============================================

The following tools are here:

  1. `sampleGraphMaker`: Creates a smaple graph that resembles a social
     network
  2. `smartifier`: Transfers CSV graph data to smart graph format

Usage of `sampleGraphMaker`
---------------------------

Usage:

    sampleGraphMaker <NAME> <NRVERTS> <NREDGES> <SEED>

where `<NAME>` is a base name for the resulting files. The program will
produce two files by appending `"_profiles.csv"` and `"_relations.csv"`
respectively. The former will contain `<NRVERTS>` profiles and the
latter will contain `<NREDGES>` relations between two profiles. All is
random and the files can directly be imported to ArangoDB using
`arangoimp`. The `<SEED>` must be a number and seeds the pseudo random
number generator for reproducible results.

Usage of `smartifier`
---------------------

Assume you have a graph in the form of two (or more, see below) CSV files,
one for vertices and one for edges. Both must have a header line for the
column attribute names. The vertex file must contain `_key`, the edge
file must contain `_from` and `_to`, and can optionally contain `_key`
as well. You need to have a smart graph attribute given with each
vertex.

Simply launch the `smartifier` by doing:

    smartifier <VERTEXFILE> <EDGEFILE> <SMARTGRAPHATTR> <MEMSIZE_IN_MB> [<SEPARATOR> [<QUOTECHAR>]]

where

  - `<VERTEXFILE>` is the input file for the vertices,
  - `<EDGEFILE>` is the input file for the edges,
  - `<SMARTGRAPHATTR>` is the name of the (string-valued!) attribute for
    the smart graph sharding, this must be one of the column names of
    the vertex file
  - `<MEMSIZE_IN_MB>` is a positive number which tells the program to
    use at most that many MB of main memory (see below)
  - `<SEPARATOR>` is a single ASCII character which is used to separate
    the column entries, default is the comma ,
  - `<QUOTECHAR>` is a single ASCII character which is used as the quote
    character, default is double quotes \"

The algorithm runs once through the vertex file and transforms all
entries in the `_key` column by prepending the value of the smart graph
attribute and a colon. If the `_key` value already contains a colon no
transformation is done. The program remembers all smart graph attributes
of all keys. This is where the RAM limit comes in.

When the in memory data structures exceed the given limit (and once at
the end of the vertex file), execution of the vertex file is paused and
the edge file is transformed, according to the current translation table
in RAM. This means that all values in the `_from` and `_to` column of
the edge file are inspected, if they do not contain a slash, they are
simply kept, if they contain a slash (as they should do!), the key part
behind the slash is extracted and searched for a colon. If there is
already one, no action is performed. If there is not yet a colon, then
the key is looked up in the in memory tables and if it is found, the
attribute value is translated by inserting the smart graph attribute.
If there is a `_key` column and both `_from` and `_to` value contain
a smart graph attribute, then the `_key` value is adapted as well, as
long as it is not yet adapted. The resulting edge file is written next
to the existing one, and in the end moved over the original, if all went
well.

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

Build
-----

Simply say

    make

Test
----

A rather trivial test is performed when you do

    make test

