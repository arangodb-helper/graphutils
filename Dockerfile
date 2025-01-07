FROM ubuntu:24.04
LABEL MAINTAINER="Max Neunhoeffer <max@arangodb.com>"

ADD build/sampleGraphMaker build/smartifier build/smartifier2 /usr/local/bin
