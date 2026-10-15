# SPDX-FileCopyrightText: Copyright (c) 2026 Cisco Systems
# SPDX-License-Identifier: BSD-2-Clause

if(CMAKE_CXX_COMPILER_ID MATCHES "^(AppleClang|Clang)$")
    include(CheckCXXSourceCompiles)
    include(CMakePushCheckState)

    cmake_push_check_state(RESET)
    set(CMAKE_REQUIRED_FLAGS "-std=c++20 -Wthread-safety -Werror=thread-safety")
    set(CMAKE_REQUIRED_DEFINITIONS -D_LIBCPP_ENABLE_THREAD_SAFETY_ANNOTATIONS)
    check_cxx_source_compiles("
        #include <mutex>

        class ThreadSafetyProbe {
            std::mutex mutex_;
            int value_ __attribute__((guarded_by(mutex_))) = 0;

        public:
            void Access() {
                std::lock_guard<std::mutex> lock(mutex_);
                ++value_;
            }
        };

        int main() {
            ThreadSafetyProbe probe;
            probe.Access();
        }
    " QUICR_HAS_THREAD_SAFETY_ANNOTATIONS)
    cmake_pop_check_state()
endif()
