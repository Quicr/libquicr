# SPDX-FileCopyrightText: Copyright (c) 2026 Cisco Systems
# SPDX-License-Identifier: BSD-2-Clause

CPMAddPackage("gh:jarro2783/cxxopts@3.3.1")

if(TARGET fmt::fmt)
    set(SPDLOG_FMT_EXTERNAL ON)
endif()

CPMAddPackage("gh:gabime/spdlog@1.17.0")

add_subdirectory(moq-capture)
add_subdirectory(moq-probe)
