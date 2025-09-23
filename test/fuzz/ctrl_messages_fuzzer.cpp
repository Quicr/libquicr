// SPDX-FileCopyrightText: Copyright (c) 2025 Cisco Systems
// SPDX-License-Identifier: BSD-2-Clause

#include "quicr/messages.h"
#include <cstddef>

using namespace quicr;

// Fuzzer entry point
extern "C" int
LLVMFuzzerTestOneInput(const std::uint8_t* data, const std::size_t size)
{
    UnownedBytes input_span(data, size);

    // UIntVar decoding.
    try {
        const auto uintvar = UintVar(input_span);
    } catch (...) {
        // This is fine.
    }

    // TODO: Add control messages.

    return 0;
}
