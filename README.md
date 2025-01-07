Some tools to work with graph data for ArangoDB
===============================================

The following tools are here:

  1. `sampleGraphMaker`: Creates a sample graph that resembles a social
     network
  2. `smartifier`: Transfers graph data to smart graph format
  3. `smartifier2`: Newer version with more features, but incompatible
     usage.

Overview for `smartifier` Version 2
-----------------------------------

Quite often, one has graph data either as CSV files or as JSONL files,
but outside of ArangoDB. One would like to import the data, but things
have to be massaged a bit to use the smart graph functionality of
ArangoDB.

What does this "smart graph functionality" do? Basically, it tries to
use domain knowledge in the data, to do the sharding of a large graph
better, such that "most" edges stay in a shard. That is, we want to
find locality in the data. To this end, we use some attribute (the
"smart graph attribute") in the data to do the sharding decision.
However, since the API of ArangoDB by definition has to find documents
by their primary key, the primary keys of the vertices have to be chosen
in a special way. Namely, they have to start with the value of the
smart graph attribute, followed by a colon and only then by the actual
original key. This setup achieves the best combination, namely:

  - it keeps data locality (provided the smart graph attribute is well chosen)
  - it allows finding the right shard by only looking at the key

The smartifier is a tool, which rewrites the input data, such that it
can be imported directly into an ArangoDB smart graph.


Usage of `smartifier2`
----------------------

The easiest way to use this tool is to use the Docker image. There
is a Docker image `neunhoef/graphutils:latest` which contains the
compiled tools for Linux systems under `/usr/local/bin`. Just run the
Docker image locally and mount the place where your data is into the
Docker container like so:

```bash
docker run -it -v /home/neunhoef/files:/files neunhoef/graphutils bash
```

```
Smartifier2 - transform graph data into smart graph format

Usage:
  smartifier2 vertices --input <input>
                       --output <outputfile>
                       --smart-graph-attribute <smartgraphattr>
                       [ --type <type> ]
                       [ --write-key <bool>]
                       [ --smart-value <smartvalue> ]
                       [ --smart-index <smartindex> ]
                       [ --hash-smart-value <bool> ]
                       [ --separator <separator> ]
                       [ --quote-char <quotechar> ]
                       [ --smart-default <smartdefault> ]
                       [ --randomize-smart <nr> ]
                       [ --rename-column <nr>:<newname> ... ]
                       [ --key-value <name>
  smartifier2 edges --vertices <vertices>... 
                    --edges <edges>...
                    [ --from-attribute <fromattribute> ]
                    [ --to-attribute <toattribute> ]
                    [ --type <type> ]
                    [ --memory <memory> ]
                    [ --separator <separator> ]
                    [ --quote-char <quotechar> ]
                    [ --smart-index <index> ]
                    [ --threads <nrthreads> ]

Options:
  --help (-h)                   Show this screen.
  --version (-v)                Show version.
  --input <input> (-i)          Input file for vertex mode.
  --output <output> (-o)        Output file for vertex mode.
  --smart-graph-attribute <smartgraphattr>  
                                Attribute name of the smart graph attribute.
  --type <type>                 Data type "csv" or "jsonl" [default: csv]
  --write-key                   If present, the `_key` attribute will be
                                written as it is necessary for a
                                smart graph. If not given, the
                                `_key` attribute is not touched or
                                written.
  --memory <memory>             Limit RAM usage in MiB [default: 4096]
  --smart-value <smartvalue>    Attribute name to get the smart graph
                                attribute value from.
  --smart-index <smartindex>    If given, only this many characters are
                                taken from the beginning of the
                                smart value to form the smart graph
                                attribute value.
  --hash-smart-value <bool>     If this is set to `true` the found smart value
                                is first hashed using sha1, a potential smart index
                                is applied after this. The default is `false` to 
                                take the value directly.
  --separator <separator>       Column separator for csv type [default: ,]
  --quote-char <quoteChar>      Quote character for csv type [default: "]
  --smart-default <smartDefault>  If given, this value is taken as the value
                                of the smart graph attribute if it is
                                not given in a document (JSONL only)
  --randomize-smart <nr>        If given, random values are taken randomly
                                from 0 .. <nr> - 1 as smart graph
                                attribute value, unless the
                                attribute is already there.
  --rename-column <nr>:<newname>  Before processing starts, rename column
                                number <nr> to <newname>, only relevant for
                                CSV, can be used multiple times, <nr> is
                                0-based.
  --key-value <name>            Take the key value from the column/attribute
                                named <name>. The `_key` column/attribute
                                will be built using the smart graph
                                attribute value, a colon and the value
                                of the column/attribute named here.

And additionally for edge mode:

  --vertices <vertices>          Vertex data in the form
        <collectionname>:<filename>, can be repeated.
  --edges <edges>                Edge data in the form
        <edgefilename>:<fromvertexcollection>:<tovertexcollection>.
      If needed, append :<columnnumber>:<newcolumnname> pairs to rename
      columns before processing.
  --smart-index <index>          If given here, no vertex data must be
                                 given, and the smart graph attribute
                                 will be the first <index> characters
                                 of the key, so we can transform _from
                                 and _to locally.
  --threads <nrthreads>          Number of threads to use, only relevant
                                 when multiple edge files are given.
```

