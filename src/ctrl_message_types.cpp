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

    Bytes& operator<<(Bytes& buffer, std::uint16_t value)
    {
        buffer.push_back(static_cast<uint8_t>(value >> 8 & 0xFF));
        buffer.push_back(static_cast<uint8_t>(value & 0xFF));
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
        if (buffer.size() < sizeof(value)) {
            throw std::invalid_argument("Provided buffer too small");
        }
        value = buffer.front();
        return buffer.subspan(sizeof(value));
    }

    BytesSpan operator>>(BytesSpan buffer, uint16_t& value)
    {
        if (buffer.size() < sizeof(value)) {
            throw std::invalid_argument("Provided buffer too small");
        }
        std::memcpy(&value, buffer.data(), sizeof(value));
        value = SwapBytes(value);
        return buffer.subspan(sizeof(std::uint16_t));
    }

    BytesSpan operator>>(BytesSpan buffer, uint64_t& value)
    {
        UintVar value_uv(buffer);
        value = static_cast<uint64_t>(value_uv);
        return buffer.subspan(value_uv.size());
    }

    Bytes& operator<<(Bytes& buffer, GroupOrder value)
    {
        if (value != GroupOrder::kAscending && value != GroupOrder::kDescending) {
            throw ProtocolViolationException("Invalid group order");
        }
        buffer << static_cast<std::uint64_t>(value);
        return buffer;
    }

    BytesSpan operator>>(BytesSpan buffer, GroupOrder& value)
    {
        std::uint64_t uvalue;
        buffer = buffer >> uvalue;
        if (uvalue < static_cast<std::uint64_t>(GroupOrder::kAscending) ||
            uvalue > static_cast<std::uint64_t>(GroupOrder::kDescending)) {
            throw ProtocolViolationException("Invalid group order");
        }
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

    Bytes& operator<<(Bytes& buffer, PublishDoneStatusCode value)
    {
        buffer << static_cast<std::uint64_t>(value);
        return buffer;
    }

    BytesSpan operator>>(BytesSpan buffer, PublishDoneStatusCode& value)
    {
        std::uint64_t uvalue;
        buffer = buffer >> uvalue;
        value = static_cast<PublishDoneStatusCode>(uvalue);
        return buffer;
    }

    Bytes& operator<<(Bytes& buffer, ErrorCode value)
    {
        buffer << static_cast<std::uint64_t>(value);
        return buffer;
    }

    BytesSpan operator>>(BytesSpan buffer, ErrorCode& value)
    {
        std::uint64_t uvalue;
        buffer = buffer >> uvalue;
        value = static_cast<ErrorCode>(uvalue);
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

    namespace {

        void SerializeKeyValuePairSequence(Bytes& buffer, std::vector<KeyValuePair<std::uint64_t>> kvps)
        {
            std::ranges::sort(kvps, [](const auto& a, const auto& b) { return a.type < b.type; });

            std::uint64_t prev_type = 0;
            for (const auto& kvp : kvps) {
                SerializeKvp(buffer, kvp, prev_type);
                prev_type = kvp.type;
            }
        }

        BytesSpan ParseKeyValuePairSequence(BytesSpan buffer, std::map<std::uint64_t, Bytes>& options)
        {
            options.clear();
            std::uint64_t prev_type = 0;
            while (!buffer.empty()) {
                KeyValuePair<std::uint64_t> kvp;
                ParseKvp(buffer, kvp, prev_type);
                prev_type = kvp.type;
                options[kvp.type] = std::move(kvp.value);
            }

            return buffer;
        }

    } // namespace

    Bytes& operator<<(Bytes& buffer, const KeyValueAttributes& attributes)
    {
        std::vector<KeyValuePair<std::uint64_t>> kvps;
        kvps.reserve(attributes.Attributes().size());
        for (const auto& [key, value] : attributes.Attributes()) {
            kvps.push_back(KeyValuePair<std::uint64_t>{ key, value });
        }

        SerializeKeyValuePairSequence(buffer, std::move(kvps));
        return buffer;
    }

    BytesSpan operator>>(BytesSpan buffer, KeyValueAttributes& attributes)
    {
        std::map<std::uint64_t, Bytes> parsed;
        buffer = ParseKeyValuePairSequence(buffer, parsed);
        attributes = KeyValueAttributes{ parsed };
        return buffer;
    }

    Bytes& operator<<(Bytes& buffer, const TrackExtensions& extensions)
    {
        // Serialize immutable blob first.
        constexpr auto immutable_key = static_cast<std::uint64_t>(ExtensionType::kImmutable);
        Bytes immutable_value_bytes;
        if (!extensions.immutable_extensions.empty()) {
            std::vector<KeyValuePair<std::uint64_t>> immutable_kvps;
            immutable_kvps.reserve(extensions.immutable_extensions.size());
            for (const auto& [imm_type, imm_value] : extensions.immutable_extensions) {
                immutable_kvps.push_back(KeyValuePair<std::uint64_t>{ imm_type, imm_value });
            }
            SerializeKeyValuePairSequence(immutable_value_bytes, std::move(immutable_kvps));
        }

        // Build KVPs.
        std::vector<KeyValuePair<std::uint64_t>> kvps;
        for (const auto& [key, value] : extensions) {
            if (key == immutable_key) {
                continue;
            }
            kvps.push_back(KeyValuePair<std::uint64_t>{ key, value });
        }

        // Add in immutable.
        if (!immutable_value_bytes.empty()) {
            kvps.push_back(KeyValuePair<std::uint64_t>{ immutable_key, immutable_value_bytes });
        }

        SerializeKeyValuePairSequence(buffer, std::move(kvps));

        return buffer;
    }

    BytesSpan operator>>(BytesSpan buffer, TrackExtensions& extensions)
    {
        extensions.extensions.clear();
        extensions.immutable_extensions.clear();
        buffer = ParseKeyValuePairSequence(buffer, extensions.extensions);

        // Immutable extensions.
        if (extensions.extensions.contains(static_cast<std::uint64_t>(ExtensionType::kImmutable))) {
            auto immutable_bytes = extensions.extensions.at(static_cast<std::uint64_t>(ExtensionType::kImmutable));
            ParseKeyValuePairSequence(immutable_bytes, extensions.immutable_extensions);
        }

        return buffer;
    }
}
namespace quicr {
    using namespace quicr::messages;

    Bytes& operator<<(Bytes& buffer, const TrackNamespace& ns)
    {
        const auto& entries = ns.GetEntries();

        buffer << UintVar(entries.size());
        for (const auto& entry : entries) {
            buffer << entry;
        }

        return buffer;
    }

    BytesSpan operator>>(BytesSpan buffer, TrackNamespace& msg)
    {
        uint64_t size = 0;
        buffer = buffer >> size;

        std::vector<Bytes> entries(size);
        for (auto& entry : entries) {

            buffer = buffer >> entry;
        }

        msg = TrackNamespace{ entries };

        return buffer;
    }
}
