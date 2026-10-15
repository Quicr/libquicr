// SPDX-FileCopyrightText: Copyright (c) 2024 Cisco Systems
// SPDX-License-Identifier: BSD-2-Clause

#include "signal_handler.h"

#include <atomic>
#include <csignal>

namespace {
    volatile std::sig_atomic_t pending_signal = 0;

    void SignalHandler(int signal) noexcept
    {
        pending_signal = signal;
    }
}

namespace moq_example {
    std::mutex main_mutex;
    std::atomic<bool> terminate{ false };
    std::atomic<bool> connected{ false };
    std::condition_variable cv;
    std::atomic<const char*> termination_reason{ nullptr };

    bool InstallSignalHandlers()
    {
        return std::signal(SIGINT, SignalHandler) != SIG_ERR && std::signal(SIGTERM, SignalHandler) != SIG_ERR;
    }

    int ConsumePendingSignal() noexcept
    {
        const int signal = pending_signal;
        pending_signal = 0;
        return signal;
    }

    const char* SignalReason(int signal) noexcept
    {
        switch (signal) {
            case SIGINT:
                return "Interrupt signal received";
            case SIGTERM:
                return "Termination signal received";
            default:
                return "Unknown signal received";
        }
    }
}