## Detailed explanation:

The `smartifier2` has two modes: "vertex mode" and "edge" mode. In
vertex mode, it transforms one input file with vertex data into one
(separate) output file. On the way, the smart graph attribute is added
(if it is not already there) and its value is prepended to the primary
key. This is a filtering operation and does not require a lot of RAM.

In edge mode, one or more edge files are rewritten. Basically, the from
and to values need to be rewritten, since the primary keys of the
vertices have changed. Therefore, edge mode needs the vertex collections
as well as the edge collections, and it needs to buffer the vertex key
transformation in RAM, to be able to look up old keys and find the value
of the smart graph attribute. If there is not enough RAM, it has to do
multiple passes for each edge collection. On the other hand, it can use
multiple threads to transform multiple edge collections concurrently.

Here are details about the command line options, we start with vertex
mode, the first three must be given, the rest are optional and have more
or less sensible defaults:

  - `--input` specifies the input file.
  - `--output` specifies the output file, this must be different from
    the input file.
  - `--smart-graph-attribute` is the name of the smart graph attribute,
    in the output, the smart graph attribute will always be present,
    even if it was missing before the transformation.
  - `--type` can be CSV for comma separated values or JSONL for one JSON
    object per line, certain of the following options only apply to the
    CSV case, the default is CSV.
  - `--write-key` is by default `true`. The `false` case is not yet
    implemented. The idea is to not write a `_key`, but this is rarely
    useful, since this prevents the edge transformation to work.
  - `--smart-value` specifies an attribute to get the value of the smart
    graph attribute from. The resulting value is then written into the
    attribute which is given under `--smart-graph-attribute`, even if
    that attribute does not exist in the original data. This is to fetch
    a value for the smart graph attribute from another attribute. This
    often makes sense in connection to the `--smart-index` option, see
    below.
  - `--smart-index` specifies, how many initial characters should be
    taken from the value found in the `--smart-value` attribute. This
    allows, for example, to create the smart graph attribute value from
    the prefix of a different attribute. This can also be used to create
    the smart graph attribute from a prefix of the `_key`.
  - `--hash-smart-value`, if this is set to `true`, the found smart value
    is first SHA1 hashed before it is used. This can be combined with smart
    index, which will then take the given number of characters from the
    hash value.
  - `--separator` specifies the field separator for CSV mode. By
    default, it is a comma `,`. This can only be a single character.
    Note that giving `\t` uses a tab in most shells.
  - `--quote-char` specifies the quote character for CSV mode. A value
    can be put in quotes. If the quote character shows up in the quoted
    string twice in a row, this is translated into a single quote
    character in the result. On output, the quote character is used, if
    the string contains an actual occurrence of the quote character.
  - `--smart-default` specifies the default value for the smart graph
    attribute, if it is for some record not given in the file.
  - `--randomize-smart` is not yet implemented, it will create a random
    string for the smart graph attribute. Details to be determined.
  - `--rename-column` takes a single argument which consists of a row
    number (zero-based) and a new name for the column with that number.
    This can be used to rename a column in CSV mode to `_key` to
    specify, which column is supposed to be the primary key.
  - `--key-value` takes a single argument which consists of a name of a row
    (CSV) or an attribute (JSONL). The key value will be taken from that
    column/attribute. The `_key` column/attribute will be built using
    the smart graph attribute value, a colon and the value of the
    column/attribute named here.

