#include "test_server.h"

#include "quicr/handlers/publish_fetch_handler.h"
#include "quicr/handlers/publish_namespace_handler.h"
#include "quicr/handlers/track_handler.h"
#include "quicr/session.h"

#include <ranges>

using namespace quicr;
using namespace quicr_test;

void
TestPublishTrackHandler::StatusChanged(Status status)
{
    switch (status) {
        case Status::kOk: {
            if (auto svr = server_.lock()) {
                if (svr->publish_accepted_promise_.has_value()) {
                    svr->publish_accepted_promise_->set_value({ GetRequestId().value(), GetFullTrackName(), {} });
                }
            }
            break;
        }
        case Status::kUnsubscribed: {
            if (const auto svr = server_.lock()) {
                if (svr->unsubscribe_promise_.has_value()) {
                    svr->unsubscribe_promise_->set_value(GetRequestId().value());
                    svr->unsubscribe_promise_.reset();
                }
            }
            break;
        }
        default:
            break;
    }
}

quicr::Reply<const quicr::PublishResponse, quicr::PublishErrorCode>
TestServer::PublishReceived(const std::shared_ptr<quicr::Session>& session,
                            std::uint64_t request_id,
                            const PublishAttributes& publish_attributes,
                            [[maybe_unused]] std::weak_ptr<quicr::SubscribeNamespaceHandler> ns_handler)
{
    std::lock_guard lock(state_mutex_);

    const auto th = TrackHash(publish_attributes.track_full_name);
    const auto track_alias = th.track_fullname_hash;

    // Is anyone interested in this prefix?
    for (const auto& [_, ns_handler] : namespace_subscribers_) {
        if (ns_handler->GetFullTrackName().name_space.HasSamePrefix(publish_attributes.track_full_name.name_space)) {
            const auto delivery_timeout = publish_attributes.delivery_timeout.value_or(0);
            auto handler = std::make_shared<TestPublishTrackHandler>(publish_attributes.track_full_name,
                                                                     quicr::TrackMode::kStream,
                                                                     publish_attributes.default_publisher_priority,
                                                                     delivery_timeout,
                                                                     shared_from_this());
            ns_handler->PublishTrack(handler);
        }
    }

    // Create a subscribe handler to receive objects from the publisher
    auto sub_track_handler = std::make_shared<TestSubscribeTrackHandler>(publish_attributes.track_full_name, true);

    // If there are subscribers for this track (possibly on a different connection/session),
    // link the subscribe handler to forward to them.
    auto sub_it = subscribes_.find(track_alias);
    if (sub_it != subscribes_.end()) {
        // Link to first subscriber's publish handler for forwarding
        auto& pub_handler = sub_it->second;
        if (pub_handler) {
            sub_track_handler->SetPublishHandler(pub_handler);
            sub_track_handler->Resume();
        }
    }

    pub_subscribes_[track_alias] = sub_track_handler;

    return quicr::PublishResponse{ {}, sub_track_handler };
}

quicr::Reply<void, int>
TestServer::PublishDoneReceived(const std::shared_ptr<quicr::Session>& session,
                                [[maybe_unused]] std::uint64_t request_id)
{
    std::lock_guard lock(state_mutex_);
    return {};
}

quicr::Reply<quicr::RequestResponse, quicr::RequestErrorCode>
TestServer::SubscribeReceived(const std::shared_ptr<quicr::Session>& session,
                              std::uint64_t request_id,
                              const FullTrackName& track_full_name,
                              const SubscribeAttributes& subscribe_attributes)
{
    std::lock_guard lock(state_mutex_);

    const SubscribeDetails details = { request_id, track_full_name, subscribe_attributes };
    if (subscribe_promise_.has_value()) {
        subscribe_promise_->set_value(details);
    }

    const auto th = TrackHash(track_full_name);
    const auto track_alias = th.track_fullname_hash;

    // Calculate TTL from delivery timeout
    const std::uint32_t ttl = subscribe_attributes.delivery_timeout != std::chrono::milliseconds::zero()
                                ? static_cast<std::uint32_t>(subscribe_attributes.delivery_timeout.count())
                                : 5000;

    // Create a publish track handler to send objects to this subscriber
    auto pub_track_handler =
      std::make_shared<TestPublishTrackHandler>(track_full_name,
                                                TrackMode::kStream,
                                                subscribe_attributes.priority,
                                                ttl,
                                                std::static_pointer_cast<TestServer>(shared_from_this()));

    // Store the publish handler for this subscriber, visible to every session sharing
    // these callbacks.
    subscribes_[track_alias] = pub_track_handler;
    subscribe_sessions_[track_alias] = session;

    // Bind the publish track handler to send data to the subscriber
    session->BindPublisherTrack(session->GetConnection()->GetID(), request_id, pub_track_handler, false);

    // Link any existing publisher subscribe handlers (possibly from a different
    // connection/session) to forward to this subscriber.
    auto pub_sub_it = pub_subscribes_.find(track_alias);
    if (pub_sub_it != pub_subscribes_.end()) {
        auto& [pub_conn, sub_handler] = *pub_sub_it;
        if (sub_handler) {
            sub_handler->SetPublishHandler(pub_track_handler);
            sub_handler->Resume();
        }
    }

    return RequestResponse{ subscribe_attributes.is_publisher_initiated };
}

