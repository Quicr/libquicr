/*
 *  quicr_client_delegate.h
 *
 *  Copyright (C) 2023
 *  Cisco Systems, Inc.
 *  All Rights Reserved
 *
 *  Description:
 *      This defined the client delegate interfaces utilized by the library to
 *      deliver information to the application.
 *
 *  Portability Issues:
 *      None.
 */

#pragma once

#include "quicr/quicr_common.h"

#include <qname>

#include <cstdint>

namespace quicr {

/*
 *  QUICR Subscriber delegate callback methods
 */
class SubscriberDelegate
{
public:
  SubscriberDelegate() = default;
  virtual ~SubscriberDelegate() = default;

  /**
   * @brief Callback for subscription response
   *
   * @param quicr_namespace : QUICR Namespace associated with the
   *                          Subscribe Request
   * @param result          : Result for the subscription
   *
   * @details This callback will be called when a subscription response
   *          is received, on error, or timeout.
   */
  virtual void onSubscribeResponse(const quicr::Namespace& quicr_namespace,
                                   const SubscribeResult& result) = 0;

  /**
   * @brief Indicates a given subscription is no longer valid
   *
   * @details  Subscription can terminate when a publisher terminated
   *           the stream or subscription timeout or other application
   *           reasons
   *
   * @param quicr_namespace       : Identifies QUICR namespace
   * @param reason                : Reason indicating end operation
   *
   */
  virtual void onSubscriptionEnded(
    const quicr::Namespace& quicr_namespace,
    const SubscribeResult::SubscribeStatus& reason) = 0;

  /**
   * @brief Report arrival of subscribed QUICR object under a Name
   *
   * @param quicr_name               : Identifies the QUICR Name for the object
   * @param priority                 : Identifies the relative priority of the
   *                                   current object
   * @param data                     : Opaque payload of the fragment
   *
   *
   * @note: It is important that the implementations not perform
   *        compute intensive tasks in this callback, but rather
   *        copy/move the needed information and hand back the control
   *        to the stack
   *
   * @note: Both the on_publish_object and on_publish_object_fragment
   *        callbacks will be called. The delegate implementation
   *        shall decide the right callback for their usage.
   */
  virtual void onSubscribedObject(const quicr::Name& quicr_name,
                                  uint8_t priority,
                                  bytes&& data) = 0;

  /**
   * @brief Report arrival of subscribed QUICR object fragment under a Name
   *
   * @param quicr_name               : Identifies the QUICR Name for the object
   * @param priority                 : Identifies the relative priority of the
   *                                   current object
   * @param offset                   : Current fragment offset
   * @param is_last_fragment         : Indicates if the current fragment is the
   *                                   last fragment
   * @param data                     : Opaque payload of the fragment
   *
   * @note: It is important that the implementations not perform
   *        compute intensive tasks in this callback, but rather
   *        copy/move the needed information and hand back the control
   *        to the stack
   */
  virtual void onSubscribedObjectFragment(const quicr::Name& quicr_name,
                                          uint8_t priority,
                                          const uint64_t& offset,
                                          bool is_last_fragment,
                                          bytes&& data) = 0;
};

/**
 *  Publisher common delegate callback operations
 */
class PublisherDelegate
{
public:
  PublisherDelegate() = default;
  virtual ~PublisherDelegate() = default;

  /**
   * @brief Callback on the response to the  publish intent
   *
   * @param quicr_namespace       : Identifies QUICR namespace
   * @param result                : Status of Publish Intent
   *
   * @details Entities processing the Subscribe Request MUST validate the
   *          request
   *
   * TODO: Add payload with origin signed blob
   */
  virtual void onPublishIntentResponse(const quicr::Namespace& quicr_namespace,
                                       const PublishIntentResult& result) = 0;
};

} // namespace quicr
