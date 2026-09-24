// SPDX-FileCopyrightText: Copyright (c) 2024 Cisco Systems
// SPDX-License-Identifier: BSD-2-Clause

#pragma once

#include "quicr/errors.h"
#include "quicr/utilities/expected.h"

#include <concepts>
#include <functional>
#include <thread>
#include <type_traits>
#include <utility>
#include <variant>

namespace quicr {

    /**
     * @brief The reply to a callback, which may be answered immediately or deferred.
     *
     * @details A Reply either holds the result of the callback, or an action that will produce it. A deferred
     *      action is run off the calling thread, which lets an application answer a callback without blocking
     *      the session's message handling.
     *
     * @tparam T The value type of a successful reply.
     * @tparam E The reason code type of a failed reply.
     */
    template<typename T, typename E>
    class Reply
    {
      public:
        using ResultType = Expected<T, Error<E>>;
        using DeferType = std::function<ResultType()>;

      private:
        Reply(DeferType&& action)
          : result_(std::in_place_type<DeferType>, std::move(action))
        {
        }

        bool IsDeferred() const noexcept { return std::holds_alternative<DeferType>(result_); }

      public:
        Reply() = default;

        Reply(const Reply& other) = delete;

        Reply(Reply&& other) noexcept = default;

        Reply& operator=(const Reply& other) = delete;

        Reply& operator=(Reply&& other) noexcept = default;

        /**
         * @brief Construct an immediate reply from anything the result is constructible from, such as a value,
         *      or an Unexpected error.
         */
        template<typename U>
            requires(!std::same_as<std::remove_cvref_t<U>, Reply> && std::is_constructible_v<ResultType, U &&>)
        Reply(U&& value)
          : result_(std::in_place_type<ResultType>, std::forward<U>(value))
        {
        }

        /**
         * @brief Construct a reply whose result is produced later, off the calling thread.
         */
        static Reply Defer(DeferType&& action) { return Reply(std::move(action)); }

        /**
         * @brief Hand the result to the given continuation, either now or once the deferred action completes.
         *
         * @note Consumes the reply; it must not be resolved more than once.
         */
        template<typename F>
        void Resolve(F&& f)
        {
            if (IsDeferred()) {
                std::thread([action = std::move(std::get<DeferType>(result_)), f = std::forward<F>(f)]() mutable {
                    try {
                        f(action());
                    } catch (...) {
                    }
                }).detach();
                return;
            }

            f(std::move(std::get<ResultType>(result_)));
        }

      private:
        std::variant<ResultType, DeferType> result_;
    };
}
