all: none
	echo "use cmake to build"


format:
	clang-format -i -style=google src/mp4seek.cc include/mp4seek.h
	clang-format -i -style=google tools/mp4seek_main.cc

.PHONY: build

build:
	\rm -rf build
	mkdir build
	cd build && CC=clang CXX=clang++ cmake -DCMAKE_BUILD_TYPE=DEBUG ..
	cd build && make -j 8

.PHONY: rebuild

rebuild:
	cd build && CC=clang CXX=clang++ cmake -DCMAKE_BUILD_TYPE=DEBUG ..
	cd build && make -j 8

