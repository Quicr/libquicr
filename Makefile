# This is just a convenience Makefile to avoid having to remember
# all the CMake commands and their arguments.

# Set CMAKE_GENERATOR in the environment to select how you build, e.g.:
#   CMAKE_GENERATOR=Ninja

BUILD_DIR=build
CLANG_FORMAT=clang-format -i

.PHONY: all clean cclean format tidy

all: ${BUILD_DIR}
	cmake --build ${BUILD_DIR}

${BUILD_DIR}: CMakeLists.txt cmd/CMakeLists.txt
	cmake -B${BUILD_DIR} -DBUILD_TESTING=TRUE -DQUICR_BUILD_TESTS=ON -DCMAKE_BUILD_TYPE=Debug .

tidy: CMakeLists.txt cmd/CMakeLists.txt
	cmake -B${BUILD_DIR} -DBUILD_TESTING=TRUE -DQUICR_BUILD_TESTS=ON -DCMAKE_BUILD_TYPE=Debug -DCLANG_TIDY=ON .

ci: CMakeLists.txt cmd/CMakeLists.txt
	cmake -B${BUILD_DIR} -DCLANG_TIDY=ON -DCMAKE_BUILD_TYPE=Debug -DBUILD_TESTING=ON -DBUILD_BENCHMARKING=ON

clean:
	cmake --build ${BUILD_DIR} --target clean

cclean:
	rm -rf ${BUILD_DIR}

format:
	find include -iname "*.h" -or -iname "*.cpp" | xargs ${CLANG_FORMAT}
	find src -iname "*.h" -or -iname "*.cpp" | xargs ${CLANG_FORMAT}
	find test -iname "*.h" -or -iname "*.cpp" | xargs ${CLANG_FORMAT}
	find cmd -iname "*.h" -or -iname "*.cpp" | xargs ${CLANG_FORMAT}
