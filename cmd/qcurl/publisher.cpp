/*
 *  publisher.cpp
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

#include <iostream> // PEJ - Remove?
#include <chrono> // PEJ - Remove?
#include "publisher.h"

/*
 *  Publisher::Publisher()
 *
 *  Description:
 *      Constructor for the Qcurl Publisher object.
 *
 *  Parameters:
 *      task_complete [in]
 *          The callback function the Publisher should call when it has
 *          finished with its task.
 *
 *  Returns:
 *      Nothing.
 *
 *  Comments:
 *      None.
 */
Publisher::Publisher(std::function<void(void)> task_complete) :
    terminate{false},
    task_complete{task_complete}
{
    // Prevent worker threads from starting before construction completes
    std::lock_guard<std::mutex> lock(publisher_mutex);

    std::cout << "Publisher initiated" << std::endl;
}

/*
 *  Publisher::~Publisher()
 *
 *  Description:
 *      Destructor for the Qcurl Publisher object.
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
Publisher::~Publisher()
{
    std::unique_lock<std::mutex> lock(publisher_mutex);
    terminate = true;
    lock.unlock();

    std::cout << "Publisher terminating" << std::endl;

    thread.join();

    std::cout << "Publisher terminated" << std::endl;
}

/*
 *  Publisher::Run()
 *
 *  Description:
 *      This function is called to initiate logic of the Publisher object.
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
void Publisher::Run(std::shared_ptr<quicr::QuicRClient> &client)
{
    std::unique_lock<std::mutex> lock(publisher_mutex);

    // Assign the client pointer
    this->client = client;

    // Issue a publish intent
    quicr::QUICRNamespace quicr_namespace{
        std::numeric_limits<std::uint64_t>::max(),
        std::numeric_limits<std::uint64_t>::max(),
        8};
    std::string auth_token = "TOKEN";
    client->publishIntentEnd(quicr_namespace, auth_token);

    // The following is jsut to do something until the client logic is complete

    unsigned counter = 0;

    std::cout << "Publisher running" << std::endl;

    while (!terminate && (++counter < 5))
    {
        lock.unlock();
        std::cout << "Ping" << std::endl;
        std::this_thread::sleep_for(std::chrono::seconds(1));
        lock.lock();
    }

    std::cout << "Publisher stopping" << std::endl;

    // If not told to terminate, report task complete
    if (!terminate) task_complete();
}

/*
 *  Publisher::onPublishIntentResponse()
 *
 *  Description:
 *      This function is called to deliver a response to an intent to publish.
 *
 *  Parameters:
 *      quicr_namespace [in]
 *          QUICR Namespace associated with the subscription.
 *
 *      result [in]
 *          Publish intent result.
 *
 *  Returns:
 *      Nothing.
 *
 *  Comments:
 *      None.
 */
void Publisher::onPublishIntentResponse(
                                const quicr::QUICRNamespace& quicr_namespace,
                                const quicr::PublishIntentResult& result)
{
    //! Incomplete
    std::cout << "Publisher received onPublishIntentResponse callback" << std::endl;
}

/*
 *  Publisher::onPublishFragmentResult()
 *
 *  Description:
 *      This function is called to deliver a response to an intent to publish.
 *
 *  Parameters:
 *      quicr_name [in]
 *          Identifies the QUICR Name for the object.
 *
 *      offset [in]
 *          Fragment offset value.
 *
 *      is_last_fragment [in]
 *          True if this is the last fragment.
 *
 *      result [in]
 *          Result of the publish operation.
 *
 *  Returns:
 *      Nothing.
 *
 *  Comments:
 *      None.
 */
void Publisher::onPublishFragmentResult(
                                const quicr::QUICRName &quicr_name,
                                const uint64_t &offset,
                                bool is_last_fragment,
                                const quicr::PublishMsgResult &result)
{
    //! Incomplete
    std::cout << "Publisher received onPublishFragmentResult callback" << std::endl;
}

/*
 *  Publisher::onPublishFragmentResult()
 *
 *  Description:
 *      This function is called to deliver a response to an intent to publish.
 *
 *  Parameters:
 *      quicr_name [in]
 *          Identifies the QUICR Name for the object.
 *
 *      result [in]
 *          Result of the publish operation.
 *
 *  Returns:
 *      Nothing.
 *
 *  Comments:
 *      None.
 */
void Publisher::onPublishObjectResult(
                                const quicr::QUICRName& quicr_name,
                                const quicr::PublishMsgResult& result)
{
    //! Incomplete
    std::cout << "Publisher received onPublishObjectResult callback" << std::endl;
}
