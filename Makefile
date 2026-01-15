all: none
	echo "use cmake to build"


format:
	clang-format -i -style=google src/mp4seek.cc src/mp4extract.cc
	clang-format -i -style=google include/mp4seek.h include/mp4extract.h
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

