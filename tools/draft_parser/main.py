"""MOQ Draft Parser"""

import os
import sys
from moqt_parser import ProtocolMessageParser, CodeGenerator  # , TableParser


def main(rfc_filename, output_name):
    """Main."""

    type_map = {
        "i": "std::uint64_t",  # variable-length integer
        "8": "std::uint8_t",
        "Track Namespace": "quicr::TrackNamespace",
        "Track Name": "quicr::messages::TrackName",
        "Track Namespace Prefix": "quicr::TrackNamespace",
        "Setup Parameters": "quicr::messages::SetupParameter",
        "New Session URI": "quicr::Bytes",
        "Parameters": "quicr::messages::Parameter",
        "Reason Phrase": "quicr::Bytes",
        "Filter Type": "quicr::messages::FilterType",
        "Group Order": "quicr::messages::GroupOrder",
        "Fetch Type": "quicr::messages::FetchType",
        "PublishDone::StatusCode": "quicr::messages::PublishDoneStatusCode",
        "SubscribeError::ErrorCode": "quicr::messages::SubscribeErrorCode",
        "PublishNamespaceError::ErrorCode": "quicr::messages::PublishNamespaceErrorCode",
        "SubscribeNamespaceError::ErrorCode": "quicr::messages::SubscribeNamespaceErrorCode",
        "FetchError::ErrorCode": "quicr::messages::FetchErrorCode",
        "Start Location": "quicr::messages::Location",
        "End Location": "quicr::messages::Location",
        "Largest Location": "quicr::messages::Location",
        "Standalone": "quicr::messages::StandaloneFetch",
        "Joining": "quicr::messages::JoiningFetch",
    }

    field_discards = ["Type", "Length"]

    parser = ProtocolMessageParser(type_map)
    # table_parser = TableParser()
    filename = os.path.basename(rfc_filename)
    generator = CodeGenerator("cpp", filename)
    messages = []

    with open(rfc_filename, "r", encoding="utf8") as rfc_file:
        spec = rfc_file.read()
        messages = parser.parse_messages(spec)

        # tables = table_parser.parse_tables(spec)
        # print(tables)
        # for table in tables:
        # print(table)

        using_map = {}
        repeat_using_map = {}
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

        for using in using_map:
            field = using_map[using]
            if field.is_repeated:
                repeat_using_map[using_map[using].cpp_using_type] = field

        if len(messages) > 0:
            with open(f"{output_name}.h", "w", encoding="utf8") as header_file:
                output = generator.generate_header(
                    messages, using_map, repeat_using_map, field_discards
                )
                print(output, file=header_file)
            with open(
                f"{output_name}.cpp", "w", encoding="utf8"
            ) as source_file:
                output = generator.generate_source(
                    messages, using_map, repeat_using_map, field_discards
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
