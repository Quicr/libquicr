// SPDX-FileCopyrightText: Copyright (c) 2025 Cisco Systems
// SPDX-License-Identifier: BSD-2-Clause

#include "quicr/messages/message_serialisation.h"
#include "quicr/messages/parameters.h"
#include <cstddef>

using namespace quicr;
using namespace quicr::messages;

// Fuzzer entry point
extern "C" int
LLVMFuzzerTestOneInput(const std::uint8_t* data, const std::size_t size)
{
    BytesSpan input_span(data, size);

    // UIntVar decoding.
    try {
        const auto uintvar = UintVar(input_span);
    } catch (...) {
        // This is fine.
    }

    // Message parameter decoding.
    try {
        messages::Parameters params;
        input_span >> params;
    } catch (const std::exception&) {
        // Malformed input is expected.
    }

    // Track namespace decoding.
    try {
        TrackNamespace track_namespace;
        input_span >> track_namespace;
    } catch (const std::exception&) {
        // Malformed input is expected.
    }

    // Key value pair decoding.
    try {
        messages::KeyValuePairs pairs;
        input_span >> pairs;
    } catch (const std::exception&) {
        // Malformed input is expected.
    }

    // Track extensions decoding.
    try {
        messages::TrackExtensions track_extensions;
        input_span >> track_extensions;
    } catch (const std::exception&) {
        // Malformed input is expected.
    }

    // Object extensions decoding.
    try {
        Extensions extensions;
        input_span >> extensions;
    } catch (const std::exception&) {
        // Malformed input is expected.
    }

    // Length-prefixed bytes decoding.
    try {
        Bytes bytes;
        input_span >> bytes;
    } catch (const std::exception&) {
        // Malformed input is expected.
    }

    // Subscription filter decoding.
    try {
        messages::SubscriptionFilter filter;
        input_span >> filter;
    } catch (const std::exception&) {
        // Malformed input is expected.
    }

    // Authorization token decoding.
    try {
        messages::Token token;
        input_span >> token;
    } catch (const std::exception&) {
        // Malformed input is expected.
    }

    // TODO: Add control messages.

    return 0;
}
