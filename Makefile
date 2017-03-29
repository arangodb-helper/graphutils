test:
	./runTest.sh

docker:
	strip build/sampleGraphMaker build/smartifier
	docker build -t neunhoef/graphutils .
	docker push neunhoef/graphutils
