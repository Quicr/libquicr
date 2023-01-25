/*
 *  subscriber.h
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

// Define the Subscriber object
class Subscriber : public quicr::SubscriberDelegate
{
    public:
        Subscriber(std::function<void(void)> task_complete);
        virtual ~Subscriber();

        void Run(std::shared_ptr<quicr::QuicRClient> &client);

    protected:
        virtual void onSubscribeResponse(
                                const quicr::QUICRNamespace& quicr_namespace,
                                const quicr::SubscribeResult& result);
        virtual void onSubscriptionEnded(
                                const quicr::QUICRNamespace& quicr_namespace,
                                const quicr::SubscribeResult& result);
        virtual void onSubscribedObject(
                                const quicr::QUICRName &quicr_name,
                                uint8_t priority,
                                uint16_t expiry_age_ms,
                                bool use_reliable_transport,
                                quicr::bytes &&data);

        bool terminate;
        std::function<void(void)> task_complete;
        std::thread thread;
        std::shared_ptr<quicr::QuicRClient> client;
        std::mutex subscriber_mutex;
};

