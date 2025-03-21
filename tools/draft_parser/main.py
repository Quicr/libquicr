"""MOQ Draft Parser"""

import sys
from moqt_parser import ProtocolMessageParser, CodeGenerator


def main(rfc_filename, output_name):
    """Main."""

    type_map = {
        "i": "std::uint64_t",  # variable-length integer
        "8": "std::uint8_t",
        "16": "std::uint16_t",
        "32": "std::uint32_t",
        "64": "std::uint64_t",
        "Start Group": "quicr::ctrl_messages::GroupId",
        "End Group": "quicr::ctrl_messages::GroupId",
        "Start Object": "quicr::ctrl_messages::ObjectId",
        "End Object": "quicr::ctrl_messages::ObjectId",
        "Track Namespace": "quicr::TrackNamespace",
        "Track Name": "quicr::Bytes",
        "Track Namespace Prefix": "quicr::TrackNamespace",
        "Subscribe Parameters": "quicr::ctrl_messages::Parameter",
        "Setup Parameters": "quicr::ctrl_messages::Parameter",
        "New Session URI": "quicr::Bytes",
        "Parameters": "quicr::ctrl_messages::Parameter",
        "Reason Phrase": "quicr::Bytes",
        "Filter Type": "quicr::ctrl_messages::FilterTypeEnum",
        "Group Order": "quicr::ctrl_messages::GroupOrderEnum",
        "Fetch Type": "quicr::ctrl_messages::FetchTypeEnum",
        "SubscribeDone::StatusCode": "quicr::ctrl_messages::SubscribeDoneStatusCodeEnum",
        "SubscribeError::ErrorCode": "quicr::ctrl_messages::SubscribeErrorCodeEnum",
        "AnnounceError::ErrorCode": "quicr::ctrl_messages::AnnounceErrorCodeEnum",
        "AnnounceCancel::ErrorCode": "quicr::ctrl_messages::AnnounceErrorCodeEnum",
        "SubscribeAnnouncesError::ErrorCode": "quicr::ctrl_messages::SubscribeAnnouncesErrorCodeEnum",
        "FetchError::ErrorCode": "quicr::ctrl_messages::FetchErrorCodeEnum",
    }

    field_discards = ["Type", "Length"]

    parser = ProtocolMessageParser(type_map)
    generator = CodeGenerator("cpp")
    messages = []

    with open(rfc_filename, "r", encoding="utf8") as rfc_file:
        spec = rfc_file.read()
        messages = parser.parse_messages(spec)

        using_map = {}
        for message in messages:
            for field in message.fields:
                if field.group_fields:
                    for group_field in field.group_fields:
                        if group_field.cpp_using_name is not None:
                            using_map[group_field.cpp_using_name] = group_field
                        else:
                            print("gropu_file.cpp_using_name is None")

                # if field.cpp_using_name is not None:
                if field.spec_name not in field_discards:
                    if field.is_optional is False:
                        using_map[field.cpp_using_name] = field

        if len(messages) > 0:
            with open(f"{output_name}.h", "w", encoding="utf8") as header_file:
                output = generator.generate_header(
                    messages, using_map, field_discards
                )
                print(output, file=header_file)
            with open(
                f"{output_name}.cpp", "w", encoding="utf8"
            ) as source_file:
                output = generator.generate_source(
                    messages, using_map, field_discards
                )
                print(output, file=source_file)
        else:
            print("Protocol parser returned empty parsed messsages list.")


if __name__ == "__main__":
    if len(sys.argv) != 3:
        print("Usage error - rfc filename required:")
        print(f"    {sys.argv[0]} rfc_filename output_name")
    else:
        main(sys.argv[1], sys.argv[2])
