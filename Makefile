test:
	./runTest.sh

docker:
	docker build -t neunhoef/graphutils .
	docker push neunhoef/graphutils
