test: smartifier sampleGraphMaker
	@./sampleGraphMaker testCase/test 10 10 1 >/dev/null
	@./smartifier testCase/test_profiles.csv profiles \
	    testCase/test_relations.csv country 1024 > /dev/null
	@cmp testCase/test_profiles.csv testCase/test_profiles_known.csv
	@cmp testCase/test_relations.csv testCase/test_relations_known.csv
	@rm testCase/test_profiles.csv testCase/test_relations.csv

docker: smartifier sampleGraphMaker
	docker build -t neunhoef/graphutils .
	docker push neunhoef/graphutils
