#pragma once
#include "quicr/common.h"
#include "quicr/detail/uintvar.h"

namespace quicr::ctrl_messages {
    enum struct ParameterTypeEnum : uint8_t
    {
        kPath = 0x1,
        kMaxSubscribeId = 0x2, // version specific, unused
        kEndpointId = 0xF0,    // Endpoint ID, using temp value for now
        kInvalid = 0xFF,       // used internally.
    };

    Bytes& operator<<(Bytes& buffer, ParameterTypeEnum value);
    BytesSpan operator>>(BytesSpan buffer, ParameterTypeEnum& value);

    enum struct GroupOrderEnum : uint8_t
    {
        kOriginalPublisherOrder = 0x0,
        kAscending,
        kDescending
    };

    Bytes& operator<<(Bytes& buffer, GroupOrderEnum value);
    BytesSpan operator>>(BytesSpan buffer, GroupOrderEnum& value);

    enum struct FilterTypeEnum : uint64_t
    {
        kNone = 0x0,
        kLatestGroup,
        kLatestObject,
        kAbsoluteStart,
        kAbsoluteRange
    };

    Bytes& operator<<(Bytes& buffer, FilterTypeEnum value);
    BytesSpan operator>>(BytesSpan buffer, FilterTypeEnum& value);

    enum class TrackStatusCodeEnum : uint64_t
    {
        kInProgress = 0x00,
        kDoesNotExist,
        kNotStarted,
        kFinished,
        kUnknown
    };

    Bytes& operator<<(Bytes& buffer, TrackStatusCodeEnum value);
    BytesSpan operator>>(BytesSpan buffer, TrackStatusCodeEnum& value);

    enum class FetchTypeEnum : uint8_t
    {
        kStandalone = 0x1,
        kJoiningFetch,
    };

    Bytes& operator<<(Bytes& buffer, FetchTypeEnum value);
    BytesSpan operator>>(BytesSpan buffer, FetchTypeEnum& value);

    enum class TerminationReasonEnum : uint64_t
    {
        kNoError = 0x0,
        kInternalError,
        kUnauthorized,
        kProtocolViolation,
        kDupTrackAlias,
        kParamLengthMismatch,
        kGoAwayTimeout = 0x10,
    };

    Bytes& operator<<(Bytes& buffer, TerminationReasonEnum value);
    BytesSpan operator>>(BytesSpan buffer, TerminationReasonEnum& value);

    enum class FetchErrorCodeEnum : uint8_t
    {
        kTrackDoesNotExist = 0xF0 // Missing in draft
    };

    Bytes& operator<<(Bytes& buffer, FetchErrorCodeEnum value);
    BytesSpan operator>>(BytesSpan buffer, FetchErrorCodeEnum& value);

    // TODO (Suhas): rename it to StreamMapping
    enum ForwardingPreferenceEnum : uint8_t
    {
        kStreamPerGroup = 0,
        kStreamPerObject,
        kStreamPerPriority,
        kStreamPerTrack,
        kDatagram
    };

    Bytes& operator<<(Bytes& buffer, ForwardingPreferenceEnum value);
    BytesSpan operator>>(BytesSpan buffer, ForwardingPreferenceEnum& value);

    enum class SubscribeErrorCodeEnum : uint64_t
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

    enum class SubscribeAnnouncesErrorCodeEnum : uint64_t
    {
        kInternalError = 0x0,
        kUnauthorized,
        kTimeout,
        kNotSupported,
        kNamespacePrefixUnknown,
    };

    // SAH - verify this
    Bytes& operator<<(Bytes& buffer, Bytes bytes);

    Bytes& operator<<(Bytes& buffer, BytesSpan bytes);
    BytesSpan operator>>(BytesSpan buffer, Bytes& value);

    Bytes& operator<<(Bytes& buffer, std::size_t value);
    BytesSpan operator>>(BytesSpan buffer, std::size_t& value);

    Bytes& operator<<(Bytes& buffer, std::uint8_t value);
    BytesSpan operator>>(BytesSpan buffer, uint8_t& value);

    Bytes& operator<<(Bytes& buffer, UintVar value);
    Bytes& operator<<(Bytes& buffer, UintVar value);

} // namespace
