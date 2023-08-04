/*
 *  pub_sub_registry.cpp
 *
 *  Copyright (C) 2023
 *  Cisco Systems, Inc.
 *  All Rights Reserved.
 *
 *  Description:
 *      This file implements a registry of publisher and subscribers.  This is
 *      used by the H3 session and connection objects to facilitate direction
 *      of responses from the application.
 *
 *  Portability Issues:
 *      None.
 */

#include "pub_sub_registry.h"

namespace quicr::h3 {

/*
 *  PubSubRegistry::PubSubRegistry()
 *
 *  Description:
 *      Constructor for the PubSubRegistry object.
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
PubSubRegistry::PubSubRegistry()
  : last_identifier{ 0 }
{
    // Nothing more to do
}

/*
 *  PubSubRegistry::Publish()
 *
 *  Description:
 *      Add a record to the registry for this publisher.
 *
 *  Parameters:
 *      connection_id [in]
 *          The QUIC connection ID associated with this publisher.
 *
 *      stream_id [id]
 *          The QUIC stream ID associated with this publisher.
 *
 *      quicr_namespace [in]
 *          The QUICR namespace associated with this publisher.
 *
 *      client_transaction_id [in]
 *          Transaction ID received from the client.
 *
 *      pub_delegate [in]
 *          For clients, this is the publisher delegate object that would
 *          receive callbacks related to this publisher.
 *
 *  Returns:
 *      The unique identifier for this publisher record or 0 if there
 *      is already a publisher record for this namespace.  A zero value is
 *      also returned if one of the parameters appears to bad bad data.
 *
 *  Comments:
 *      None.
 */
RegistryID
PubSubRegistry::Publish(const QUICConnectionID& connection_id,
                        QUICStreamID stream_id,
                        const Namespace& quicr_namespace,
                        std::uint64_t client_transaction_id,
                        const std::shared_ptr<PublisherDelegate> pub_delegate)
{
  // Do a sanity check on the parameters
  if (connection_id.GetDataLength() == 0) return 0;
  if (quicr_namespace.length() == 0) return 0;

  // Lock the mutex
  std::lock_guard<std::mutex> lock(registry_mutex);

  // Look to see if there is an existing publisher record for this namespace
  if (publisher_map.contains(quicr_namespace)) return 0;

  // Get the next record identifier
  auto record_id = GetNextID();

  // Insert the publisher record
  pub_sub_records[record_id] = { .identifier = record_id,
                                 .publisher = true,
                                 .connection_id = connection_id,
                                 .stream_id = stream_id,
                                 .quicr_namespace = quicr_namespace,
                                 .transaction_id = client_transaction_id,
                                 .pub_delegate = pub_delegate,
                                 .sub_delegate =
                                   std::weak_ptr<SubscriberDelegate>() };

  publisher_map[quicr_namespace] = record_id;
  connection_map[connection_id].push_back(record_id);

  return record_id;
}

/*
 *  PubSubRegistry::Subscribe()
 *
 *  Description:
 *      Add a record to the registry for this subscriber.
 *
 *  Parameters:
 *      connection_id [in]
 *          The QUIC connection ID associated with this subscriber.
 *
 *      stream_id [id]
 *          The QUIC stream ID associated with this subscriber.
 *
 *      quicr_namespace [in]
 *          The QUICR namespace associated with this subscriber.
 *
 *      client_transaction_id [in]
 *          Transaction ID received from the client.
 *
 *      sub_delegate [in]
 *          For clients, this is the subscriber delegate object that would
 *          receive callbacks related to this subscriber.
 *
 *  Returns:
 *      The unique identifier for this subscriber record.  A zero value is
 *      returned if one of the parameters appears to bad bad data.
 *
 *  Comments:
 *      None.
 */
RegistryID
PubSubRegistry::Subscribe(
  const QUICConnectionID& connection_id,
  QUICStreamID stream_id,
  const Namespace& quicr_namespace,
  std::uint64_t client_transaction_id,
  const std::shared_ptr<SubscriberDelegate> sub_delegate)
{
  // Do a sanity check on the parameters
  if (connection_id.GetDataLength() == 0) return 0;
  if (quicr_namespace.length() == 0) return 0;

  // Lock the mutex
  std::lock_guard<std::mutex> lock(registry_mutex);

  // Get the next record identifier
  auto record_id = GetNextID();

  // Insert the subscriber record
  pub_sub_records[record_id] = { .identifier = record_id,
                                 .publisher = false,
                                 .connection_id = connection_id,
                                 .stream_id = stream_id,
                                 .quicr_namespace = quicr_namespace,
                                 .transaction_id = client_transaction_id,
                                 .pub_delegate =
                                   std::weak_ptr<PublisherDelegate>(),
                                 .sub_delegate = sub_delegate };

  subscriber_map[quicr_namespace].push_back(record_id);
  connection_map[connection_id].push_back(record_id);

  return record_id;
}

/*
 *  PubSubRegistry::FindRecord()
 *
 *  Description:
 *      Find and return the Pub/Sub Registry record for the entity associated
 *      with the given identifier.
 *
 *  Parameters:
 *      identifier [id]
 *          The identifier for which the pub/sub registry record is sought.
 *
 *  Returns:
 *      The record for the given identifier or std::nullopt if the record
 *      does not exist.
 *
 *  Comments:
 *      None.
 */
std::optional<PubSubRecord>
PubSubRegistry::FindRecord(RegistryID identifier)
{
  // Lock the mutex
  std::lock_guard<std::mutex> lock(registry_mutex);

  // Try to find the requested record
  auto it = pub_sub_records.find(identifier);

  // Return std::nullopt if not found
  if (it == pub_sub_records.end()) return std::nullopt;

  return it->second;
}

/*
 *  PubSubRegistry::Expunge()
 *
 *  Description:
 *      Remove the record associated with the given identifier.
 *
 *  Parameters:
 *      identifier [id]
 *          The identifier for which the record should be removed.
 *
 *  Returns:
 *      Nothing.
 *
 *  Comments:
 *      None.
 */
void
PubSubRegistry::Expunge(RegistryID identifier)
{
  // Just return if the identifier has a zero value
  if (identifier == 0) return;

  // Lock the mutex
  std::lock_guard<std::mutex> lock(registry_mutex);

  // Try to find the requested record
  auto it = pub_sub_records.find(identifier);

  // Return if not found
  if (it == pub_sub_records.end()) return;

  // Remove this from the connection_map
  auto cit = connection_map.find(it->second.connection_id);
  if (cit != connection_map.end())
  {
    std::erase(cit->second, identifier);

    // If the vector is empty, remove the map entry
    if (cit->second.empty()) connection_map.erase(cit);
  }

  // Remove the entry from the publisher or subscriber maps
  if (it->second.publisher)
  {
    // Erase the publisher record
    publisher_map.erase(it->second.quicr_namespace);
  }
  else
  {
    // Locate the vector of subscribers for this namespace
    auto sub_it = subscriber_map.find(it->second.quicr_namespace);

    // Found this vector?
    if (sub_it != subscriber_map.end())
    {
        // Remove the entry for this subscriber
        std::erase(sub_it->second, identifier);

        // If the vector is now empty, remove the map entry
        if (sub_it->second.empty()) subscriber_map.erase(sub_it);
    }
  }

  // Remove the pub/sub record
  pub_sub_records.erase(it);
}

/*
 *  PubSubRegistry::Expunge()
 *
 *  Description:
 *      Remove the records associated with specified QUIC connection identifier.
 *
 *  Parameters:
 *      connection_id [id]
 *          The QUIC connection identifier for which all associated records
 *          should be expunged.
 *
 *  Returns:
 *      Nothing.
 *
 *  Comments:
 *      It is assumed that the application will not be adding new registrations
 *      while this routine is being called. If it does, this may not remove
 *      everything. It is assumed this function will be called when connections
 *      are destroyed.
 */
void
PubSubRegistry::Expunge(const QUICConnectionID& connection_id)
{
  // Lock the mutex
  std::unique_lock<std::mutex> lock(registry_mutex);

  // Try to find the specified connection
  auto it = connection_map.find(connection_id);

  // Just return if it does not exist
  if (it == connection_map.end()) return;

  // Make a copy the vector registrations in the vector
  auto registrations = it->second;

  // Unlock the mutex
  lock.unlock();

  // Now expunge each registration
  for (auto registration : registrations) Expunge(registration);
}

/*
 *  PubSubRegistry::FindPublisher()
 *
 *  Description:
 *      Find and return the Pub/Sub Registry record for the entity publisher
 *      having associated with the specified namespace.
 *
 *  Parameters:
 *      quicr_namespace [id]
 *          The namespace for which the pub/sub registry record is sought.
 *
 *  Returns:
 *      The record for the given publisher namespace or std::nullopt if
 *      the record does not exist.
 *
 *  Comments:
 *      None.
 */
std::optional<PubSubRecord>
PubSubRegistry::FindPublisher(const Namespace& quicr_namespace)
{
  // Lock the mutex
  std::unique_lock<std::mutex> lock(registry_mutex);

  // Locate the publisher
  auto it = publisher_map.find(quicr_namespace);

  // Return std::nullopt if not found
  if (it == publisher_map.end()) return std::nullopt;

  // Copy the identifier
  auto identifier = it->second;

  // Unlock the mutex
  lock.unlock();

  return FindRecord(identifier);
}

/*
 *  PubSubRegistry::FindPublisher()
 *
 *  Description:
 *      Find and return the Pub/Sub Registry record for the entity publisher
 *      having associated with the specified QUICR name.
 *
 *  Parameters:
 *      quicr_name [id]
 *          The QUICR name for which the associated Publisher is sought.
 *
 *  Returns:
 *      The record for the given publisher or std::nullopt if the record does
 *      not exist.
 *
 *  Comments:
 *      None.
 */
std::optional<PubSubRecord>
PubSubRegistry::FindPublisher(const Name& quicr_name)
{
  // Lock the mutex
  std::unique_lock<std::mutex> lock(registry_mutex);

  auto it = std::find_if(publisher_map.begin(),
                         publisher_map.end(),
                         [&quicr_name](const auto& publisher) {
                           return publisher.first.contains(quicr_name);
                         });

  // Return std::nullopt if not found
  if (it == publisher_map.end()) return std::nullopt;

  // Copy the identifier
  auto identifier = it->second;

  // Unlock the mutex
  lock.unlock();

  return FindRecord(identifier);
}

/*
 *  PubSubRegistry::FindSubscribers()
 *
 *  Description:
 *      Find the set of records for subscribers for the given namespace.
 *
 *  Parameters:
 *      quicr_namespace [id]
 *          The namespace for which the pub/sub registry records are sought.
 *
 *  Returns:
 *      The records for the specified namespace.  If no records were found,
 *      the returned vector will be empty.
 *
 *  Comments:
 *      None.
 */
std::vector<PubSubRecord>
PubSubRegistry::FindSubscribers(const Namespace& quicr_namespace)
{
  std::vector<PubSubRecord> subscriber_records;

  // Lock the mutex
  std::unique_lock<std::mutex> lock(registry_mutex);

  // Find the subscriber vector for the given namespace
  auto it = subscriber_map.find(quicr_namespace);

  // If nothing is found, return an empty vector
  if (it == subscriber_map.end()) return subscriber_records;

  // Iterate over the vector of subscribers
  for (const auto subscriber : it->second)
  {
    // Locate records for the subscriber
    auto sub_it = pub_sub_records.find(subscriber);

    // If found, make a copy of that record
    if (sub_it != pub_sub_records.end()) {
        subscriber_records.push_back(sub_it->second);
    }
  }

  return subscriber_records;
}

/*
 *  PubSubRegistry::FindSubscriber()
 *
 *  Description:
 *      Find the subscriber for the given connection and namespace.
 *
 *  Parameters:
 *      connection_id [in]
 *          The QUIC connection ID associated with this subscription.
 *
 *      quicr_namespace [id]
 *          The namespace for which the pub/sub registry records are sought.
 *
 *  Returns:
 *      The record for the specified subscription or std::nullopt if the record
 *      does not exist.
 *
 *  Comments:
 *      This function exists to help enforce a rule that allows only one
 *      subscription per connection and namespace.  This is necessary since
 *      the unsubscribe message does not container a subscriber ID.
 *      TODO: We should fix the reason this function exists, then remove this
 *            function.
 */
std::optional<PubSubRecord>
PubSubRegistry::FindSubscriber(const QUICConnectionID& connection_id,
                               const Namespace& quicr_namespace)
{
  // Lock the mutex
  std::unique_lock<std::mutex> lock(registry_mutex);

  // Find the subscriber vector for the given namespace
  auto it = subscriber_map.find(quicr_namespace);

  // If nothing is found, return an empty vector
  if (it == subscriber_map.end()) return std::nullopt;

  // Iterate over the vector of subscribers
  for (auto subscriber : it->second)
  {
    // Locate records for the subscriber
    auto sub_it = pub_sub_records.find(subscriber);

    // If found, return a copy of the record
    if ((sub_it != pub_sub_records.end()) &&
        (sub_it->second.connection_id == connection_id)) {
        return sub_it->second;
    }
  }

  return std::nullopt;
}

/*
 *  PubSubRegistry::FindSubscriber()
 *
 *  Description:
 *      Find the subscriber using the given stream_id.
 *
 *  Parameters:
 *      connection_id [in]
 *          The QUIC connection ID associated with this subscription.
 *
 *      stream_id [in]
 *          The QUIC stream identifier associated with this subscription.
 *
 *  Returns:
 *      The record for the specified subscription or std::nullopt if the record
 *      does not exist.
 *
 *  Comments:
 *      None.
 */
std::optional<PubSubRecord>
PubSubRegistry::FindSubscriber(const QUICConnectionID& connection_id,
                               const QUICStreamID stream_id)
{
  // Lock the mutex
  std::unique_lock<std::mutex> lock(registry_mutex);

  // Find the subscriber vector for the given connection ID
  auto it = connection_map.find(connection_id);

  // If nothing is found, return an empty vector
  if (it == connection_map.end()) return std::nullopt;

  // Iterate over the vector of subscribers
  for (auto subscriber : it->second)
  {
    // Locate records for the subscriber
    auto sub_it = pub_sub_records.find(subscriber);

    // If found, return a copy of the record
    if ((sub_it != pub_sub_records.end()) &&
        (sub_it->second.stream_id == stream_id)) {
        return sub_it->second;
    }
  }

  return std::nullopt;
}

/*
 *  PubSubRegistry::FindRegistrations()
 *
 *  Description:
 *      Find the set of records for subscribers for the given namespace.
 *
 *  Parameters:
 *      connection_id [id]
 *          The QUIC connection identifier for which all associated records
 *          should be returned.
 *
 *  Returns:
 *      The records for the specified connection ID.
 *
 *  Comments:
 *      None.
 */
std::vector<PubSubRecord>
PubSubRegistry::FindRegistrations(const QUICConnectionID& connection_id)
{
  std::vector<PubSubRecord> records;

  // Lock the mutex
  std::unique_lock<std::mutex> lock(registry_mutex);

  // Try to find the specified connection
  auto it = connection_map.find(connection_id);

  // Just return if it does not exist
  if (it == connection_map.end()) return records;

  // Make a copy the vector registrations in the vector
  auto registrations = it->second;

  // Unlock the mutex
  lock.unlock();

  // Now expunge each registration
  for (auto registration : registrations)
  {
    auto record_it = pub_sub_records.find(registration);
    if (record_it != pub_sub_records.end())
    {
      records.push_back(record_it->second);
    }
  }

  return records;
}

/*
 *  PubSubRegistry::UpdateStreamID()
 *
 *  Description:
 *      Update the QUIC stream ID associated with this pub/sub record.
 *
 *  Parameters:
 *      identifier [id]
 *          The pub/sub registry record identifier to update.
 *
 *      stream_id [in]
 *          The QUIC stream ID to assign to this record.
 *
 *  Returns:
 *      True if the record was updated, false if the record was not found.
 *
 *  Comments:
 *      None.
 */
bool
PubSubRegistry::UpdateStreamID(RegistryID identifier, QUICStreamID stream_id)
{
  // Lock the mutex
  std::lock_guard<std::mutex> lock(registry_mutex);

  auto it = pub_sub_records.find(identifier);

  if (it == pub_sub_records.end()) return false;

  it->second.stream_id = stream_id;

  return true;
}

/*
 *  PubSubRegistry::GetNextID()
 *
 *  Description:
 *      Return the next publisher or subscriber record identifier.
 *
 *  Parameters:
 *      None.
 *
 *  Returns:
 *      Publisher or subscriber identifier value.
 *
 *  Comments:
 *      The mutex MUST be locked by the caller.
 */
RegistryID
PubSubRegistry::GetNextID()
{
  if (++last_identifier == 0) ++last_identifier;

  return last_identifier;
}

} // namespace quicr::h3
