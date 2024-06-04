/*
 *  Copyright (C) 2024
 *  Cisco Systems, Inc.
 *  All Rights Reserved
 */
#pragma once

#include <quicr/quicr_common.h>

namespace quicr {

/**
 * @brief MOQ track delegate for subscribe and publish
 *
 * @details MOQ track delegate defines all track related callbacks and
 *  functions. Track delegate operates on a single track (namespace + name).
 *  It can be used for subscribe, publish, or both subscribe and publish. The
 *  only requirement is that the namespace and track alias be the same.
 */
class MoQTrackDelegate
{
public:
  enum class ReadError : uint8_t
  {
    OK = 0,
    NOT_AUTHORIZED,
    NOT_SUBSCRIBED,
    NO_DATA,
  };

  enum class SendError : uint8_t
  {
    OK = 0,
    NOT_AUTHORIZED,
    NOT_ANNOUNCED,
    NO_SUBSCRIBERS,
  };

  enum class TrackReadStatus : uint8_t
  {
    NOT_AUTHORIZED = 0,
    NOT_SUBSCRIBED,
    PENDING_SUBSCRIBE_RESPONSE,
    SUBSCRIBE_NOT_AUTHORIZED
  };

  enum class TrackSendStatus : uint8_t
  {
    NOT_ANNOUNCED = 0,
    PENDING_ANNOUNCE_RESPONSE,
    ANNOUNCE_NOT_AUTHORIZED,
    NO_SUBSCRIBERS,
  };


  // --------------------------------------------------------------------------
  // Public API methods that normally should not be overridden
  // --------------------------------------------------------------------------

  /**
   * @brief Track delegate constructor
   */
  MoQTrackDelegate(const bytes& track_namespace,
                   const bytes& track_name,
                   uint8_t default_priority,
                   uint32_t default_ttl);

  /**
   * @brief Send object to announced track
   *
   * @details Send object to announced track that was previously announced.
   *   This will have an error if the track wasn't announced yet. Status will
   *   indicate if there are no subscribers. In this case, the object will
   *   not be sent.
   *
   * @param[in] object   Object to send to track
   *
   * @returns SendError status of the send
   *
   */
  SendError sendObject(const std::span<const uint8_t>& object);
  SendError sendObject(const std::span<const uint8_t>& object, uint32_t ttl);

  /**
   * @brief Read object from track
   *
   * @details Reads an object from the subscribed track
   *
   * @param[out] object   Refence to object to be updated. Will be cleared.
   *
   * @returns ReadError status of the read
   */
  ReadError readObject(std::vector<const uint8_t>& object);

  /**
   * @brief Current track read status
   *
   * @details Obtains the current track read status/state
   *
   * @returns current TrackReadStatus
   */
  TrackReadStatus statusRead();

  /**
   * @brief Current track send status
   *
   * @details Obtains the current track send status/state
   *
   * @returns current TrackSendStatus
   */
  TrackReadStatus statusSend();

  /**
   * @brief set/update the default priority for published objects
   */
  void setDefaultPriority(uint8_t);

  /**
   * @brief set/update the default TTL expirty for published objects
   */
  void setDefaultTTL(uint32_t);

  // --------------------------------------------------------------------------
  // Public Virtual API callback event methods to be overridden
  // --------------------------------------------------------------------------

  /**
   * @brief Notificaiton that data is avaialble to be read.
   *
   * @details Event notification to inform the caller that data can be read. The caller
   *   should read all data if possible.
   *
   * @param objects_available       Number of objects available to be read at time of notification
   */
  virtual void callback_objectsAvailable(uint64_t objects_available) = 0;
  virtual void callback_objectReceived(std::vector<uint8_t>&& object) = 0;

  /**
   * @brief Notification that data can be sent
   * @details Notification that an announcement has been successful and there is at least one
   *   subscriber for the track. Data can now be succesfully sent.
   */
  virtual void callback_sendReady() = 0;

  /**
   * @brief Notification that data can not be sent
   * @details Notification that data cannot be sent yet with a reason. This will
   *   be called as it transitions through send states.
   *
   * @param status        Indicates the reason for why data cannot be sent [yet]
   */
  virtual void callback_sendNotReady(TrackSendStatus status) = 0;

  /**
   * @brief Notification that the send queue is congested
   * @details Notification indicates that send queue is backlogged and sending more
   *   will likely cause more congestion.
   *
   * @param cleared             Indicates if congestion has cleared
   * @param objects_in_queue    Number of objects still pending to be sent at time of notification
   */
  virtual void callback_sendCongested(bool cleared, uint64_t objects_in_queue) = 0;

  /**
   * @brief Notification to indicate reading is ready
   * @details Notification that an announcement has been successful and but
   * there are no subscribers, so data cannot be sent yet.
   */
  virtual void callback_readReady() = 0;

 /**
  * @brief Notification that read is not available
  *
  * @param status        Indicates the reason for why data cannot be sent [yet]
  */
 virtual void callback_readNotReady(TrackReadStatus status) = 0;



  // --------------------------------------------------------------------------
  // --------------------------------------------------------------------------

private:
  const bytes _track_namespace;
  const bytes _track_name;
  std::optional<uint64_t> _track_alias;
};

} // namespace quicr