We continue with edge mode:

  - `--vertices` specifies the input vertex collections. The argument
    must contain the name of the vertex collection, then a colon `:`
    and then the file name of the vertex data. The data in the file must
    already be smartified, in the sense that each record has a `_key`
    attribute which contains a colon. Everything before the first
    colon is assumed to be the value of the smart graph attribute,
    everything after the colon is the original key. This is used to
    build the transformation table for `_from` and `_to`. This option can be
    specified multiple times.
  - `--edges` specifies the input and output vertex collections. The
    argument must contain the file name of an edge collection, followed
    by a colon, then the name of the `_from` vertex collection, another
    colon, and then the name of the `_to` vertex collection. These
    collection names are used, if the `_from` value does not yet contain
    a slash character, otherwise, it is assumed that the prefix before
    the slash is the name of the vertex collection and this is left
    unchanged. In case of CSV mode, one can follow with further pairs of
    the form `:<columnnumber>:<newcolumnname>` to rename columns in this
    edge collection file. This is needed to rename one column to `_from`
    and one to `_to` to specify which columns contain the from and the
    to value respectively. These are also the columns which are
    transformed (unless specified differently by `--from-attribute` and
    `--to-attribute`).
  - `--from-attribute` specifies the name of the attribute used as from
    value. This is currently always `_from` and cannot yet be changed.
  - `--to-attribute` specifies the name of the attribute used as to
    value. This is currently always `_to` and cannot yet be changed.
  - `--type` can be CSV for comma separated values or JSONL for one JSON
    object per line, certain of the following options only apply to the
    CSV case, the default is CSV.
  - `--smart-index` here specifies, how many initial characters should be
    taken from the key to define the value of the smart graph attribute.
    If this is used, then no vertex collections need to be given, since
    the transformation can work without a lookup table. This covers an
    important special case of smartifying.
  - `--separator` specifies the field separator for CSV mode. By
    default, it is a comma `,`. This can only be a single character.
  - `--quote-char` specifies the quote character for CSV mode. A value
    can be put in quotes. If the quote character shows up in the quoted
    string twice in a row, this is translated into a single quote
    character in the result. On output, the quote character is used, if
    the string contains an actual occurrence of the quote character.
  - `--memory` specifies the memory limit as a decimal number in
    megabytes. The tool will read as much vertex data as possible with
    the available memory. If this is not enough, it does multiple passes
    through the edge collections.
  - `--threads` specifies how many threads to use. This has only an
    effect, if multiple edge collections are done in the same run.


Worked example for a `smartifier2` usage
-----------------------------------------

Assume a data set like this for the vertices (CSV data):

```
_key,name,country,email,age,gender,address
"111",name1,DE,miller@person1.com,48,F,43 Main Street;Eppelheim;83834
"222",name2,US,meier@person2.com,89,M,52 Butcher Street;New York;81503
"333",name3,US,karl@person3.com,91,F,76 Butcher Street;San Francisco;7540
```

and for the edges (denoting friendship, say):

```
_key,_from,_to
"a",111,222
"b",222,333
```

That is, the person with name `name1` has the person with name `name2` as
friend, which in turn has the person with name `name3` as friend.

We want to use the country attribute as smart graph attribute, assuming
that many people have their friends in the same country. Not all
friendships will be within a country, but this is not necessary!

So for the vertex collection people we have to rewrite the data by
prepending the value of the smart graph attribute and a colon `:` to the
primary key `_key`, that is, we have to rewrite the data to:

```
_key,name,country,email,age,gender,address
"DE:111",name1,DE,miller@person1.com,48,F,43 Main Street;Eppelheim;83834
"US:222",name2,US,meier@person2.com,89,M,52 Butcher Street;New York;81503
"US:333",name3,US,karl@person3.com,91,F,76 Butcher Street;San Francisco;7540
```

This is relatively straightforward and can be done by only streaming
this data file through a converter and changing each line accordingly.
No “memory” is needed. Here is the call to the `smartifier2` to achieve
this:

```
smartifier2 vertices --input person.csv --output person_smart.csv --type csv --smart-graph-attribute country
```


But now look at the edge file. Here, we have to rewrite the `_from` and
`_to` columns. For ArangoDB’s smart graph purposes, we have to do two
things:

 - We have to prepend the name of the vertex collection and a slash /,
   this is necessary to indicate from which vertex collection the vertices
   come. Note that a smart graph can have multiple vertex collections.

 - We have to rewrite the old vertex key to the new vertex key by
   prepending the value of the smart graph attribute and a colon `:`.

