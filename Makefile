all: smartifier sampleGraphMaker

smartifier:	Makefile smartifier.cpp
	g++ -Wall -O3 -g -o smartifier smartifier.cpp -std=c++11

sampleGraphMaker:	Makefile sampleGraphMaker.cpp
	g++ -Wall -O3 -g -o sampleGraphMaker sampleGraphMaker.cpp -std=c++11

clean:
	rm -f smartifier sampleGraphMaker
