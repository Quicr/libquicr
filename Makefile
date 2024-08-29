# SPDX-FileCopyrightText: Copyright (c) 2024 Cisco Systems
# SPDX-License-Identifier: BSD-2-Clause

# This is just a convenience Makefile to avoid having to remember
# all the CMake commands and their arguments.

# Set CMAKE_GENERATOR in the environment to select how you build, e.g.:
#   CMAKE_GENERATOR=Ninja

BUILD_DIR=build
export MERMAID_FILTER_THEME=neutral
CLANG_FORMAT=clang-format -i

.PHONY: all clean cclean format tidy

all: ${BUILD_DIR}
	cmake --build ${BUILD_DIR} --parallel 8

${BUILD_DIR}: CMakeLists.txt cmd/CMakeLists.txt
	cmake -B${BUILD_DIR} -DBUILD_TESTING=TRUE -DQUICR_BUILD_TESTS=ON -DCMAKE_BUILD_TYPE=Debug -DUSE_MBEDTLS=OFF .

tidy: CMakeLists.txt cmd/CMakeLists.txt
	cmake -B${BUILD_DIR} -DBUILD_TESTING=TRUE -DQUICR_BUILD_TESTS=ON -DCMAKE_BUILD_TYPE=Debug -DCLANG_TIDY=ON .

ci: CMakeLists.txt cmd/CMakeLists.txt
	cmake -B${BUILD_DIR} -DCLANG_TIDY=ON -DCMAKE_BUILD_TYPE=Debug -DBUILD_TESTING=ON -DBUILD_BENCHMARKING=ON

cert:
	@echo "Creating certificate in ${BUILD_DIR}/cmd/examples"
	@openssl req -nodes -x509 -newkey rsa:2048 -days 365 \
        -subj "/C=US/ST=CA/L=San Jose/O=Cisco/CN=test.m10x.org" \
        -keyout ${BUILD_DIR}/cmd/examples/server-key.pem -out ${BUILD_DIR}/cmd/examples/server-cert.pem
test: ci
	cmake --build ${BUILD_DIR}
	ctest --test-dir ${BUILD_DIR} --output-on-failure

clean:
	cmake --build ${BUILD_DIR} --target clean

cclean:
	rm -rf ${BUILD_DIR}

doc:
	@echo "Creating Doxygen Docs"
	@doxygen
	@echo "Creating implementation doc HTML"
	@pandoc docs/moqt-api-process-flows.md -f markdown -o docs/moqt-api-process-flows.html --filter=mermaid-filter


format:
	find include -iname "*.h" -or -iname "*.cpp" | xargs ${CLANG_FORMAT}
	find src -iname "*.h" -or -iname "*.cpp" | xargs ${CLANG_FORMAT}
	find test -iname "*.h" -or -iname "*.cpp" | xargs ${CLANG_FORMAT}
	find cmd -iname "*.h" -or -iname "*.cpp" | xargs ${CLANG_FORMAT}

lint:
	reuse lint

sbom:
	reuse spdx -o libquicr.spdx

