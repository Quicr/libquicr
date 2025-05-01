#include "quicr/detail/ctrl_message_types.h"

namespace quicr::messages {

    Bytes& operator<<(Bytes& buffer, const Bytes& bytes)
    {
        buffer << static_cast<std::uint64_t>(bytes.size()); // length of byte span
        buffer.insert(buffer.end(), bytes.begin(), bytes.end());
        return buffer;
    }

    Bytes& operator<<(Bytes& buffer, const BytesSpan& bytes)
    {
        buffer << static_cast<std::uint64_t>(bytes.size()); // length of byte span
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

    Bytes& operator<<(Bytes& buffer, std::uint64_t value)
    {
        UintVar varint = value;
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
        value = buffer.front();
        return buffer.subspan(sizeof(value));
    }

    BytesSpan operator>>(BytesSpan buffer, uint64_t& value)
    {
        UintVar value_uv(buffer);
        value = static_cast<uint64_t>(value_uv);
        return buffer.subspan(value_uv.size());
    }

    Bytes& operator<<(Bytes& buffer, GroupOrder value)
    {
        buffer << static_cast<std::uint64_t>(value);
        return buffer;
    }

    BytesSpan operator>>(BytesSpan buffer, GroupOrder& value)
    {
        std::uint64_t uvalue;
        buffer = buffer >> uvalue;
        value = static_cast<GroupOrder>(uvalue);
        return buffer;
    }

    Bytes& operator<<(Bytes& buffer, FetchType value)
    {
        buffer << static_cast<std::uint64_t>(value);
        return buffer;
    }

    BytesSpan operator>>(BytesSpan buffer, FetchType& value)
    {
        std::uint8_t uvalue;
        buffer = buffer >> uvalue;
        value = static_cast<FetchType>(uvalue);
        return buffer;
    }

    Bytes& operator<<(Bytes& buffer, FetchErrorCode value)
    {
        buffer << static_cast<std::uint64_t>(value);
        return buffer;
    }

    BytesSpan operator>>(BytesSpan buffer, FetchErrorCode& value)
    {
        std::uint64_t uvalue;
        buffer = buffer >> uvalue;
        value = static_cast<FetchErrorCode>(uvalue);
        return buffer;
    }

    Bytes& operator<<(Bytes& buffer, SubscribeDoneStatusCode value)
    {
        buffer << static_cast<std::uint64_t>(value);
        return buffer;
    }

    BytesSpan operator>>(BytesSpan buffer, SubscribeDoneStatusCode& value)
    {
        std::uint64_t uvalue;
        buffer = buffer >> uvalue;
        value = static_cast<SubscribeDoneStatusCode>(uvalue);
        return buffer;
    }

    Bytes& operator<<(Bytes& buffer, SubscribeErrorCode value)
    {
        buffer << static_cast<std::uint64_t>(value);
        return buffer;
    }

    BytesSpan operator>>(BytesSpan buffer, SubscribeErrorCode& value)
    {
        std::uint64_t uvalue;
        buffer = buffer >> uvalue;
        value = static_cast<SubscribeErrorCode>(uvalue);
        return buffer;
    }

    Bytes& operator<<(Bytes& buffer, const Location& location)
    {
        buffer << UintVar(location.group);
        buffer << UintVar(location.object);
        return buffer;
    }

    BytesSpan operator>>(BytesSpan buffer, Location& location)
    {
        buffer = buffer >> location.group;
        buffer = buffer >> location.object;
        return buffer;
    }

}
