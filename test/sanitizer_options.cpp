// SPDX-FileCopyrightText: Copyright (c) 2026 Cisco Systems
// SPDX-License-Identifier: BSD-2-Clause

// NOLINTBEGIN(readability-identifier-naming)

extern "C" const char*
__asan_default_options()
{
    return "detect_leaks=1";
}

extern "C" const char*
__ubsan_default_options()
{
    return "print_stacktrace=1";
}

extern "C" const char*
__lsan_default_suppressions()
{
    // The client owns the WebTransport h3 context but never frees it.
    // Freeing it properly needs DeregisterWebTransport and CloseInternal
    // untangled and refactored.
    return "leak:picowt_connect_ex\n"
           "leak:picowt_prepare_client_cnx\n"
           "leak:picowt_create_stream_ctx\n"
           "leak:picowt_set_control_stream\n"
           "leak:h3zero_find_or_create_stream\n";
}

// NOLINTEND(readability-identifier-naming)
