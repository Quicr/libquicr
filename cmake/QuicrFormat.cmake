# SPDX-FileCopyrightText: Copyright (c) 2026 Cisco Systems
# SPDX-License-Identifier: BSD-2-Clause

include_guard(GLOBAL)

function(quicr_configure_format)
    set(CMAKE_CXX_STANDARD 20)
    set(CMAKE_CXX_STANDARD_REQUIRED ON)

    include(CheckCXXSourceCompiles)
    check_cxx_source_compiles("
        #include <chrono>
        #include <format>
        #include <string>
        int main() {
            std::string fmt = \"Hello {}\";
            std::string result = std::vformat(fmt, std::make_format_args(\"World\"));
            const auto now = std::chrono::floor<std::chrono::microseconds>(
                std::chrono::system_clock::now());
            result += std::format(\"{:%F %T}\", now);
            return result.empty() ? 1 : 0;
        }
    " QUICR_HAS_STD_FORMAT)

    if(QUICR_HAS_STD_FORMAT)
        add_compile_definitions("QUICR_HAS_STD_FORMAT")
        message(STATUS "Using std::format")
    else()
        message(STATUS "Using fmt::format")
        CPMAddPackage("gh:fmtlib/fmt#12.2.0")
    endif()
endfunction()

quicr_configure_format()
