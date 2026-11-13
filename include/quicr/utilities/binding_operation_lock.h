// SPDX-FileCopyrightText: Copyright (c) 2026 Cisco Systems
// SPDX-License-Identifier: BSD-2-Clause

#pragma once

#include "quicr/utilities/thread_safety.h"

#include <mutex>

namespace quicr {
    class QUICR_CAPABILITY("mutex") BindingOperationMutex
    {
        friend class BindingOperationLock;
        std::mutex mutex_;
    };

    class QUICR_SCOPED_CAPABILITY BindingOperationLock
    {
      public:
        BindingOperationLock(BindingOperationMutex& binding, [[maybe_unused]] std::mutex& session_state)
          QUICR_ACQUIRE(binding) QUICR_EXCLUDES(session_state)
          : lock_(binding.mutex_)
        {
        }

        ~BindingOperationLock() QUICR_RELEASE() = default;

        BindingOperationLock(const BindingOperationLock&) = delete;
        BindingOperationLock& operator=(const BindingOperationLock&) = delete;

      private:
        std::unique_lock<std::mutex> lock_;
    };
}