quicr::Reply<std::vector<quicr::TrackNamespace>, quicr::RequestErrorCode>
TestServer::SubscribeTracksReceived(const std::shared_ptr<quicr::Session>& session,
                                    const TrackNamespace& prefix_namespace,
                                    const SubscribeNamespaceAttributes& attributes)
{
    std::lock_guard lock(state_mutex_);

    if (subscribe_namespace_promise_.has_value()) {
        subscribe_namespace_promise_->set_value({ prefix_namespace, attributes });
    }

    auto ns_handler = PublishNamespaceHandler::Create(prefix_namespace);
    session->PublishNamespace(ns_handler, true);

    for (const auto& track : known_published_tracks_) {
        auto handler =
          std::make_shared<TestPublishTrackHandler>(track.full_track_name,
                                                    quicr::TrackMode::kStream,
                                                    track.attributes.default_publisher_priority,
                                                    track.attributes.delivery_timeout.value_or(0),
                                                    std::static_pointer_cast<TestServer>(shared_from_this()));
        ns_handler->PublishTrack(handler);
    }

    // Registered on the shared callbacks so publishes arriving on a different
    // connection/session can be matched against this namespace subscription.
    namespace_subscribers_[prefix_namespace] = ns_handler;

    // Deliberately not prefix matching to allow testing bad case. Tests should only add tracks
    // with this in mind. Blindly accept it.
    return known_published_namespaces_;
}

quicr::Reply<std::vector<quicr::TrackNamespace>, quicr::RequestErrorCode>
TestServer::SubscribeNamespaceReceived([[maybe_unused]] const std::shared_ptr<quicr::Session>& session,
                                       [[maybe_unused]] const TrackNamespace& prefix_namespace,
                                       [[maybe_unused]] const SubscribeNamespaceAttributes& attributes)
{
    // TODO: Implement.
    return std::vector<quicr::TrackNamespace>{};
}

void
TestServer::AddKnownPublishedNamespace(const TrackNamespace& track_namespace)
{
    std::lock_guard lock(state_mutex_);
    known_published_namespaces_.push_back(track_namespace);
}

void
TestServer::AddKnownPublishedTrack(const FullTrackName& track,
                                   const std::optional<messages::Location>& largest_location,
                                   const PublishAttributes& attributes)
{
    std::lock_guard lock(state_mutex_);
    known_published_tracks_.emplace_back(
      AvailableTrack{ track, largest_location.value_or(messages::Location{ 0, 0 }), attributes });
}

std::shared_ptr<TestPublishTrackHandler>
TestServer::GetSubscriberPublishHandler(const std::uint64_t track_alias) const
{
    std::lock_guard lock(state_mutex_);

    const auto it = subscribes_.find(track_alias);
    return it == subscribes_.end() ? nullptr : it->second;
}

bool
TestServer::UnbindSubscriberPublishTrack(const std::uint64_t track_alias)
{
    std::shared_ptr<TestPublishTrackHandler> handler;
    std::shared_ptr<quicr::Session> session;

    {
        std::lock_guard lock(state_mutex_);

        const auto handler_it = subscribes_.find(track_alias);
        const auto session_it = subscribe_sessions_.find(track_alias);
        if (handler_it == subscribes_.end() || session_it == subscribe_sessions_.end()) {
            return false;
        }

        handler = handler_it->second;
        session = session_it->second;
    }

    // Called without our lock held, since the session takes its own lock during unbind.
    session->UnbindPublisherTrack(session->GetConnection()->GetID(), handler, false);

    return true;
}

