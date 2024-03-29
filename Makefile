all: make

make:
	cd build ; cmake --build . -- -j 64 ; cd ..

normal:
	rm -rf build ; mkdir -p build ; cd build ; ../cmakung -DCMAKE_BUILD_TYPE=RelWithDebInfo .. ; cmake --build . -- -j 64 ; cd ..

debug:
	rm -rf build ; mkdir -p build ; cd build ; ../cmakung -DCMAKE_BUILD_TYPE=Debug .. ; cmake --build . -- -j 64 ; cd ..

asan:
	rm -rf build ; mkdir -p build ; cd build ; ../cmakung -DCMAKE_CXX_FLAGS="-fsanitize=address -fno-omit-frame-pointer" -DCMAKE_BUILD_TYPE=Debug .. ; cmake --build . -- -j 64 ; cd ..

test: normal
	./runTest.sh
	build/smartifier2 --test
	./testRunner.sh

coverage:
	rm -rf build ; mkdir -p build ; cd build ; ../cmakung -DCMAKE_CXX_FLAGS="--coverage" -DCMAKE_EXE_LINKER_FLAGS="--coverage" -DCMAKE_BUILD_TYPE=Debug .. -DCOVERAGE=1 ; cmake --build . -- -j 64 ; make tests_coverage ; cd ..

docker:
	strip build/sampleGraphMaker build/smartifier
	docker build -t neunhoef/graphutils .
	docker push neunhoef/graphutils
