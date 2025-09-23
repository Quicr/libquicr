# SPDX-FileCopyrightText: Copyright (c) 2024 Cisco Systems
# SPDX-License-Identifier: BSD-2-Clause

# This is just a convenience Makefile to avoid having to remember
# all the CMake commands and their arguments.

# Set CMAKE_GENERATOR in the environment to select how you build, e.g.:
#   CMAKE_GENERATOR=Ninja

BUILD_DIR=build
INTEGRATION_TEST_BUILD_DIR=${BUILD_DIR}/test/integration_test
FUZZ_BUILD_DIR=${BUILD_DIR}/test/fuzz
BENCHMARK_BUILD_DIR=${BUILD_DIR}/benchmark

export MERMAID_FILTER_THEME=neutral
CLANG_FORMAT=clang-format -i

.PHONY: all clean cclean format fuzz

# Build.
all: ${BUILD_DIR}
	cmake --build ${BUILD_DIR}  --parallel 8

# Standard development CMake generation.
${BUILD_DIR}: CMakeLists.txt cmd/CMakeLists.txt
	cmake -B${BUILD_DIR} -DCMAKE_POLICY_VERSION_MINIMUM=3.5 -DBUILD_TESTING=TRUE -DQUICR_BUILD_TESTS=ON -DCMAKE_BUILD_TYPE=Debug -DUSE_MBEDTLS=OFF -DLINT=OFF .

# Run fuzzing tests.
fuzz:
	cmake -DCMAKE_CXX_COMPILER=clang++ -DCMAKE_C_COMPILER=clang -B${BUILD_DIR} -DCMAKE_POLICY_VERSION_MINIMUM=3.5 -DBUILD_TESTING=TRUE -DQUICR_BUILD_TESTS=ON -DQUICR_BUILD_FUZZ=ON .
	cmake --build ${BUILD_DIR} --parallel 8
	./${FUZZ_BUILD_DIR}/ctrl_messages_fuzzer -max_total_time=10

# Mimic a CI build.
ci: CMakeLists.txt examples/CMakeLists.txt
	cmake -B${BUILD_DIR} -DLINT=ON -DCMAKE_BUILD_TYPE=Debug -DBUILD_TESTING=ON -DQUICR_BUILD_TESTS=ON -Dquicr_BUILD_BENCHMARKS=ON -Dquicr_BUILD_EXAMPLES=ON

# Generate self-signed certificates.
cert:
	@echo "Creating certificate in ${BUILD_DIR}test/integration_test"
	@openssl req -nodes -x509 -newkey rsa:2048 -days 365 \
        -subj "/C=US/ST=CA/L=San Jose/O=Cisco/CN=test.m10x.org" \
        -keyout ${INTEGRATION_TEST_BUILD_DIR}/test-key.pem -out ${INTEGRATION_TEST_BUILD_DIR}/test-cert.pem

# Run the tests.
test: ci cert
	cmake --build ${BUILD_DIR}
	ctest --test-dir ${BUILD_DIR} --output-on-failure

# Run the benchmarks
bench: ci cert
	cmake --build ${BUILD_DIR}
	./${BENCHMARK_BUILD_DIR}/quicr_benchmark

# Clean all built targets.
clean:
	cmake --build ${BUILD_DIR} --target clean

# Delete the build folder.
cclean:
	rm -rf ${BUILD_DIR}

# Generate documentation.
doc:
	@echo "Creating Doxygen Docs"
	@doxygen
	@echo "Creating implementation doc HTML"
	@pandoc docs/api-guide.md -f markdown --to=html5 -o docs/html/api-guide.html --filter=mermaid-filter \
 			--template=docs/pandoc-theme/elegant_bootstrap_menu.html --toc

# Format code.
format:
	find include -iname "*.h" -or -iname "*.cpp" | xargs ${CLANG_FORMAT}
	find src -iname "*.h" -or -iname "*.cpp" | xargs ${CLANG_FORMAT}
	find test -iname "*.h" -or -iname "*.cpp" | xargs ${CLANG_FORMAT}
	find examples -iname "*.h" -or -iname "*.cpp" -or -iname "*.cc" -or -iname "*.hpp" | xargs ${CLANG_FORMAT}
	find benchmark -name "*.h" -or -iname "*.cpp" | xargs ${CLANG_FORMAT}
	find tools -iname "*.h" -or -iname "*.cpp" | xargs ${CLANG_FORMAT}

lint:
	reuse lint

sbom:
	reuse spdx -o libquicr.spdx