Furthermore, if the keys for the edges should be specificed, we have to
massage them into a format such that we can read off the smart graph
attribute of the `_from` vertex as prefix of the key and the value of
the smart graph attribute of the `_to` vertex as suffix of the key, both
separated by a colon. This will allow us to shard the edge collection by
looking at the key only, too.

That is, we want to achieve this:

```
_key,_from,_to
"DE:a:US",person/DE:111,person/US:222
"US:b:US",person/US:222,person/US:333
```

This can be achieved with this `smartifier2` commands. Note that the
edge transformation happens in place, so we first create a copy of the
edges:

```
cp isfriend.csv isfriend_smart.csv
smartifier2 edges --type csv --vertices person:person_smart.csv --edges isfriend_smart.csv:person:person 
```

Please observe that the argument to `--vertices` has the collection name
person as well as the file name, separated by a colon. The argument to
`--edges` has the file name and the default vertex collection names for
`_from` and `_to`, also separated by colons.

Note that if we do not have to specify the keys of the edges, we can
just let ArangoDB generate such keys automatically on import.

This transformation is not difficult, but there is one challenge: To
change the vertex key `1` into `person/DE:1` we need to know that the person
with original key `1` has `DE` as value of his/her smart graph attribute.
Suddenly, this is no longer a trivial streaming transformation, we
suddenly need to store something about the vertices to be able to
perform the rewrite. For small data sets, this is easily done in RAM,
for larger data sets, this is a bit more effort. This is where tools
like the smartifier (see below) come into play.

In any case, after massaging the data like this, it can directly be
imported into a smart graph with a smart edge collection called person
with smart graph attribute country, and edge collection isfriend (say).
Note that such a smart graph would be created in arangosh with the
following commands:

```
sg = require("@arangodb/smart-graph");
sg._create("G",[sg._relation("isfriend",["person"],["person"])], [],
           {isSmart: true, smartGraphAttribute: "country",
            numberOfShards: 3, replicationFactor: 2 });
```

After that, the rewritten data can be imported into ArangoDB with the
following arangoimport commands:

```
arangoimport --server.endpoint ssl://localhost:8529 --collection person --type csv --file person_smart.csv
arangoimport --server.endpoint ssl://localhost:8529 --collection isfriend --type csv --file isfriend_smart.csv
```

Obviously, you have to change the endpoint to your ArangoDB instance and
potentially add authentication credentials.


Overview and usage scenario for `smartifier` Version 1
------------------------------------------------------

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
        arangosh> var G = sg._create("G", [sg._relation("E", ["V"], ["V"])], [], {numberOfShards:3, replicationFactor:2, smartGraphAttribute: "country"})
    arangoimport --server.endpoint tcp://NEWCLUSTER:PORT --input.file export/V.jsonl --type json --collection V
    arangoimport --server.endpoint tcp://NEWCLUSTER:PORT --input.file export/E.jsonl --type json --collection E

Note that the `arangoexport` utility is only available in ArangoDB >= 3.2,
therefore you either have to install one of the prereleases or use the
Docker image. You can use `arangoexport` on an ArangoDB 3.1 without 
a problem. Here is a sample call using Docker to export the two collections
to the local directory `export`:

    docker run -it --net=host -v `pwd`/export:/data arangodb/arangodb-preview:3.2.devel arangoexport --collection V --collection E --output-directory /data --type jsonl


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
                 [--smartDefault=<smartDefault>]
                 <vertexFile> <vertexColl> <edgeFile> <smartGraphAttr>

    Options:
      -h --help                      Show this screen.
      --version                      Show version.
      --type=<type>                  Data type "csv" or "jsonl" [default: csv]
      --separator=<separator>        Column separator for csv type [default: ,]
      --quoteChar=<quoteChar>        Quote character for csv type [default: "]
      --memory=<memory>              Limit RAM usage in MiB [default: 4096]
      --smartDefault=<smartDefault>  If given, this value is taken as the value
                                     of the smart graph attribute if it is
                                     not given in a document (JSONL only)
      <vertexFile>                   File for the vertices.
      <vertexColl>                   Name of vertex collection.
      <edgeFile>                     File for the edges.
      <smartGraphAttr>               Smart graph attribute.

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

