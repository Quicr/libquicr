/*
 *  pub_sub_registry.h
 *
 *  Copyright (C) 2023
 *  Cisco Systems, Inc.
 *  All Rights Reserved.
 *
 *  Description:
 *      This file defines a registry of publisher and subscribers.  This is
 *      used by the H3 session and connection objects to facilitate direction
 *      of responses from the application.
 *
 *  Portability Issues:
 *      None.
 */

#pragma once

#include <map>
#include <vector>
#include <memory>
#include <mutex>
#include <optional>
#include "quic_identifier.h"
#include "quiche_types.h"
#include "quicr/namespace.h"
#include "quicr/quicr_client_delegate.h"

namespace quicr::h3 {

// Define an identifier to use with the registry
typedef std::uint64_t RegistryID;

// Define a type to hold pub/sub details
struct PubSubRecord
{
  RegistryID identifier;                          // Record identifier
  bool publisher;                                 // Publisher?
  QUICConnectionID connection_id;                 // QUIC connection ID
  QUICStreamID stream_id;                         // QUIC stream ID
  bool reliable;                                  // Publish reliably?
  Namespace quicr_namespace;                      // QUICR Namespace
  std::uint64_t transaction_id;                   // Client's transaction ID
  std::weak_ptr<PublisherDelegate> pub_delegate;  // Publisher delegate
  std::weak_ptr<SubscriberDelegate> sub_delegate; // Subscriber delegate
};

// Define the PubSubRegistry object
class PubSubRegistry
{
public:
  PubSubRegistry();
  ~PubSubRegistry() = default;

  RegistryID Publish(
    const QUICConnectionID& connection_id,
    QUICStreamID stream_id,
    bool use_reliable_transport,
    const Namespace& quicr_namespace,
    std::uint64_t client_transaction_id,
    const std::shared_ptr<PublisherDelegate> pub_delegate = nullptr);

  RegistryID Subscribe(
    const QUICConnectionID& connection_id,
    QUICStreamID stream_id,
    const Namespace& quicr_namespace,
    std::uint64_t client_transaction_id,
    const std::shared_ptr<SubscriberDelegate> sub_delegate = nullptr);

  std::optional<PubSubRecord> FindRecord(RegistryID identifier);

  void Expunge(RegistryID identifier);
  void Expunge(const QUICConnectionID& connection_id);

  std::optional<PubSubRecord> FindPublisher(const Namespace& quicr_namespace);
  std::optional<PubSubRecord> FindPublisher(const Name& quicr_name);
  std::vector<PubSubRecord> FindSubscribers(const Namespace& quicr_namespace);
  std::optional<PubSubRecord> FindSubscriber(
    const QUICConnectionID& connection_id,
    const Namespace& quicr_namespace);
  std::optional<PubSubRecord> FindSubscriber(
    const QUICConnectionID& connection_id,
    const QUICStreamID stream_id);
  std::vector<PubSubRecord> FindRegistrations(
    const QUICConnectionID& connection_id);

  bool UpdateStreamID(RegistryID identifier, QUICStreamID stream_id);

protected:
  RegistryID GetNextID();
  RegistryID last_identifier;

  std::map<RegistryID, PubSubRecord> pub_sub_records;
  std::map<Namespace, std::vector<RegistryID>> subscriber_map;
  std::map<Namespace, RegistryID> publisher_map;
  std::map<QUICConnectionID, std::vector<RegistryID>> connection_map;

  std::mutex registry_mutex;
};

// Define a shared pointer type for convenience
typedef std::shared_ptr<PubSubRegistry> PubSubRegistryPointer;

} // namespace quicr::h3
