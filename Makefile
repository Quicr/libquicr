# This is just a convenience Makefile to avoid having to remember
# all the CMake commands and their arguments.

# Set CMAKE_GENERATOR in the environment to select how you build, e.g.:
#   CMAKE_GENERATOR=Ninja

BUILD_DIR=build
CLANG_FORMAT=clang-format -i

.PHONY: all tidy test libs test-libs test-all gen example everything clean cclean format

all: ${BUILD_DIR}
	cmake --build ${BUILD_DIR}

${BUILD_DIR}: CMakeLists.txt cmd/CMakeLists.txt
	cmake -B${BUILD_DIR} -DCMAKE_BUILD_TYPE=Debug .

libs: ${BUILD_DIR}
	cmake --build ${BUILD_DIR} --target metrics

test-libs: ${BUILD_DIR}
	cmake --build ${BUILD_DIR} --target metrics_test
	cd build/lib/metrics/test/ && ctest

test: ${BUILD_DIR} test/*

	cmake --build ${BUILD_DIR} --target quicr_test 

tidy:
	cmake -B${BUILD_DIR} -DCLANG_TIDY=ON -DCMAKE_BUILD_TYPE=Debug .

vcpkg-status:
	less build/vcpkg_installed/vcpkg/status

everything: ${BUILD_DIR}
	cmake --build ${BUILD_DIR}

clean:
	cmake --build ${BUILD_DIR} --target clean

cclean:
	rm -rf ${BUILD_DIR}

format:
	find include -iname "*.h" -or -iname "*.cpp" | xargs ${CLANG_FORMAT}
	find src -iname "*.h" -or -iname "*.cpp" | xargs ${CLANG_FORMAT}
	find test -iname "*.h" -or -iname "*.cpp" | xargs ${CLANG_FORMAT}
