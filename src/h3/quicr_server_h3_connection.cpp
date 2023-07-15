/*
 *  h3_server_connection.cpp
 *
 *  Copyright (C) 2023
 *  Cisco Systems, Inc.
 *  All Rights Reserved.
 *
 *  Description:
 *      This file implements a H3ServerConnection object that is used for
 *      communication over a single QUIC connection.
 *
 *      There are three types of valid requests:
 *          - POST /path - Indicate a publish intent
 *          - PUT /path - Publish an object
 *          - DELETE /path - Terminate a publication or subscription
 *          - GET /path - Subscribe to a namespace
 *
 *      Response codes:
 *          POST:   201 if successful
 *                  419 if request expired
 *                  405 if request failed
 *                  303 if redirected (not implemented)
 *                  400 if the request was unacceptable
 *          PUT:    200 if successful
 *                  400 if the request was unacceptable
 *                  404 if for objects published without publish intent
 *          DELETE: 200 if the publish was cancelled (always succeeds)
 *          GET:    200 if successful
 *                  400 if the request was unacceptable
 *                  404 if the namespace does not exist (not implemented)
 *
 *      And other request type will result in 501 being returned.
 *      Server errors will result in a 500 status code being returned.
 *
 *      TODO: Stream closures for long-lived connections (e.g., closing the
 *            GET request) is not presently acted up. That should result in
 *            cancelling the subscription. Further, if a stream is closed
 *            before a response is delivered, that can also cause client/server
 *            sync issues.
 *
 *  Portability Issues:
 *      None.
 */

#include <algorithm>
#include <cctype>
#include <chrono>
#include "quicr_server_h3_connection.h"
#include "cantina/data_buffer.h"
#include "cantina/logger_macros.h"
#include "quiche_api_lock.h"
#include "quicr/encode.h"
#include "quicr/quicr_common.h"
#include "quicr/name.h"

