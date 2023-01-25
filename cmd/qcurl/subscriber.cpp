/*
 *  subscriber.cpp
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

#include <limits>
#include <iostream> // PEJ - Remove?
#include <chrono> // PEJ - Remove?
#include "subscriber.h"

/*
 *  Subscriber::Subscriber()
 *
 *  Description:
 *      Constructor for the Qcurl Subscriber object.
 *
 *  Parameters:
 *      task_complete [in]
 *          The callback function the Subscriber should call when it has
 *          finished with its task.
 *
 *  Returns:
 *      Nothing.
 *
 *  Comments:
 *      None.
 */
Subscriber::Subscriber(std::function<void(void)> task_complete) :
    terminate{false},
    task_complete{task_complete}
{
    // Prevent worker threads from starting before construction completes
    std::lock_guard<std::mutex> lock(subscriber_mutex);

    std::cout << "Subscriber initiated" << std::endl;
}

/*
 *  Subscriber::~Subscriber()
 *
 *  Description:
 *      Destructor for the Qcurl Subscriber object.
 *
 *  Parameters:
 *      None.
 *
 *  Returns:
 *      Nothing.
 *
 *  Comments:
 *      None.
 */
Subscriber::~Subscriber()
{
    std::unique_lock<std::mutex> lock(subscriber_mutex);
    terminate = true;
    lock.unlock();

    std::cout << "Subscriber terminating" << std::endl;

    thread.join();

    std::cout << "Subscriber terminated" << std::endl;
}

/*
 *  Subscriber::Run()
 *
 *  Description:
 *      This function is called to initiate logic of the Subscriber object.
 *
 *  Parameters:
 *      client [in]
 *          A pointer to the QuicrClient object.
 *
 *  Returns:
 *      Nothing.
 *
 *  Comments:
 *      None.
 */
void Subscriber::Run(std::shared_ptr<quicr::QuicRClient> &client)
{
    std::unique_lock<std::mutex> lock(subscriber_mutex);

    // Assign the client pointer
    this->client = client;

    std::cout << "Sending a subscription request" << std::endl;

    //! Change the following fake data
    quicr::QUICRNamespace quicr_namespace{
        std::numeric_limits<std::uint64_t>::max(),
        std::numeric_limits<std::uint64_t>::max(),
        8};
    quicr::SubscribeIntent intent = quicr::SubscribeIntent::immediate;
    std::string origin_url = "https:://localhost";
    bool use_reliable_transport = false;
    std::string auth_token = "TOKEN";
    quicr::bytes e2e_token = {0, 1, 2, 3};

    client->subscribe(quicr_namespace,
                      intent,
                      origin_url,
                      use_reliable_transport,
                      auth_token,
                      std::move(e2e_token));

    // The following is jsut to do something until the client logic is complete
    unsigned counter = 0;

    std::cout << "Subscriber running" << std::endl;

    while (!terminate && (++counter < 5))
    {
        lock.unlock();
        std::cout << "Ping" << std::endl;
        std::this_thread::sleep_for(std::chrono::seconds(1));
        lock.lock();
    }

    std::cout << "Subscriber stopping" << std::endl;

    // If not told to terminate, report task complete
    if (!terminate) task_complete();
}

/*
 *  Subscriber::onSubscribeResponse()
 *
 *  Description:
 *      This function will receive a callback for subscriptions.
 *
 *  Parameters:
 *      quicr_namespace [in]
 *          QUICR Namespace associated with the subscription.
 *
 *      result [in]
 *          Subscription result.
 *
 *  Returns:
 *      Nothing.
 *
 *  Comments:
 *      None.
 */
void Subscriber::onSubscribeResponse(
                                const quicr::QUICRNamespace &quicr_namespace,
                                const quicr::SubscribeResult &result)
{
    //! Incomplete
    std::cout << "Subscriber received onSubscribe callback" << std::endl;
}

/*
 *  Subscriber::onSubscriptionEnded()
 *
 *  Description:
 *      This function will receive a callback when a subscription ends.
 *
 *  Parameters:
 *      quicr_namespace [in]
 *          QUICR Namespace associated with the subscription.
 *
 *      result [in]
 *          Contains a reason for why the subscription ended.
 *
 *  Returns:
 *      Nothing.
 *
 *  Comments:
 *      None.
 */
void Subscriber::onSubscriptionEnded(
                                const quicr::QUICRNamespace &quicr_namespace,
                                const quicr::SubscribeResult &result)
{
    //! Incomplete
    std::cout << "Subscriber received onSubscriptionEnded callback" << std::endl;
}

/*
 *  Subscriber::onSubscribedObject()
 *
 *  Description:
 *      This function will receive a callback when a subscription ends.
 *
 *  Parameters:
 *      quicr_name [in]
 *          Identifies the QUICR Name for the object.
 *
 *      priority [in]
 *          Indicates the relative priority of the current object.
 *
 *      expiry_age_ms [in]
 *          Time after which this object should be purged from cache.
 *
 *      use_reliable_transport [in]
 *          Indicated the objects transport preference (if forwarded).
 *
 *      data [in]
 *          Opaque object payload.
 *
 *  Returns:
 *      Nothing.
 *
 *  Comments:
 *      It is important that the callback be halded expediently so as to not
 *      delay the calling thread.
 */
void Subscriber::onSubscribedObject(const quicr::QUICRName &quicr_name,
                                    uint8_t priority,
                                    uint16_t expiry_age_ms,
                                    bool use_reliable_transport,
                                    quicr::bytes &&data)
{
    //! Incomplete
    std::cout << "Subscriber received onSubscribedObject callback" << std::endl;
}
