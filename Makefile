# SPDX-FileCopyrightText: Copyright (c) 2024 Cisco Systems
# SPDX-License-Identifier: BSD-2-Clause

# This is just a convenience Makefile to avoid having to remember
# all the CMake commands and their arguments.

# Set CMAKE_GENERATOR in the environment to select how you build, e.g.:
#   CMAKE_GENERATOR=Ninja

BUILD_DIR=build
PACKAGE_LOCK_BUILD_DIR?=${BUILD_DIR}-package-lock
export MERMAID_FILTER_THEME=neutral
CLANG_FORMAT=clang-format -i

.PHONY: all clean cclean format fuzz c-bridge c-bridge-only cert licenses package-lock

# Build.
all: ${BUILD_DIR}
	cmake --build ${BUILD_DIR}  --parallel 8

# Standard development CMake generation.
${BUILD_DIR}: CMakeLists.txt
	cmake -B${BUILD_DIR} -DCMAKE_POLICY_VERSION_MINIMUM=3.5 -DBUILD_TESTING=TRUE -DQUICR_BUILD_TESTS=ON -DQUICR_BUILD_C_BRIDGE=ON -DQUICR_BUILD_EXAMPLES=ON -DQUICR_BUILD_TOOLS=ON -DCMAKE_BUILD_TYPE=Debug -DWITH_MBEDTLS=OFF -DLINT=OFF .

# Run fuzzing tests.
fuzz:
	cmake -DCMAKE_CXX_COMPILER=clang++ -DCMAKE_C_COMPILER=clang -B${BUILD_DIR} -DCMAKE_POLICY_VERSION_MINIMUM=3.5 -DQUICR_BUILD_FUZZ=ON .
	cmake --build ${BUILD_DIR} --parallel 8
	./${BUILD_DIR}/fuzz/ctrl_messages_fuzzer -max_total_time=10

# Run the tests.
test: ${BUILD_DIR}
	cmake --build ${BUILD_DIR}
	ctest --test-dir ${BUILD_DIR} --output-on-failure --parallel

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

# Build C Bridge only.
c-bridge-only: CMakeLists.txt c-bridge/CMakeLists.txt
	cmake -B${BUILD_DIR} -DCMAKE_POLICY_VERSION_MINIMUM=3.5 -DBUILD_TESTING=FALSE -DQUICR_BUILD_TESTS=OFF -DQUICR_BUILD_C_API=OFF -DQUICR_BUILD_C_BRIDGE=ON -DCMAKE_BUILD_TYPE=Release -DWITH_MBEDTLS=OFF -DLINT=OFF .
	cmake --build ${BUILD_DIR} --target quicr-bridge bridge_examples --parallel 8

# Build with C Bridge enabled.
c-bridge: ${BUILD_DIR}
	cmake --build ${BUILD_DIR} --target quicr-bridge bridge_examples --parallel 8

# Generate third-party license notices for distributed CPM dependencies.
licenses:
	cmake -B${BUILD_DIR} -DQUICR_GENERATE_THIRD_PARTY_LICENSES=ON .
	cmake --build ${BUILD_DIR} --target third-party-licenses
	@echo "Third-party notices: ${BUILD_DIR}/THIRD_PARTY_LICENSES.txt"

# Update the CPM package lock with all non-fuzz build configurations enabled.
package-lock:
	cmake -S . -B "${PACKAGE_LOCK_BUILD_DIR}" -DBUILD_TESTING=ON -DQUICR_BUILD_TESTS=ON -DQUICR_BUILD_BENCHMARKS=ON -DQUICR_BUILD_C_BRIDGE=ON -DQUICR_BUILD_EXAMPLES=ON -DQUICR_BUILD_TOOLS=ON -DQUICR_GENERATE_THIRD_PARTY_LICENSES=ON -DQUICR_BUILD_FUZZ=OFF -DLINT=OFF
	cmake --build "${PACKAGE_LOCK_BUILD_DIR}" --target cpm-update-package-lock
	@echo "Updated package-lock.cmake"

# Format code.
format:
	find include -iname "*.h" -or -iname "*.cpp" | xargs ${CLANG_FORMAT}
	find src -iname "*.h" -or -iname "*.cpp" | xargs ${CLANG_FORMAT}
	find src/moq -iname "*.h" -or -iname "*.cpp" | xargs ${CLANG_FORMAT}
	find src/quic -iname "*.h" -or -iname "*.cpp" | xargs ${CLANG_FORMAT}
	find test -iname "*.h" -or -iname "*.cpp" | xargs ${CLANG_FORMAT}
	find cmd -iname "*.h" -or -iname "*.cpp" -or -iname "*.cc" -or -iname "*.hpp" | xargs ${CLANG_FORMAT}
	find benchmark -name "*.h" -or -iname "*.cpp" | xargs ${CLANG_FORMAT}
	find tools -iname "*.h" -or -iname "*.cpp" | xargs ${CLANG_FORMAT}
	find c-bridge -iname "*.h" -or -iname "*.c" -or -iname "*.cpp" | xargs ${CLANG_FORMAT}

lint:
	reuse lint

sbom:
	reuse spdx -o libquicr.spdx
