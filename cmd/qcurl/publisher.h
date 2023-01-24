/*
 *  publisher.h
 *
 *  Copyright (C) 2023
 *  Cisco Systems, Inc.
 *  All Rights Reserved.
 *
 *  Description:
 *      This module implements logic to act as a quicr publisher.
 *
 *  Portability Issues:
 *      None.
 */

#pragma once

#include <thread>
#include <mutex>
#include <functional>
#include <memory>
#include "quicr/quicr_client.h"

using namespace quicr;
using namespace qtransport;

// Define the Publisher object
class Publisher : public quicr::PublisherDelegate
{
    public:
        Publisher(std::function<void(void)> task_complete);
        ~Publisher();

        void Run(std::shared_ptr<quicr::QuicRClient> &client);

    protected:
        virtual void onPublishIntentResponse(
                                const quicr::QUICRNamespace& quicr_namespace,
                                const quicr::PublishIntentResult& result);
        virtual void onPublishFragmentResult(
                                const quicr::QUICRName &quicr_name,
                                const uint64_t &offset,
                                bool is_last_fragment,
                                const quicr::PublishMsgResult &result);
        virtual void onPublishObjectResult(
                                const quicr::QUICRName& quicr_name,
                                const quicr::PublishMsgResult& result);

        bool terminate;
        std::function<void(void)> task_complete;
        std::thread thread;
        std::shared_ptr<quicr::QuicRClient> client;
        std::mutex publisher_mutex;
};

