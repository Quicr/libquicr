#pragma once
#include "quicr/common.h"
#include "quicr/detail/uintvar.h"

namespace quicr::messages {
    quicr::Bytes& operator<<(quicr::Bytes& buffer, const quicr::Bytes& bytes);

    quicr::Bytes& operator<<(quicr::Bytes& buffer, const quicr::BytesSpan& bytes);
    quicr::BytesSpan operator>>(quicr::BytesSpan buffer, quicr::Bytes& value);

    quicr::Bytes& operator<<(quicr::Bytes& buffer, std::size_t value);
    quicr::BytesSpan operator>>(quicr::BytesSpan buffer, std::size_t& value);

    quicr::Bytes& operator<<(quicr::Bytes& buffer, std::uint8_t value);
    quicr::BytesSpan operator>>(quicr::BytesSpan buffer, uint8_t& value);

    quicr::Bytes& operator<<(quicr::Bytes& buffer, const quicr::UintVar& value);


    using GroupId = uint64_t;
    using ObjectId = uint64_t;

    enum struct ParameterType : uint64_t
    {
        kPath = 0x1,
        kMaxSubscribeId = 0x2, // version specific, unused
        kEndpointId = 0xF0,    // Endpoint ID, using temp value for now
        kInvalid = 0xFF,       // used internally.
    };

    struct Parameter
    {
        ParameterType type{ 0 };
        Bytes value;
    };

    Bytes& operator<<(Bytes& buffer, ParameterType value);
    BytesSpan operator>>(BytesSpan buffer, ParameterType& value);

    enum struct GroupOrder : uint8_t
    {
        kOriginalPublisherOrder = 0x0,
        kAscending,
        kDescending
    };

    Bytes& operator<<(Bytes& buffer, GroupOrder value);
    BytesSpan operator>>(BytesSpan buffer, GroupOrder& value);

    enum struct FilterType : uint64_t
    {
        kNone = 0x0,
        kLatestGroup,
        kLatestObject,
        kAbsoluteStart,
        kAbsoluteRange
    };

    Bytes& operator<<(Bytes& buffer, FilterType value);
    BytesSpan operator>>(BytesSpan buffer, FilterType& value);

    enum class TrackStatusCode : uint64_t
    {
        kInProgress = 0x00,
        kDoesNotExist,
        kNotStarted,
        kFinished,
        kUnknown
    };

    Bytes& operator<<(Bytes& buffer, TrackStatusCode value);
    BytesSpan operator>>(BytesSpan buffer, TrackStatusCode& value);

    enum class SubscribeDoneStatusCode : uint64_t
    {
        kInternalError = 0x00,
        kUnauthorized,
        kTrackEnded,
        kSubscribtionEnded,
        kGoingAway,
        kExpired,
        kTooFarBehind,
    };

    Bytes& operator<<(Bytes& buffer, SubscribeDoneStatusCode value);
    BytesSpan operator>>(BytesSpan buffer, SubscribeDoneStatusCode& value);

    enum class FetchType : uint8_t
    {
        kStandalone = 0x1,
        kJoiningFetch,
    };

    Bytes& operator<<(Bytes& buffer, FetchType value);
    BytesSpan operator>>(BytesSpan buffer, FetchType& value);

    enum class TerminationReason : uint64_t
    {
        kNoError = 0x0,
        kInternalError,
        kUnauthorized,
        kProtocolViolation,
        kDupTrackAlias,
        kParamLengthMismatch,
        kGoAwayTimeout = 0x10,
    };

    Bytes& operator<<(Bytes& buffer, TerminationReason value);
    BytesSpan operator>>(BytesSpan buffer, TerminationReason& value);

    enum class FetchErrorCode : uint8_t
    {
        kInternalError = 0x0,
        kUnauthorized = 0x1,
        kTimeout = 0x2,
        kNotSupported = 0x3,
        kTrackDoesNotExist = 0x4,
        kInvalidRange = 0x5,
    };

    Bytes& operator<<(Bytes& buffer, FetchErrorCode value);
    BytesSpan operator>>(BytesSpan buffer, FetchErrorCode& value);

    enum class AnnounceErrorCode : uint64_t
    {
        kInternalError = 0x0,
        kUnauthorized,
        kTimeout,
        kNotSupported,
        kUninterested
    };

    Bytes& operator<<(Bytes& buffer, AnnounceErrorCode value);
    BytesSpan operator>>(BytesSpan buffer, AnnounceErrorCode& value);

    // TODO (Suhas): rename it to StreamMapping
    enum ForwardingPreference : uint8_t
    {
        kStreamPerGroup = 0,
        kStreamPerObject,
        kStreamPerPriority,
        kStreamPerTrack,
        kDatagram
    };

    Bytes& operator<<(Bytes& buffer, ForwardingPreference value);
    BytesSpan operator>>(BytesSpan buffer, ForwardingPreference& value);

    enum class SubscribeErrorCode : uint64_t
    {
        kInternalError = 0x0,
        kUnauthorized,
        kTimeout,
        kNotSupported,
        kTrackDoesNotExist,
        kInvalidRange,
        kRetryTrackAlias,

        kTrackNotExist = 0xF0 // Missing in draft
    };

    Bytes& operator<<(Bytes& buffer, SubscribeErrorCode value);
    BytesSpan operator>>(BytesSpan buffer, SubscribeErrorCode& value);

    enum class SubscribeAnnouncesErrorCode : uint64_t
    {
        kInternalError = 0x0,
        kUnauthorized,
        kTimeout,
        kNotSupported,
        kNamespacePrefixUnknown,
    };

    Bytes& operator<<(Bytes& buffer, SubscribeAnnouncesErrorCode value);
    BytesSpan operator>>(BytesSpan buffer, SubscribeAnnouncesErrorCode& value);
} // namespace
