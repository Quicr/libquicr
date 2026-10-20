// SPDX-FileCopyrightText: Copyright (c) 2024 Cisco Systems
// SPDX-License-Identifier: BSD-2-Clause

#pragma once

#include "quicr/errors.h"
#include "quicr/transport.h"
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
        /// @brief Immediate success-or-error result represented by this reply.
        using ResultType = Expected<T, Error<E>>;

        /// @brief Continuation invoked with the completed result.
        using CompletionType = std::function<void(ResultType)>;

        /// @brief Deferred producer that must invoke its completion exactly once.
        using DeferType = std::function<void(CompletionType&&)>;

      private:
        Reply(DeferType&& action)
          : result_(std::in_place_type<DeferType>, std::move(action))
        {
        }

        bool IsDeferred() const noexcept { return std::holds_alternative<DeferType>(result_); }

      public:
        /// @brief Construct an immediate default result.
        Reply() = default;

        /// @brief Replies are move-only because resolving consumes their state.
        /// @param other Reply that would be copied.
        Reply(const Reply& other) = delete;

        /// @brief Move a reply, transferring its unresolved result or deferred action.
        /// @param other Reply to move.
        Reply(Reply&& other) noexcept = default;

        /// @brief Replies cannot be copy-assigned.
        /// @param other Reply that would be copied.
        Reply& operator=(const Reply& other) = delete;

        /// @brief Move-assign a reply, transferring its unresolved state.
        /// @param other Reply to move.
        /// @return This reply.
        Reply& operator=(Reply&& other) noexcept = default;

        /**
         * @brief Construct an immediate reply from anything the result is constructible from, such as a value,
         *      or an Unexpected error.
         *
         * @tparam U Source type used to construct `ResultType`.
         * @param value Successful value or unexpected error to store.
         */
        template<typename U>
            requires(!std::same_as<std::remove_cvref_t<U>, Reply> && std::is_constructible_v<ResultType, U &&>)
        Reply(U&& value)
          : result_(std::in_place_type<ResultType>, std::forward<U>(value))
        {
        }

        /**
         * @brief Construct a reply whose result is produced later, off the calling thread.
         *
         * @details The action owns the completion callback passed to it and must invoke that callback exactly
         *      once. The action itself is invoked synchronously by `Resolve`; it must arrange any desired
         *      off-thread work.
         *
         * @param action Producer that eventually supplies the reply result.
         * @return A deferred reply owning @p action.
         */
        static Reply Defer(DeferType&& action) { return Reply(std::move(action)); }

        /**
         * @brief Hand the result to the given continuation, either now or once the deferred action completes.
         *
         * @details Immediate continuations run on the calling thread. A deferred completion is queued through
         *      @p transport and runs on the transport notification thread. Capturing the transport keeps it
         *      alive until completion is queued.
         *
         * @tparam F Callable accepting `ResultType`.
         * @param transport Transport used to marshal a deferred completion to its notification thread; must
         *      remain usable and must not be null.
         * @param f Continuation invoked with the result.
         * @pre This reply has not previously been resolved.
         * @note Consumes the stored state; resolving the same reply more than once is invalid.
         */
        template<typename F>
        void Resolve(const std::shared_ptr<Transport>& transport, F&& f)
        {
            if (IsDeferred()) {
                auto action = std::move(std::get<DeferType>(result_));
                action([transport, f = std::forward<F>(f)](ResultType reply) mutable {
                    transport->QueueDeferredReply(
                      [result = std::move(reply), f = std::forward<F>(f)]() mutable { f(result); });
                });
                return;
            }

            f(std::move(std::get<ResultType>(result_)));
        }

      private:
        std::variant<ResultType, DeferType> result_;
    };
}
