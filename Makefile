all: smartifier sampleGraphMaker

smartifier:	Makefile smartifier.cpp
	g++ -Wall -O3 -g -o smartifier smartifier.cpp -std=c++11

sampleGraphMaker:	Makefile sampleGraphMaker.cpp
	g++ -Wall -O3 -g -o sampleGraphMaker sampleGraphMaker.cpp -std=c++11

test: smartifier sampleGraphMaker
	@./sampleGraphMaker testCase/test 10 10 1 >/dev/null
	@./smartifier testCase/test_profiles.csv testCase/test_relations.csv \
	    country 1 > /dev/null
	@cmp testCase/test_profiles.csv testCase/test_profiles_known.csv
	@cmp testCase/test_relations.csv testCase/test_relations_known.csv
	@rm testCase/test_profiles.csv testCase/test_relations.csv

clean:
	rm -f smartifier sampleGraphMaker
