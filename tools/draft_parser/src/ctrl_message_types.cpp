#include "quicr/detail/ctrl_message_types.h"

namespace quicr::ctrl_messages {

    Bytes& operator<<(Bytes& buffer, const Bytes& bytes)
    {
        buffer << bytes.size(); // length of byte span
        buffer.insert(buffer.end(), bytes.begin(), bytes.end());
        return buffer;
    }

    Bytes& operator<<(Bytes& buffer, const BytesSpan& bytes)
    {
        buffer << bytes.size(); // length of byte span
        buffer.insert(buffer.end(), bytes.begin(), bytes.end());
        return buffer;
    }

    Bytes& operator<<(Bytes& buffer, const UintVar& varint)
    {
        buffer.insert(buffer.end(), varint.begin(), varint.end());
        return buffer;
    }

    Bytes& operator<<(Bytes& buffer, std::uint8_t value)
    {
        // assign 8 bits - not a varint
        buffer.push_back(value);
        return buffer;
    }

    Bytes& operator<<(Bytes& buffer, std::size_t value)
    {
        UintVar varint = static_cast<std::uint64_t>(value);
        buffer << varint;
        return buffer;
    }

    BytesSpan operator>>(BytesSpan buffer, Bytes& value)
    {
        uint64_t size = 0;
        buffer = buffer >> size;
        value.assign(buffer.begin(), std::next(buffer.begin(), size));
        return buffer.subspan(value.size());
    }

    BytesSpan operator>>(BytesSpan buffer, uint8_t& value)
    {
        // need 8 bits - not a varint
        value = buffer[0];
        return buffer.subspan(sizeof(value));
    }

    BytesSpan operator>>(BytesSpan buffer, uint64_t& value)
    {
        UintVar value_uv(buffer);
        value = static_cast<uint64_t>(value_uv);
        return buffer.subspan(value_uv.size());
    }

    Bytes& operator<<(Bytes& buffer, ParameterTypeEnum value)
    {
        buffer << UintVar(value);
        return buffer;
    }

    BytesSpan operator>>(BytesSpan buffer, ParameterTypeEnum& value)
    {
        std::uint64_t uvalue;
        buffer = buffer >> uvalue;
        value = static_cast<ParameterTypeEnum>(uvalue);
        return buffer;
    }

    Bytes& operator<<(Bytes& buffer, GroupOrderEnum value)
    {
        buffer << static_cast<std::uint64_t>(value);
        return buffer;
    }

    BytesSpan operator>>(BytesSpan buffer, GroupOrderEnum& value)
    {
        std::uint64_t uvalue;
        buffer = buffer >> uvalue;
        value = static_cast<GroupOrderEnum>(uvalue);
        return buffer;
    }

    Bytes& operator<<(Bytes& buffer, FilterTypeEnum value)
    {
        buffer << static_cast<std::uint64_t>(value);
        return buffer;
    }

    BytesSpan operator>>(BytesSpan buffer, FilterTypeEnum& value)
    {
        std::uint64_t uvalue;
        buffer = buffer >> uvalue;
        value = static_cast<FilterTypeEnum>(uvalue);
        return buffer;
    }

    Bytes& operator<<(Bytes& buffer, FetchTypeEnum value)
    {
        buffer << static_cast<std::uint64_t>(value);
        return buffer;
    }

    BytesSpan operator>>(BytesSpan buffer, FetchTypeEnum& value)
    {
        std::uint8_t uvalue;
        buffer = buffer >> uvalue;
        value = static_cast<FetchTypeEnum>(uvalue);
        return buffer;
    }

    Bytes& operator<<(Bytes& buffer, AnnounceErrorCodeEnum value)
    {
        buffer << static_cast<std::uint64_t>(value);
        return buffer;
    }

    BytesSpan operator>>(BytesSpan buffer, AnnounceErrorCodeEnum& value)
    {
        std::uint64_t uvalue;
        buffer = buffer >> uvalue;
        value = static_cast<AnnounceErrorCodeEnum>(uvalue);
        return buffer;
    }

    Bytes& operator<<(Bytes& buffer, SubscribeAnnouncesErrorCodeEnum value)
    {
        buffer << static_cast<std::uint64_t>(value);
        return buffer;
    }

    BytesSpan operator>>(BytesSpan buffer, SubscribeAnnouncesErrorCodeEnum& value)
    {
        std::uint64_t uvalue;
        buffer = buffer >> uvalue;
        value = static_cast<SubscribeAnnouncesErrorCodeEnum>(uvalue);
        return buffer;
    }

    Bytes& operator<<(Bytes& buffer, FetchErrorCodeEnum value)
    {
        buffer << static_cast<std::uint64_t>(value);
        return buffer;
    }

    BytesSpan operator>>(BytesSpan buffer, FetchErrorCodeEnum& value)
    {
        std::uint64_t uvalue;
        buffer = buffer >> uvalue;
        value = static_cast<FetchErrorCodeEnum>(uvalue);
        return buffer;
    }

    Bytes& operator<<(Bytes& buffer, SubscribeDoneStatusCodeEnum value)
    {
        buffer << static_cast<std::uint64_t>(value);
        return buffer;
    }

    BytesSpan operator>>(BytesSpan buffer, SubscribeDoneStatusCodeEnum& value)
    {
        std::uint64_t uvalue;
        buffer = buffer >> uvalue;
        value = static_cast<SubscribeDoneStatusCodeEnum>(uvalue);
        return buffer;
    }

    Bytes& operator<<(Bytes& buffer, SubscribeErrorCodeEnum value)
    {
        buffer << static_cast<std::uint64_t>(value);
        return buffer;
    }

    BytesSpan operator>>(BytesSpan buffer, SubscribeErrorCodeEnum& value)
    {
        std::uint64_t uvalue;
        buffer = buffer >> uvalue;
        value = static_cast<SubscribeErrorCodeEnum>(uvalue);
        return buffer;
    }

}
