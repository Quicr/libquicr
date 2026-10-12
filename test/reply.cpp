// SPDX-FileCopyrightText: Copyright (c) 2024 Cisco Systems
// SPDX-License-Identifier: BSD-2-Clause

#include "quicr/reply.h"

#include <doctest/doctest.h>

#include <functional>
#include <memory>
#include <utility>
#include <vector>

namespace {
    class TestTransport : public quicr::Transport
    {
      public:
        quicr::TransportStatus Status() const override { return quicr::TransportStatus::kReady; }
        std::shared_ptr<quicr::Connection> Start() override { return nullptr; }
        void Close(const std::shared_ptr<quicr::Connection>&, quicr::AppReasonForClose) override {}
        void CloseStream(const std::shared_ptr<quicr::Connection>&,
                         const std::shared_ptr<quicr::Stream>&,
                         quicr::StreamOperation) override
        {
        }
        bool GetPeerAddrInfo(const std::shared_ptr<quicr::Connection>&, sockaddr_storage*) override { return false; }
        quicr::TransportError Enqueue(const std::shared_ptr<quicr::Connection>&,
                                      const std::shared_ptr<quicr::Stream>&,
                                      std::shared_ptr<const std::vector<uint8_t>>,
                                      uint8_t,
                                      uint32_t,
                                      EnqueueFlags) override
        {
            return quicr::TransportError::kNone;
        }
        quicr::TransportError EnqueueDatagram(const std::shared_ptr<quicr::Connection>&,
                                              std::shared_ptr<const std::vector<uint8_t>>,
                                              uint8_t,
                                              uint32_t) override
        {
            return quicr::TransportError::kNone;
        }
        std::shared_ptr<const std::vector<uint8_t>> Dequeue(const std::shared_ptr<quicr::Connection>&) override
        {
            return nullptr;
        }
        int CloseWebTransportSession(const std::shared_ptr<quicr::Connection>&, uint32_t, const char*) override
        {
            return 0;
        }
        int DrainWebTransportSession(const std::shared_ptr<quicr::Connection>&) override { return 0; }
        std::shared_ptr<quicr::Stream> CreateDataStream(const std::shared_ptr<quicr::Connection>&, uint8_t) override
        {
            return nullptr;
        }
        std::shared_ptr<quicr::Stream> CreateControlStream(const std::shared_ptr<quicr::Connection>&) override
        {
            return nullptr;
        }
        std::shared_ptr<quicr::Stream> CreateRequestStream(const std::shared_ptr<quicr::Connection>&) override
        {
            return nullptr;
        }

        std::size_t QueuedReplyCount() const { return queued_replies_.size(); }

        void RunQueuedReply()
        {
            auto reply = std::move(queued_replies_.front());
            queued_replies_.erase(queued_replies_.begin());
            reply();
        }

      private:
        void QueueDeferredReply(std::function<void()>&& reply_handler) override
        {
            queued_replies_.push_back(std::move(reply_handler));
        }

        std::vector<std::function<void()>> queued_replies_;
    };
}

TEST_CASE("Reply::Defer queues its completed result on the transport")
{
    using Reply = quicr::Reply<int, quicr::ErrorCode>;

    Reply::CompletionType completion;
    auto reply = Reply::Defer([&completion](auto&& complete) { completion = std::move(complete); });
    auto transport = std::make_shared<TestTransport>();
    int resolved_value = 0;

    reply.Resolve(transport, [&resolved_value](const Reply::ResultType& result) {
        REQUIRE(result.has_value());
        resolved_value = result.value();
    });

    REQUIRE(completion);
    CHECK_EQ(transport->QueuedReplyCount(), 0);
    CHECK_EQ(resolved_value, 0);

    completion(42);

    CHECK_EQ(transport->QueuedReplyCount(), 1);
    CHECK_EQ(resolved_value, 0);

    transport->RunQueuedReply();

    CHECK_EQ(transport->QueuedReplyCount(), 0);
    CHECK_EQ(resolved_value, 42);
}