quicr::Reply<void, quicr::PublishNamespaceErrorCode>
TestServer::PublishNamespaceReceived(const std::shared_ptr<quicr::Session>& session,
                                     const TrackNamespace& track_namespace,
                                     const PublishNamespaceAttributes& publish_announce_attributes)
{
    if (publish_namespace_promise_.has_value()) {
        publish_namespace_promise_->set_value({ track_namespace, publish_announce_attributes });
    }

    return {};
}

quicr::Reply<const quicr::FetchResponse, quicr::FetchErrorCode>
TestServer::StandaloneFetchReceived(const std::shared_ptr<quicr::Session>& session,
                                    const std::uint64_t request_id,
                                    const FullTrackName& track_full_name,
                                    const StandaloneFetchAttributes& attrs)
{
    if (fetch_response_data_.empty()) {
        // No response data configured
        return quicr::Unexpected<quicr::Error<quicr::FetchErrorCode>>(FetchErrorCode::kInternalError,
                                                                      "No fetch test response configured");
    }

    // Create location for the response
    const messages::Location largest_location = { .group = fetch_response_data_.back().headers.group_id,
                                                  .object = fetch_response_data_.back().headers.object_id };

    // Publish the response
    auto pub_fetch_handler =
      PublishFetchHandler::Create(track_full_name,
                                  attrs.priority,
                                  request_id,
                                  attrs.group_order.value_or(attrs.publisher_default_group_order),
                                  500);

    session->BindFetchTrack(pub_fetch_handler);
    for (size_t i = 0; i < fetch_response_data_.size(); ++i) {
        pub_fetch_handler->PublishObject(fetch_response_data_[i].headers, fetch_response_data_[i].payload);
    }

    return FetchResponse{ largest_location };
}

quicr::Reply<const quicr::FetchResponse, quicr::FetchErrorCode>
TestServer::JoiningFetchReceived(const std::shared_ptr<quicr::Session>& session,
                                 const uint64_t request_id,
                                 const FullTrackName& track_full_name,
                                 const JoiningFetchAttributes& attrs)
{
    if (joining_fetch_promise_.has_value()) {
        joining_fetch_promise_->set_value({ session->GetConnection()->GetID(), request_id, track_full_name, attrs });
        joining_fetch_promise_.reset();
    }

    if (!fetch_response_data_.empty()) {
        messages::Location largest_location{};
        for (const auto& response : fetch_response_data_) {
            largest_location =
              std::max(largest_location, messages::Location{ response.headers.group_id, response.headers.object_id });
        }

        auto pub_fetch_handler =
          PublishFetchHandler::Create(track_full_name,
                                      attrs.priority,
                                      request_id,
                                      attrs.group_order.value_or(attrs.publisher_default_group_order),
                                      500);
        session->BindFetchTrack(pub_fetch_handler);
        for (const auto& response : fetch_response_data_) {
            pub_fetch_handler->PublishObject(response.headers, response.payload);
        }

        return FetchResponse{ .largest_location = largest_location };
    }

    return quicr::Unexpected<quicr::Error<quicr::FetchErrorCode>>(FetchErrorCode::kInternalError,
                                                                  "No joining fetch test response configured");
}

quicr::Reply<void, int>
TestServer::UnsubscribeReceived(const std::shared_ptr<quicr::Session>& session, const uint64_t request_id)
{
    std::lock_guard lock(state_mutex_);
    if (unsubscribe_received_promise_.has_value()) {
        const auto handler_type =
          expected_unsubscribe_handler_type_.value_or(UnsubscribeReceivedDetails::HandlerType::kSubscribeTrack);
        unsubscribe_received_promise_->set_value({ .request_id = request_id, .handler_type = handler_type });
        unsubscribe_received_promise_.reset();
        expected_unsubscribe_handler_type_.reset();
    }

    return {};
}

quicr::Reply<void, int>
TestServer::NewGroupRequested(const quicr::FullTrackName& track_full_name, std::uint64_t group_id)
{
    std::lock_guard lock(state_mutex_);
    const auto th = quicr::TrackHash(track_full_name);

    auto it = pub_subscribes_.find(th.track_fullname_hash);
    if (it == pub_subscribes_.end()) {
        return {};
    }

    auto& [track_alias, sub_handler] = *it;
    if (sub_handler) {
        sub_handler->RequestNewGroup(group_id);
    }

    return {};
}
