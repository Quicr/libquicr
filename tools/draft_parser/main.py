import sys
from moqt_parser import ProtocolMessageParser, CodeGenerator


def main(rfc_filename, output_name):
    """Main."""
    parser = ProtocolMessageParser()
    generator = CodeGenerator("cpp")
    messages = []

    with open(rfc_filename, "r", encoding="utf8") as rfc_file:
        spec = rfc_file.read()
        messages = parser.parse_messages(spec)

        using_map = {}
        for message in messages:
            if message.name == "Fetch":
                print("fetch")
            for field in message.fields:
                # if field.name == "start_group":
                #    print(field)
                #    return
                # else:
                #    continue
                if field.group_fields:
                    for group_field in field.group_fields:
                        if group_field.cpp_using_name is not None:
                            using_map[group_field.cpp_using_name] = group_field
                        else:
                            print("gropu_file.cpp_using_name is None")

                # if field.cpp_using_name is not None:
                if field.name != "type" and field.name != "length":
                    if field.is_optional is False:
                        using_map[field.cpp_using_name] = field

        # for message in messages:
        #    if message.optional_groups is not None:
        #        print(message.optional_groups)
        # for message in messages:
        #    for field in message.fields:
        #        if field.is_optional:
        #            print(field)
        #        # print(f"{message.name} {field.name} {field.is_optional}")

        if len(messages) > 0:
            with open(f"{output_name}.h", "w", encoding="utf8") as header_file:
                output = generator.generate_header(messages, using_map)
                print(output, file=header_file)
            with open(
                f"{output_name}.cpp", "w", encoding="utf8"
            ) as source_file:
                output = generator.generate_source(messages, using_map)
                print(output, file=source_file)
        else:
            print("Protocol parser returned empty parsed messsages list.")


if __name__ == "__main__":
    if len(sys.argv) != 3:
        print("Usage error - rfc filename required:")
        print(f"    {sys.argv[0]} rfc_filename output_name")
    else:
        main(sys.argv[1], sys.argv[2])