namespace quicr {

/*
 *  H3ServerConnection::H3ServerConnection()
 *
 *  Description:
 *      Constructor for the H3ServerConnection object.
 *
 *  Parameters:
 *      parent_logger [in]
 *          Parent logger object.
 *
 *      timer_manager [in]
 *          A pointer to the TimerManager object.
 *
 *      network [in]
 *          Network object used to send data.
 *
 *      pub_sub_registry [in]
 *          A pointer to the Pub/Sub registry to record publications and
 *          subscription data.
 *
 *      max_send_size [in]
 *          Maximum size of data packets to transmit.
 *
 *      max_recv_size [in]
 *          Maximum size of data packets to receive.
 *
 *      use_datagrams [in]
 *          Use datagrams if peer supports them.
 *
 *      local_cid [in]
 *          Local (server) connection ID.
 *
 *      local_address [in]
 *          Local server address.
 *
 *      quiche_connection [in]
 *          Quiche connection data; this object will take ownership of this
 *          data and destroy it in the destructor.
 *
 *      heartbeat_interval [in]
 *          Period in milliseconds with which ping messages should be sent so
 *          that connections remain up without actual traffic.
 *
 *      closure_callback [in]
 *          Function to call when the connection is closing.
 *
 *      server_delegate [in]
 *          A reference to the server delegate to which callbacks are made.
 *
 *  Returns:
 *      Nothing.
 *
 *  Comments:
 *      None.
 */
H3ServerConnection::H3ServerConnection(
  const cantina::LoggerPointer& parent_logger,
  const cantina::TimerManagerPointer& timer_manager,
  const cantina::AsyncRequestsPointer& async_requests,
  const cantina::NetworkPointer& network,
  const PubSubRegistryPointer& pub_sub_registry,
  socket_t data_socket,
  std::size_t max_send_size,
  std::size_t max_recv_size,
  bool use_datagrams,
  const QUICConnectionID& local_cid,
  const cantina::NetworkAddress& local_address,
  quiche_conn* quiche_connection,
  std::uint64_t heartbeat_interval,
  const ClosureCallback closure_callback,
  ServerDelegate& server_delegate)
  : H3ConnectionBase(parent_logger,
                     timer_manager,
                     async_requests,
                     network,
                     pub_sub_registry,
                     data_socket,
                     max_send_size,
                     max_recv_size,
                     use_datagrams,
                     local_cid,
                     local_address,
                     quiche_connection,
                     heartbeat_interval,
                     closure_callback)
  , server_delegate{ server_delegate }
{
}

/*
 *  H3ServerConnection::~H3ServerConnection()
 *
 *  Description:
 *      Destructor for the H3ServerConnection object.
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
H3ServerConnection::~H3ServerConnection()
{
  // Lock the client mutex
  std::unique_lock<std::mutex> lock(connection_lock);

  // Set the terminate flag
  terminate = true;

  // Make a copy of the Pub/Sub Registry to safely empty it below
  auto registrations = pub_sub_registry->FindRegistrations(local_cid);

  // Unlock the client mutex
  lock.unlock();

  // If there are publishers or subscribers remaining, let the app know they
  // are gone since the connection is terminating
  for (auto& registration : registrations) {
    if (registration.publisher) {
      PublishEndNotify(registration);
    } else {
      UnsubscribeNotify(registration);
    }
  }
}

////////////////////////////////////////////////////////////////////////////
// Functions to satisfy the QUICR Server interface
////////////////////////////////////////////////////////////////////////////

/*
 *  H3ServerConnection::PublishIntentResponse()
 *
 *  Description:
 *      This function is called by the application in response to the Publish
 *      Intent request delivered.  This will deliver an appropriate HTTP
 *      response back to the client.
 *
 *  Parameters:
 *      publisher [in]
 *          The publisher record from the pub/sub registry for this response.
 *
 *      result [in]
 *          The result to send to the client as delivered by the application.
 *
 *  Returns:
 *      Nothing.
 *
 *  Comments:
 *      None.
 */
void
H3ServerConnection::PublishIntentResponse(const PubSubRecord& publisher,
                                          const PublishIntentResult& result)
{
  unsigned status_code; // HTTP status code to return

  try {
    messages::PublishIntentResponse response{
      messages::MessageType::PublishIntentResponse,
      publisher.quicr_namespace,
      result.status,
      publisher.transaction_id
    };

    messages::MessageBuffer msg(sizeof(response));
    msg << response;

    // Lock the connection object
    std::unique_lock<std::mutex> lock(connection_lock);

    // Update the response record
    RequestData* request = FindRequest(publisher.stream_id);
    if (!request) {
      logger->warning << "Request for publish intent response not found";
      return;
    }

    // Ensure the state is "Initiated"
    if (request->state != H3RequestState::Initiated) {
      logger->warning << "PublishIntentResponse found unexpected request state";
      return;
    }

    // Update the response state
    request->state = H3RequestState::Complete;

    // Determine the appropriate status code
    switch (result.status) {
      case messages::Response::Ok:
        status_code = 201;
        break;
      case messages::Response::Expired:
        status_code = 419;
        break;
      case messages::Response::Fail:
        status_code = 405;
        break;
      case messages::Response::Redirect:
        status_code = 303;
        break;
      default:
        status_code = 500;
        break;
    }

    // Send the response to the client
    SendHTTPResponse(publisher.stream_id, status_code, {}, msg.take(), false);

    // This request is now complete, so remove it
    ExpungeRequest(publisher.stream_id);

    // If the request was rejected, we remove the publisher record
    if (result.status != messages::Response::Ok) {
      pub_sub_registry->Expunge(publisher.identifier);
    }

    // Dispatch Quiche messages
    DispatchMessages(lock);
  } catch (const std::exception& e) {
    logger->error
      << "Unexpected error trying to process publish response message: "
      << e.what() << std::flush;
  } catch (...) {
    logger->error
      << "Unexpected error trying to process publish response message"
      << std::flush;
  }
}

/*
 *  H3ServerConnection::SubscribeResponse()
 *
 *  Description:
 *      This function is called by the application in response to a subscription
 *      request.  This will deliver an appropriate HTTP response back to the
 *      client.
 *
 *  Parameters:
 *      subscriber [in]
 *          The subscriber record from the pub/sub registry for this response.
 *
 *      result [in]
 *          The result to send to the client as delivered by the application.
 *
 *  Returns:
 *      Nothing.
 *
 *  Comments:
 *      None.
 */
void
H3ServerConnection::SubscribeResponse(const PubSubRecord& subscriber,
                                      const SubscribeResult& result)
{
  unsigned status_code; // HTTP status code to return

  try {
    messages::SubscribeResponse response;
    response.transaction_id = subscriber.transaction_id;
    response.quicr_namespace = subscriber.quicr_namespace;
    response.response = result.status;

    messages::MessageBuffer msg;
    msg << response;

    // Lock the connection object
    std::unique_lock<std::mutex> lock(connection_lock);

    // Update the response record
    RequestData* request = FindRequest(subscriber.stream_id);
    if (!request) {
      logger->warning << "Request for subscribe response not found";
      return;
    }

    // Ensure the state is "Initiated"
    if (request->state != H3RequestState::Initiated) {
      logger->warning << "SubscribeResponse found unexpected request state";
      return;
    }

    // Determine the appropriate status code
    switch (result.status) {
      case SubscribeResult::SubscribeStatus::Ok:
        request->state = H3RequestState::Active;
        status_code = 200;
        break;
      case SubscribeResult::SubscribeStatus::Expired:
        request->state = H3RequestState::Complete;
        status_code = 419;
        break;
      case SubscribeResult::SubscribeStatus::Redirect:
        request->state = H3RequestState::Complete;
        status_code = 303;
        break;
      case SubscribeResult::SubscribeStatus::FailedError:
        request->state = H3RequestState::Complete;
        status_code = 405;
        break;
      case SubscribeResult::SubscribeStatus::FailedAuthz:
        request->state = H3RequestState::Complete;
        status_code = 401;
        break;
      case SubscribeResult::SubscribeStatus::TimeOut:
        request->state = H3RequestState::Complete;
        status_code = 408;
        break;
      default:
        request->state = H3RequestState::Complete;
        status_code = 500;
        break;
    }

    // Send the response to the client, keeping this stream open if subscribing
    SendHTTPResponse(subscriber.stream_id,
                     status_code,
                     {},
                     msg.take(),
                     true,
                     (result.status != SubscribeResult::SubscribeStatus::Ok));

    // If the request is complete, expunge it
    if (request->state == H3RequestState::Complete) {
      ExpungeRequest(subscriber.stream_id);
    }

    // If the request was rejected, we remove the subscriber record
    if (result.status != SubscribeResult::SubscribeStatus::Ok) {
      pub_sub_registry->Expunge(subscriber.identifier);
    }

    // Dispatch Quiche messages
    DispatchMessages(lock);
  } catch (const std::exception& e) {
    logger->error
      << "Unexpected error trying to process subscribe response message: "
      << e.what() << std::flush;
  } catch (...) {
    logger->error
      << "Unexpected error trying to process subscribe response message"
      << std::flush;
  }
}

/*
 *  H3ServerConnection::SubscriptionEnded()
 *
 *  Description:
 *      Notify the remote client that the subscription has ended.
 *
 *  Parameters:
 *      subscriber [in]
 *          The subscriber record from the pub/sub registry for this response.
 *
 *      quicr_namespace [in]
 *          The namespace for this subscription.
 *
 *      result [in]
 *          The reason the subscription has ended.
 *
 *  Returns:
 *      Nothing.
 *
 *  Comments:
 *      None.
 */
void
H3ServerConnection::SubscriptionEnded(
  const PubSubRecord& subscriber,
  const quicr::Namespace& quicr_namespace,
  const SubscribeResult::SubscribeStatus& reason)
{
  try {
    // Lock the connection object
    std::unique_lock<std::mutex> lock(connection_lock);

    // Update the response record
    RequestData* request = FindRequest(subscriber.stream_id);
    if (!request) {
      logger->warning << "Request for subscribe response not found";
      return;
    }

    // Ensure the request is in the expected state
    if (request->state != H3RequestState::Active) {
      logger->warning << "Subscriber not in an active state; cannot unsubscribe"
                      << std::flush;
    }

    messages::SubscribeEnd subEnd;
    subEnd.quicr_namespace = quicr_namespace;
    subEnd.reason = reason;

    messages::MessageBuffer msg;
    msg << subEnd;

    // Move the buffer and create a DataBuffer object
    std::vector<std::uint8_t> message_buffer = msg.take();
    cantina::DataBuffer data_buffer(message_buffer.size() +
                                    sizeof(std::uint64_t));

    // Insert the message length
    data_buffer.AppendValue(static_cast<std::uint64_t>(message_buffer.size()));
    data_buffer.AppendValue(message_buffer);

    // Send the message indicating the subscription has ended
    QuicheCall(quiche_h3_send_body,
               http3_connection,
               quiche_connection,
               subscriber.stream_id,
               data_buffer.GetBufferPointer(),
               data_buffer.GetDataLength(),
               true);

    // Remove the subscription record
    ExpungeRequest(subscriber.stream_id);

    // Remove the subscription record
    pub_sub_registry->Expunge(subscriber.identifier);

    // Dispatch Quiche messages
    DispatchMessages(lock);
  } catch (const std::exception& e) {
    logger->error << "Unexpected error trying send subscription ended message: "
                  << e.what() << std::flush;
  } catch (...) {
    logger->error << "Unexpected error trying send subscription ended message"
                  << std::flush;
  }
}

/*
 *  H3ServerConnection::SendNamedObject()
 *
 *  Description:
 *      This function will send an object to the subscriber.
 *
 *  Parameters:
 *      subscriber [in]
 *          The subscriber record from the pub/sub registry for this response.
 *
 *      use_reliable_transport [in]
 *          If true, this datagram will be sent as an amendment to the GET
 *          request for this subscription.  If false, it will be sent as an
 *          HTTP datagram.
 *
 *  Returns:
 *      Nothing.
 *
 *  Comments:
 *      None.
 */
void
H3ServerConnection::SendNamedObject(const PubSubRecord& subscriber,
                                    bool use_reliable_transport,
                                    const messages::PublishDatagram& datagram)
{
  try {
    messages::MessageBuffer msg;
    msg << datagram;
    std::vector<std::uint8_t> message = msg.take();

    // Lock the connection object
    std::unique_lock<std::mutex> lock(connection_lock);

    // If told to use a reliable transport of if the peer does not support
    // datagrams, the message will be sent reliably
    if (use_reliable_transport || !using_datagrams) {
      cantina::DataBuffer data_buffer(message.size() + sizeof(std::uint64_t));

      // Insert the message length and message
      data_buffer.AppendValue(static_cast<std::uint64_t>(message.size()));
      data_buffer.AppendValue(message);

      auto result = QuicheCall(quiche_h3_send_body,
                               http3_connection,
                               quiche_connection,
                               subscriber.stream_id,
                               data_buffer.GetBufferPointer(),
                               data_buffer.GetDataLength(),
                               false);
      if (result < 0) {
        logger->warning << "Failed to send named object" << std::flush;
      }
    } else {
      auto result = QuicheCall(quiche_h3_send_dgram,
                               http3_connection,
                               quiche_connection,
                               subscriber.stream_id,
                               const_cast<std::uint8_t*>(message.data()),
                               message.size());

      if (result < 0) {
        logger->warning << "Failed to send datagram" << std::flush;
      }
    }

    // Dispatch Quiche messages
    DispatchMessages(lock);
  } catch (const std::exception& e) {
    logger->error << "Unexpected error trying to send named object: "
                  << e.what() << std::flush;
  } catch (...) {
    logger->error << "Unexpected error trying to send named object"
                  << std::flush;
  }
}

////////////////////////////////////////////////////////////////////////////
// End of functions to satisfy the QUICR Server interface
////////////////////////////////////////////////////////////////////////////

/*
 *  H3ServerConnection::PublishEndNotify()
 *
 *  Description:
 *      This function notify the application that a publish has ended.
 *      This is generally due to connection termination.
 *
 *  Parameters:
 *      publisher [in]
 *          The publisher record in the pub/sub registry that is no longer
 *          publishing.
 *
 *  Returns:
 *      Nothing.
 *
 *  Comments:
 *      TODO: There is really no data to be passed to the application, except
 *            the namespace.  Perhaps the server delegate needs changes?
 */
void
H3ServerConnection::PublishEndNotify(const PubSubRecord& publisher)
{
  try {
    // Create an async event to issue the call to the delegate
    async_requests->Perform(
      [server_delegate = &server_delegate, publisher = publisher]() {
        server_delegate->onPublishIntentEnd(publisher.quicr_namespace, {}, {});
      });
  } catch (const std::exception& e) {
    logger->error << "Failed to remove publisher: " << e.what() << std::flush;
  } catch (...) {
    logger->error << "Failed to remove publisher" << std::flush;
  }

  // Remove the registry entry since there was a failure
  pub_sub_registry->Expunge(publisher.identifier);
}

/*
 *  H3ServerConnection::UnsubscribeNotify()
 *
 *  Description:
 *      This function notify the application that a subscription has ended.
 *      This is generally due to connection termination.
 *
 *  Parameters:
 *      subscriber [in]
 *          The subscriber record in the pub/sub registry that is unsubscribing.
 *
 *  Returns:
 *      Nothing.
 *
 *  Comments:
 *      TODO: The security token is empty and it would not be available
 *            if the connection closed abruptly.
 */
void
H3ServerConnection::UnsubscribeNotify(const PubSubRecord& subscriber)
{
  try {
    // Create an async event to issue the call to the delegate
    async_requests->Perform(
      [server_delegate = &server_delegate, subscriber = subscriber]() {
        server_delegate->onUnsubscribe(
          subscriber.quicr_namespace, subscriber.identifier, {});
      });
  } catch (const std::exception& e) {
    logger->error << "Failed to perform unsubscribe: " << e.what()
                  << std::flush;
  } catch (...) {
    logger->error << "Failed to perform unsubscribe" << std::flush;
  }

  // Remove the registry entry since there was a failure
  pub_sub_registry->Expunge(subscriber.identifier);
}

/*
 *  H3ServerConnection::HandleCompletedRequest()
 *
 *  Description:
 *      This function is called when once an HTTP request is "complete."
 *      For a client, this means the request is complete and no additional
 *      data is coming.  For a server, this means that the client has finished
 *      issuing its request.
 *
 *  Parameters:
 *      stream_id [in]
 *          QUIC stream on which this event occurred.
 *
 *      request [in]
 *          The RequestData object corresponding to this request.
 *
 *  Returns:
 *      True if the server is completely finished serving the request of
 *      false if there is additional data to send later.
 *
 *  Comments:
 *      The connection mutex must be locked by the caller.
 */
bool
H3ServerConnection::HandleCompletedRequest(QUICStreamID stream_id,
                                           RequestData* request)
{
  LOGGER_DEBUG(logger, "[stream " << stream_id << "] H3 - Finished");

  // Process the HTTP request
  auto [final_response, status] = ProcessRequest(stream_id, request);

  // Status code 100 indicates the response is pending, but nothing is signaled
  // to the client; just return for now
  if (status == 100) return false;

  // Send a response back to the client
  SendHTTPResponse(stream_id, status, {}, {}, false, final_response);

  // Indicate whether the server is finished serving the request
  return final_response;
}

/*
 *  H3ServerConnection::HandleH3DatagramEvent()
 *
 *  Description:
 *      This function is called when handling the QUICHE_H3_EVENT_DATAGRAM
 *      event.
 *
 *  Parameters:
 *      stream_id [in]
 *          QUIC stream on which this event occurred.
 *
 *      datagram [in]
 *          The received datagram.
 *
 *  Returns:
 *      Nothing.
 *
 *  Comments:
 *      The connection mutex must be locked by the caller.
 */
void
H3ServerConnection::HandleReceivedDatagram(QUICStreamID stream_id,
                                           std::vector<std::uint8_t>& datagram)
{
  try {
    // Move the vector data into a MessageBuffer
    messages::MessageBuffer msg(std::move(datagram));

    // Deserialize the message
    messages::PublishDatagram datagram;
    msg >> datagram;

    // Try to find the publisher
    auto publisher = pub_sub_registry->FindPublisher(datagram.header.name);
    if (!publisher.has_value()) {
      logger->warning << "Unable to find publisher for name "
                      << datagram.header.name << std::flush;
      return;
    }

    // Create an async event to issue the call to the delegate
    async_requests->Perform([server_delegate = &server_delegate,
                              stream_id,
                              identifier = publisher->identifier,
                              datagram = std::move(datagram)]() mutable {
      server_delegate->onPublisherObject(
        identifier, stream_id, false, std::move(datagram));
    });
  } catch (const std::exception& e) {
    logger->error << "Error processing datagram: " << e.what() << std::flush;
  } catch (...) {
    logger->error << "Error processing datagram" << std::flush;
  }
}

/*
 *  H3ServerConnection::ProcessRequest()
 *
 *  Description:
 *      This function is called from HandleH3FinishedEvent() to handle the
 *      details of the incoming request.  The valid requests are enumerated
 *      at the top of this module.
 *
 *  Parameters:
 *      stream_id [in]
 *          QUIC stream to be processed.
 *
 *  Returns:
 *      A pair having these components:
 *          bool - Final response?
 *          unsigned - status code to return to client
 *
 *  Comments:
 *      The connection mutex must be locked by the caller.
 */
std::pair<bool, unsigned>
H3ServerConnection::ProcessRequest(QUICStreamID stream_id, RequestData* request)
{
  // If terminating, short-circuit processing
  if (terminate) return { true, 503 };

  // Locate the request method and path, updating the request data structure
  request->method = request->request_headers[":method"];
  std::transform(request->method.begin(),
                 request->method.end(),
                 request->method.begin(),
                 [](char c) { return std::toupper(c); });
  request->path = request->request_headers[":path"];

  // Assign the request state
  request->state = H3RequestState::Initiated;

  // If either the method or path are empty, return 400
  if (request->method.empty() || request->path.empty()) return { true, 400 };

  // Ensure the URI leads with what is expected; this should be revised later
  if ((!request->path.starts_with("/pub/0x")) &&
      (!request->path.starts_with("/sub/0x"))) {
    logger->warning << "Invalid URI in path: " << request->path << std::flush;
    return { true, 400 };
  }

  // Process the request based on the type of request
  if (request->method == "POST") {
    logger->info << "Received POST to " << request->path << std::flush;

    // Since a POST is a PublishIntent, process it accordingly
    if (request->request_body.empty()) return { true, 400 };

    // Process the PublishIntent
    if (HandlePublishIntent(stream_id, request)) return { false, 100 };

    return { true, 400 };
  }

  if (request->method == "PUT") {
    logger->info << "Received PUT to " << request->path << std::flush;

    // Since a PUT is a "publish named object", process it accordingly
    if (request->request_body.empty()) return { true, 400 };

    // Forward the published message
    return { true, HandlePublishNamedObject(stream_id, request) };
  }

  if (request->method == "DELETE") {
    logger->info << "Received DELETE for " << request->path << std::flush;

    // Assumption this signals Publication Intent ends
    if (request->path.starts_with("/pub/0x")) {
      return { true, HandlePublishIntentEnd(request) };
    }

    // Assumption this signals Unsubscribe
    if (request->path.starts_with("/sub/0x")) {
      return { true, HandleUnsubscribe(request) };
    }

    return { true, 400 };
  }

  if (request->method == "GET") {
    logger->info << "Received GET for " << request->path << std::flush;

    // Assumption is this signals a subscription request
    if (HandleSubscribe(stream_id, request)) return { false, 100 };

    return { true, 400 };
  }

  return { true, 501 };
}

/*
 *  H3ServerConnection::SendHTTPResponse()
 *
 *  Description:
 *      This function will emit an HTTP response to the client.  Once the
 *      response is sent, this function will also remove data from the
 *      internal maps related to the request if the stream is closed.
 *
 *  Parameters:
 *      stream_id [in]
 *          QUIC stream on which to send an HTTP response.
 *
 *      response_headers [in]
 *          Any HTTP headers that should be included in the response.
 *
 *      status_code [in]
 *          The status code to return
 *
 *      prefix_length [in]
 *          Prefix the message to send with the message length.  This is used
 *          for responses that may result in multiple, concatenated messages.
 *          Namely, a subscription will result in a stream of named objects.
 *
 *      close_stream [in]
 *          Indicates whether the stream should be closed after the message
 *          is delivered.  The default is to close the stream.
 *
 *  Returns:
 *      Nothing.
 *
 *  Comments:
 *      The connection mutex must be locked by the caller.
 */
void
H3ServerConnection::SendHTTPResponse(
  QUICStreamID stream_id,
  unsigned status_code,
  const HTTPHeaders& response_headers,
  const std::vector<std::uint8_t>& response_body,
  bool prefix_length,
  bool close_stream)
{
  // Headers to send back in every reply
  HTTPHeaders header_map = { { ":status", std::to_string(status_code) },
                             { "server", "QoH3Server" } };

  // Add additional headers provided by the caller
  for (auto& [header, value] : response_headers) header_map[header] = value;

  // Allocate memory for the headers
  quiche_h3_header* headers =
    new (std::nothrow) quiche_h3_header[header_map.size()];
  if (headers == nullptr) {
    logger->critical << "Failed to allocate memory" << std::flush;
    NotifyOnClosure(true);
    return;
  }

  // Put contents of header map into allocated structure array
  {
    std::size_t i = 0;
    for (auto const& item : header_map) {
      headers[i].name =
        reinterpret_cast<const std::uint8_t*>(item.first.c_str());
      headers[i].name_len = item.first.size();

      headers[i].value =
        reinterpret_cast<const std::uint8_t*>(item.second.c_str());
      headers[i].value_len = item.second.size();

      i++;
    }
  }

  // Send the HTTP headers in response
  QuicheCall(quiche_h3_send_response,
             http3_connection,
             quiche_connection,
             stream_id,
             headers,
             header_map.size(),
             (close_stream ? response_body.empty() : false));

  LOGGER_DEBUG(logger,
               "[stream " << stream_id << "] Sending response to client");

  // Delete the headers array
  delete[] headers;

  // If there is a response body, send it
  if (!response_body.empty()) {
    if (prefix_length) {
      cantina::DataBuffer data_buffer(sizeof(std::uint64_t) +
                                      response_body.size());

      // Insert the message length and the message
      data_buffer.AppendValue(static_cast<std::uint64_t>(response_body.size()));
      data_buffer.AppendValue(response_body);

      // Transmit the response body
      QuicheCall(quiche_h3_send_body,
                 http3_connection,
                 quiche_connection,
                 stream_id,
                 data_buffer.GetBufferPointer(),
                 data_buffer.GetDataLength(),
                 close_stream);
    } else {
      // Transmit the response body
      QuicheCall(quiche_h3_send_body,
                 http3_connection,
                 quiche_connection,
                 stream_id,
                 const_cast<std::uint8_t*>(response_body.data()),
                 response_body.size(),
                 close_stream);
    }
  }
}

/*
 *  H3ServerConnection::HandlePublishIntent()
 *
 *  Description:
 *      This function is called to handle publication requests (HTTP POST)
 *      messages.
 *
 *  Parameters:
 *      stream_id [in]
 *          QUIC stream on which a POST request was received.
 *
 *      request [in]
 *          A pointer to the associated RequestData object.
 *
 *  Returns:
 *      True is successful, false if there was a problem.
 *
 *  Comments:
 *      The connection mutex must be locked by the caller.
 */
bool
H3ServerConnection::HandlePublishIntent(QUICStreamID stream_id,
                                        RequestData* request)
{
  RegistryID publisher_id = 0; // Registry ID for publisher

  try {
    // Get the contents of the POST body
    messages::MessageBuffer msg(std::move(request->request_body));

    // Deserialize the message
    messages::PublishIntent publish_intent;
    msg >> publish_intent;

    // Record the publish intent
    publisher_id = pub_sub_registry->Publish(local_cid,
                                             stream_id,
                                             publish_intent.quicr_namespace,
                                             publish_intent.transaction_id);

    // If the registration failed, return 0
    if (publisher_id == 0) {
      logger->warning << "Failed to create publisher for "
                      << publish_intent.quicr_namespace << std::flush;
      return false;
    }

    logger->info << "PublishIntent for namespace "
                 << publish_intent.quicr_namespace << std::flush;

    // Create an async event to issue the call to the delegate
    async_requests->Perform([server_delegate = &server_delegate,
                             intent = std::move(publish_intent)]() mutable {
      server_delegate->onPublishIntent(intent.quicr_namespace,
                                       "" /* intent.origin_url */,
                                       false,
                                       "" /* intent.relay_token */,
                                       std::move(intent.payload));
    });

    return true;
  } catch (const std::exception& e) {
    logger->error << "Failed to decode Publish Intent: " << e.what()
                  << std::flush;
  } catch (...) {
    logger->error << "Failed to decode Publish Intent" << std::flush;
  }

  // Remove the registry entry since there was a failure
  if (publisher_id) pub_sub_registry->Expunge(publisher_id);

  return false;
}

/*
 *  H3ServerConnection::HandlePublishIntentEnd()
 *
 *  Description:
 *      This function is called to handle publication end requests (HTTP DELETE)
 *      messages.
 *
 *  Parameters:
 *      request [in]
 *          A pointer to the associated RequestData object.
 *
 *  Returns:
 *      HTTP status code indicating the result.
 *
 *  Comments:
 *      The connection mutex must be locked by the caller.
 *
 *      TODO: This is not complete.  The parameters for onPublishIntentEnd()
 *            were not fully defined as this was written, so it needs updating.
 */
unsigned
H3ServerConnection::HandlePublishIntentEnd(RequestData* request)
{
  try {
    messages::MessageBuffer msg(std::move(request->request_body));

    // Deserialize the message
    messages::PublishIntentEnd publish_intent_end;
    msg >> publish_intent_end;

    // Try to find the publisher
    auto publisher =
      pub_sub_registry->FindPublisher(publish_intent_end.quicr_namespace);
    if (!publisher.has_value()) {
      logger->warning << "Unable to find publisher for name "
                      << publish_intent_end.quicr_namespace << std::flush;
      return 404;
    }

    // Create an async event to issue the call to the delegate
    async_requests->Perform([server_delegate = &server_delegate,
                             pie = std::move(publish_intent_end)]() mutable {
      server_delegate->onPublishIntentEnd(
        pie.quicr_namespace, {}, std::move(pie.payload));
    });

    // Remove the publisher record
    pub_sub_registry->Expunge(publisher->identifier);

    return 200;
  } catch (const std::exception& e) {
    logger->error << "Exception in PublishIntentEnd: " << e.what()
                  << std::flush;
  } catch (...) {
    logger->error << "Exception in PublishIntentEnd" << std::flush;
  }

  return 500;
}

/*
 *  H3ServerConnection::HandlePublishNamedObject()
 *
 *  Description:
 *      This function is called to handle objects sent via PUT.
 *
 *  Parameters:
 *      stream_id [in]
 *          QUIC stream on which the PUT request was received.
 *
 *      request [in]
 *          A pointer to the associated RequestData object.
 *
 *  Returns:
 *      An HTTP status code to return for the request.
 *
 *  Comments:
 *      The connection mutex must be locked by the caller.
 *
 *      TODO: What stream ID is the application expecting? The one for which
 *            the original publication intent was given or the one for the
 *            PUT request.  At the moment, the code sends the former since
 *            there is no other stream ID to use for datagrams.
 */
unsigned
H3ServerConnection::HandlePublishNamedObject(
  [[maybe_unused]] QUICStreamID stream_id,
  RequestData* request)
{
  try {
    // Get the contents of the PUT body
    messages::MessageBuffer msg(std::move(request->request_body));

    // Deserialize the message
    messages::PublishDatagram datagram;
    msg >> datagram;

    // Try to find the publisher
    auto publisher = pub_sub_registry->FindPublisher(datagram.header.name);
    if (!publisher.has_value()) {
      logger->warning << "Unable to find publisher for name "
                      << datagram.header.name << std::flush;
      return 404;
    }

    // Create an async event to issue the call to the delegate
    async_requests->Perform([server_delegate = &server_delegate,
                             identifier = publisher->identifier,
                             stream_id = publisher->stream_id,
                             datagram = std::move(datagram)]() mutable {
      server_delegate->onPublisherObject(
        identifier, stream_id, true, std::move(datagram));
    });

    return 200;
  } catch (const std::exception& e) {
    logger->error << "Failed to decode published object: " << e.what()
                  << std::flush;
  } catch (...) {
    logger->error << "Failed to decode published object" << std::flush;
  }

  return 500;
}

/*
 *  H3ServerConnection::HandleSubscribe()
 *
 *  Description:
 *      This function will handle subscription requests (GET requests).
 *
 *  Parameters:
 *      stream_id [in]
 *          QUIC stream on which the GET request was received.
 *
 *      request [in]
 *          A pointer to the associated RequestData object.
 *
 *  Returns:
 *      True is successful, false if there was a problem.
 *
 *  Comments:
 *      The connection mutex must be locked by the caller.
 */
bool
H3ServerConnection::HandleSubscribe(QUICStreamID stream_id,
                                    RequestData* request)
{
  RegistryID subscriber_id = 0; // Registry ID for subscriber

  try {
    // Get the contents of the GET body
    messages::MessageBuffer msg(std::move(request->request_body));

    messages::Subscribe subscribe;
    msg >> subscribe;

    // Ensure there isn't a subscriber on this connection for this namespace
    // TODO: We should not have to perform this step.  However, since the
    //       unsubscribe message does not specify the subscriber ID, all
    //       that can be done is match on the connection ID and namespace.
    //       This would be ambiguous if multiple were allowed.
    if (pub_sub_registry->FindSubscriber(local_cid, subscribe.quicr_namespace)
          .has_value()) {
      logger->warning << "Duplicate subscription request on connection for: "
                      << subscribe.quicr_namespace << std::flush;
      return false;
    }

    // Record the subscription
    subscriber_id = pub_sub_registry->Subscribe(local_cid,
                                                stream_id,
                                                subscribe.quicr_namespace,
                                                subscribe.transaction_id);

    // If the registration failed, return 0
    if (subscriber_id == 0) {
      logger->warning << "Failed to create subscription for "
                      << subscribe.quicr_namespace << std::flush;
      return false;
    }

    logger->info << "Subscription for namespace " << subscribe.quicr_namespace
                 << std::flush;

    // Create an async event to issue the call to the delegate
    async_requests->Perform([server_delegate = &server_delegate,
                             subscriber_id,
                             stream_id,
                             subscribe = std::move(subscribe)]() mutable {
      server_delegate->onSubscribe(subscribe.quicr_namespace,
                                   subscriber_id,
                                   subscriber_id,
                                   stream_id,
                                   subscribe.intent,
                                   "",
                                   false, // Use reliable transport?
                                   "",
                                   {});
    });

    return true;
  } catch (const std::exception& e) {
    logger->error << "Failed to decode subscription request: " << e.what()
                  << std::flush;
  } catch (...) {
    logger->error << "Failed to decode subscription request" << std::flush;
  }

  // Remove the registry entry since there was a failure
  if (subscriber_id) pub_sub_registry->Expunge(subscriber_id);

  return false;
}

/*
 *  H3ServerConnection::HandleUnsubscribe()
 *
 *  Description:
 *      Handle requests from the client to terminate subscriptions.
 *
 *  Parameters:
 *      request [in]
 *          A pointer to the associated RequestData object.
 *
 *  Returns:
 *      True in all cases. There is no value in signaling a failed unsubscribe.
 *
 *  Comments:
 *      TODO: The security token is empty and it would not be available
 *            if the connection closed abruptly.
 */
bool
H3ServerConnection::HandleUnsubscribe(RequestData* request)
{
  PubSubRecord subscriber{};

  try {
    // Get the contents of the DELETE body
    messages::MessageBuffer msg(std::move(request->request_body));

    messages::Unsubscribe unsub;
    msg >> unsub;

    // Locate the subscriber record
    auto subscribers = pub_sub_registry->FindSubscribers(unsub.quicr_namespace);
    for (auto& item : subscribers) {
      if (item.connection_id == local_cid) {
        subscriber = item;
        break;
      }
    }

    // If the subscriber was not found, return
    if (subscriber.identifier == 0) return true;

    // Create an async event to issue the call to the delegate
    async_requests->Perform([server_delegate = &server_delegate,
                             subscriber_id = subscriber.identifier,
                             quicr_namespace = unsub.quicr_namespace]() {
      server_delegate->onUnsubscribe(quicr_namespace, subscriber_id, {});
    });

    // Remove the subscriber from the registry
    pub_sub_registry->Expunge(subscriber.identifier);

    // Terminate the QUIC stream associated with this subscription
    QuicheCall(quiche_h3_send_body,
               http3_connection,
               quiche_connection,
               subscriber.stream_id,
               nullptr,
               0,
               true);

  } catch (const std::exception& e) {
    logger->error << "Failed to handle unsubscribe: " << e.what() << std::flush;
  } catch (...) {
    logger->error << "Failed to handle unsubscribe" << std::flush;
  }

  // This should never fail
  return true;
}

} // namespace quicr
