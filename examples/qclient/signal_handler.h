// SPDX-FileCopyrightText: Copyright (c) 2024 Cisco Systems
// SPDX-License-Identifier: BSD-2-Clause

#pragma once

#include <atomic>
#include <condition_variable>
#include <mutex>

namespace moq_example {
    extern std::mutex main_mutex;
    extern std::atomic<bool> terminate;
    extern std::atomic<bool> connected;
    extern std::condition_variable cv;
    extern std::atomic<const char*> termination_reason;

    bool
    installSignalHandlers();

    int
    consumePendingSignal() noexcept;

    const char*
    signalReason(int signal) noexcept;
}
