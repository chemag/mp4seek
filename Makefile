all: none
	echo "use cmake to build"


format:
	clang-format -i -style=google src/mp4seek.bento4.cc src/mp4seek.mp4v2.cc
	clang-format -i -style=google src/mp4extract.bento4.cc src/mp4extract.mp4v2.cc
	clang-format -i -style=google include/mp4seek.h include/mp4extract.h
	clang-format -i -style=google tools/mp4seek_main.cc
	clang-format -i -style=google tests/mp4seek_test.cc

# Bento4 build (default)
.PHONY: build-bento4

build-bento4:
	\rm -rf build
	mkdir build
	cd build && CC=clang CXX=clang++ cmake -DCMAKE_BUILD_TYPE=DEBUG -DBUILD_TESTING=ON ..
	cd build && make -j 8

.PHONY: rebuild-bento4

rebuild-bento4:
	cd build && CC=clang CXX=clang++ cmake -DCMAKE_BUILD_TYPE=DEBUG -DBUILD_TESTING=ON ..
	cd build && make -j 8

.PHONY: test-bento4

test-bento4: rebuild-bento4
	./build/mp4seek_test

# mp4v2 build
.PHONY: build-mp4v2

build-mp4v2:
	\rm -rf build
	mkdir build
	cd build && CC=clang CXX=clang++ cmake -DCMAKE_BUILD_TYPE=DEBUG -DBUILD_TESTING=ON -DUSE_MP4V2=ON ..
	cd build && make -j 8

.PHONY: rebuild-mp4v2

rebuild-mp4v2:
	cd build && CC=clang CXX=clang++ cmake -DCMAKE_BUILD_TYPE=DEBUG -DBUILD_TESTING=ON -DUSE_MP4V2=ON ..
	cd build && make -j 8

.PHONY: test-mp4v2

test-mp4v2: rebuild-mp4v2
	./build/mp4seek_test

# Default targets (use bento4)
.PHONY: build

build: build-bento4

.PHONY: rebuild

rebuild: rebuild-bento4

.PHONY: test

test: test-bento4

# Test both backends
.PHONY: test-all

test-all:
	@echo "=== Testing Bento4 backend ==="
	$(MAKE) build-bento4
	./build/mp4seek_test
	@echo ""
	@echo "=== Testing mp4v2 backend ==="
	$(MAKE) build-mp4v2
	./build/mp4seek_test
	@echo ""
	@echo "=== All tests passed ==="
