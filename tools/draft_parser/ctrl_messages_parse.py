"""MOQ RFC Parser/Generator."""

import re
import sys
from typing import List, Tuple
from dataclasses import dataclass
from contextlib import redirect_stdout


@dataclass
class Field:
    """Messgae field class."""

    name: str
    field_type: str
    length: str = None
    is_optional: bool = False
    is_repeated: bool = False
    default_value: str = None
    is_variable_length: bool = False
    group_name: str = None  # Added to track optional group membership


class MessageSpec:
    """Messgae specification class."""

    def __init__(
        self, name: str, fields: List[Field], optional_groups: List[str]
    ):
        """Protocol message spec constructor."""
        self.name = name.title().replace("_", "")
        self.spec_name = name
        self.fields = fields
        self.optional_groups = optional_groups


class ProtocolMessageParser:
    """Messgae specification parser."""

    def __init__(self):
        """Protocol message parser constructor."""
        self.field_type_map = {
            "i": "std::uint64_t",  # variable-length integer
            "b": "std::vector<uint8_t>",  # length-prefixed binary data
            "tuple": "tuple - UNKNOWN",  # tuple type
            "8": "std::uint8_t",
            "16": "std::uint16_t",
            "32": "std::uint32_t",
            "64": "std::uint64_t",
            "Track Namespace": "TrackNamespace",
            "Track Name": "TrackName",
            "Track Namespace Prefix": "TrackNamespacePrefix",
            "Subscribe Parameters": "Parameter",
            "Setup Parameters": "Parameter",
            "New Session URI": "NewSessionURI",
            "Parameters": "Parameter",
            "Reason Phrase": "ReasonPhrase",
        }

    def parse_field_definition(
        self, line: str, group_name: str = None
    ) -> Field:
        """Parse a single field definition line."""
        # Remove comments and whitespace
        line = line.split("#")[0].strip().rstrip(",")
        if not line:
            return None

        # Match field pattern
        pattern = r"^(?:\[)?([A-Za-z_][A-Za-z0-9_\s]*?)\s*\(([^)]+)\)(?:\s*=\s*([^,]+))?(?:\s*\.\.\.)?\]?"
        match = re.match(pattern, line)
        if not match:
            return None

        name, field_type, default_value = match.groups()
        name = name.strip()
        field_type = field_type.strip()

        is_optional = line.startswith("[") or group_name is not None
        is_repeated = "..." in line

        is_variable_length = field_type == ".."
        # is_variable_length = field_type == "tuple"

        # variable length - we have created our own type
        # the name of the type is the name of this field
        if is_variable_length:
            field_type = name

        if field_type == "tuple":
            field_type = name

        return Field(
            name=name.lower().replace(" ", "_"),
            field_type=field_type,
            default_value=default_value.strip() if default_value else None,
            is_optional=is_optional,
            is_repeated=is_repeated,
            is_variable_length=is_variable_length,
            group_name=group_name,
        )

    def generate_cpp_type(self, field: Field) -> str:
        """Generate appropriate C++ type for a field."""
        base_type = self.field_type_map.get(field.field_type, "uint64_t")

        if field.is_repeated:
            base_type = f"std::vector<{base_type}>"

        return base_type

    def parse_message(self, message_text: str) -> MessageSpec:
        """Parse a complete message definition and generate C++ struct."""
        lines = message_text.strip().split("\n")

        # Extract message name
        message_name = lines[0].split("{")[0].strip()

        # Parse fields and track optional groups
        fields = []
        optional_groups = {}
        current_group = None
        group_fields = []

        lines = [
            line.strip()
            for line in lines[1:]
            if line.strip() and line.strip() != "}"
        ]

        i = 0
        while i < len(lines):
            line = lines[i]

            # Check for start of optional group
            if line.startswith("["):
                current_group = f"OptionalGroup_{len(optional_groups)}"
                group_fields = []

                # Continue until we find the closing bracket
                while i < len(lines):
                    line = lines[i].strip().rstrip(",")
                    if line.endswith("]"):
                        # Remove the closing bracket and parse the last field
                        line = line[:-1].strip()
                        if (
                            line
                        ):  # If there's content before the closing bracket
                            field = self.parse_field_definition(
                                line, current_group
                            )
                            if field:
                                group_fields.append(field)
                                fields.append(field)
                        break
                    elif line.startswith("["):
                        # Remove the opening bracket for the first line
                        line = line[1:].strip()

                    field = self.parse_field_definition(line, current_group)
                    if field:
                        if field.is_variable_length:
                            group_fields.pop()
                        group_fields.append(field)
                        fields.append(field)
                    i += 1

                optional_groups[current_group] = group_fields
                current_group = None
            else:
                field = self.parse_field_definition(line)
                if field:
                    if field.is_repeated:
                        # NOTE: this field is a vector (is_repeated). Assuming previous field is the
                        # size of the vector. Since C++ vectors include a size - remove the previous
                        # field.
                        fields.pop()

                    if field.is_variable_length:
                        fields.pop()
                    fields.append(field)
            i += 1

        message = MessageSpec(
            message_name.replace(" Message", ""), fields, optional_groups
        )
        return message

    def generate_cpp_enum(self, message: MessageSpec) -> Tuple[str, int]:
        """Generate enum tuple for message."""
        for field in message.fields:
            if field.name == "type":
                return [f"k{message.name}", int(field.default_value, 16)]

    def generate_cpp_struct(self, message: MessageSpec) -> str:
        """Generate cpp struct for message."""
        cpp_struct = f"""
    struct {message.name} {{
"""
        # Generate optional group structs first
        for group_name, group_fields in message.optional_groups.items():
            cpp_struct += f"""
        struct {group_name} {{"""
            for field in group_fields:
                cpp_type = self.generate_cpp_type(field)
                cpp_struct += f"""
            {cpp_type} {field.name}"""
                if field.default_value:
                    cpp_struct += f""" = {field.default_value};"""
                else:
                    cpp_struct += """;"""
            cpp_struct += """
        };
"""

        # Add regular fields
        prev_group_name = None
        for field in message.fields:
            if field.name == "type":
                # skip the type
                continue
            if field.name == "length":
                # skip the length
                continue

            # a group is inherenetly optional
            # we have already generated the group structure
            # generate a 'field' of an optional group
            if field.group_name is not None:
                if prev_group_name == field.group_name:
                    # ignore follow on fields that are in the same group
                    pass
                else:
                    # new group
                    prev_group_name = field.group_name
                    cpp_struct += f"""
        std::optional<{field.group_name}> {field.group_name.lower()};
"""
            # not a group - just a regular field
            else:
                cpp_type = self.generate_cpp_type(field)
                cpp_struct += f"""        {cpp_type} {field.name}"""
                if field.default_value:
                    cpp_struct += f""" = {field.default_value}"""
                cpp_struct += ";\n"

        cpp_struct += f"""
    }}; // {message.name}
"""

        # add streaming operator declaration
        cpp_struct += f"""
    // decode {message.spec_name}
    BytesSpan operator>>(BytesSpan buffer, {message.name}& msg);
    // encode {message.spec_name}
    Bytes& operator<<(Bytes& buffer, const {message.name}& msg);
"""
        return cpp_struct

    def generate_cpp_encode(self, message: MessageSpec) -> str:
        """Generate cpp encode function."""
        cpp_encoding = f"""
// {message.spec_name}  encoder
Bytes& operator<<(Bytes& buffer, const {message.name}& msg)
{{
    Bytes payload;

    // fill out payload"""
        prev_group_name = None
        for field in message.fields:
            base_type = self.field_type_map.get(field.field_type, "uint64_t")
            if field.name == "type":
                # skip the type
                continue
            if field.name == "length":
                # skip the length
                continue

            if prev_group_name is not None:
                if field.group_name != prev_group_name:
                    # close out previous group
                    cpp_encoding += """
    }"""

            if field.is_repeated:
                cpp_encoding += f"""
    {{  // {field.name} vector
        payload << UintVar(msg.{field.name}.size()); // std::size_t        
        for (const auto& element : msg.{field.name}) {{
            payload << element; // {base_type}
        }}
    }}"""
            elif field.group_name is not None:
                if field.group_name == prev_group_name:
                    # same group
                    cpp_encoding += f"""
        payload << msg.{field.group_name.lower()}->{field.name};  // optional {base_type}"""
                else:
                    # new group
                    prev_group_name = field.group_name
                    cpp_encoding += f"""
    if (msg.{field.group_name.lower()}.has_value()) {{
        payload << msg.{field.group_name.lower()}->{field.name}; // optional {base_type}"""
            # }}"""
            else:
                cpp_encoding += f"""
    payload << msg.{field.name}; // {base_type}"""

        cpp_encoding += f"""

    // fill out buffer
    buffer << ControlMessageType::{message.spec_name};
    buffer << payload.size();
    buffer << payload;
    return buffer;"""

        cpp_encoding += """
}
"""
        return cpp_encoding

    def generate_cpp_decode(self, message: MessageSpec) -> str:
        """Generate cpp decode function."""
        cpp_decoding = f"""
// {message.spec_name}  decoder
BytesSpan operator>>(BytesSpan buffer, {message.name}& msg)
{{"""
        prev_group_name = None
        for field in message.fields:
            base_type = self.field_type_map.get(field.field_type, "uint64_t")
            if field.name == "type":
                # skip the type
                continue
            if field.name == "length":
                # skip the length
                continue

            if prev_group_name is not None:
                if field.group_name != prev_group_name:
                    # close out previous group
                    cpp_decoding += f"""
        msg.{prev_group_name.lower()} = std::make_optional({prev_group_name.lower()});
    }}"""

            if field.is_repeated:
                cpp_decoding += f"""
    {{ // {field.name} vector
        std::uint64_t vector_size;
        buffer = buffer >> vector_size;
        for (std::uint64_t  i = 0; i < vector_size; ++i) {{
            {base_type} temp_variable;
            buffer = buffer >> temp_variable; // {base_type}
            msg.{field.name}.push_back(temp_variable);
        }}
    }}
    """
            elif field.group_name is not None:
                if field.group_name == prev_group_name:
                    # same group
                    cpp_decoding += f"""
        buffer = buffer >> {field.group_name.lower()}.{field.name};  // optional {base_type}"""
                else:
                    # new group
                    prev_group_name = field.group_name
                    cpp_decoding += f"""
    #error "Contional code needs to be update manually."
    if (/* condition - goes -here */ )) {{
        {message.name}::{field.group_name} {field.group_name.lower()};
        buffer = buffer >> {field.group_name.lower()}.{field.name}; // optional {base_type}"""
            else:
                cpp_decoding += f"""
    buffer = buffer >> msg.{field.name}; // {base_type}"""

        cpp_decoding += """
    return buffer;
}
"""
        return cpp_decoding

    def parse_messages(self, text: str) -> List[str]:
        """Parse multiple message definitions from text."""
        # Split text into individual message definitions
        message_pattern = r"([A-Za-z_][A-Za-z0-9_]*\s*Message\s*\{[^}]+\})"
        messages = re.finditer(message_pattern, text, re.MULTILINE | re.DOTALL)
        return [self.parse_message(m.group(1)) for m in messages]

    def print_header(self, header_file, messages: List[MessageSpec]):
        """Iterate through parsed messages and generate header file."""
        cpp_header = ""
        with redirect_stdout(header_file):
            cpp_header += """
#pragma once
                  
#include "quicr/common.h"
#include "quicr/object.h"
#include "quicr/track_name.h"
#include "stream_buffer.h"

#include <map>
#include <string>
#include <vector>

namespace quicr::messages {                  

    Bytes& operator<<(Bytes& buffer, BytesSpan bytes);
    Bytes& operator<<(Bytes& buffer, ControlMessageType message_type);
    Bytes& operator<<(Bytes& buffer, std::size_t value);
    Bytes& operator<<(Bytes& buffer, std::uint8_t value);

    BytesSpan operator>>(BytesSpan buffer, std::size_t& value);
    BytesSpan operator>>(BytesSpan buffer, Bytes& value);
    BytesSpan operator>>(BytesSpan buffer, uint64_t& value);
    BytesSpan operator>>(BytesSpan buffer, uint8_t& value);

    struct Parameter
    {
        uint64_t type{ 0 };
        uint64_t length{ 0 };
        Bytes value;
        template<class StreamBufferType>
        friend bool operator>>(StreamBufferType& buffer, MoqParameter& msg);
    };

    BytesSpan operator>>(BytesSpan buffer, MoqParameter& msg);
    Bytes& operator<<(Bytes& buffer, const MoqParameter& msg);

    //
    // Namespace
    //

    BytesSpan operator>>(BytesSpan buffer, TrackNamespace& msg);
    Bytes& operator<<(Bytes& buffer, const TrackNamespace& msg);                                    
"""
            message_enums = []
            for message in messages:
                enum_tuple = self.generate_cpp_enum(message)
                if enum_tuple is not None:
                    message_enums.append(enum_tuple)
            sorted_message_enums = sorted(message_enums, key=lambda x: x[1])
            cpp_header += """
    enum class ControlMessageType : uint64_t
    {"""
            for enum_tuple in sorted_message_enums:
                cpp_header += f"""
        {enum_tuple[0]} = {hex(enum_tuple[1])}"""
            cpp_header += """
    }; // ControlMessageType
"""

            for message in messages:
                cpp_header += self.generate_cpp_struct(message)

            cpp_header += """
} // namespace
"""

            print(cpp_header)

    def print_source(
        self, source_file, messages: List[MessageSpec], output_name: str
    ):
        """Iterate through parsed message and gererate source file."""
        ...
        with redirect_stdout(source_file):
            cpp_source = f"""
#include "{output_name}.h"

using namespace quicr::messages;

Bytes& operator<<(Bytes& buffer, BytesSpan bytes)
{{
    buffer.insert(buffer.end(), bytes.begin(), bytes.end());
    return buffer;
}}

Bytes& operator<<(Bytes& buffer, ControlMessageType message_type)
{{
    UintVar varint = static_cast<std::uint64_t>(message_type);
    buffer << varint;
    return buffer;
}}

Bytes& operator<<(Bytes& buffer, std::size_t value)
{{
    UintVar varint = static_cast<std::uint64_t>(value);
    buffer << varint;
    return buffer;
}}

Bytes& operator<<(Bytes& buffer, std::uint8_t value)
{{
    // assign 8 bits - not a varint
    buffer.push_back(value);
    return buffer;
}}

BytesSpan operator>>(BytesSpan buffer, uint64_t& value)
{{
    UintVar value_uv(buffer);
    value = static_cast<uint64_t>(value_uv);
    return buffer.subspan(value_uv.size());
}}

BytesSpan operator>>(BytesSpan buffer, uint8_t& value)
{{
    // need 8 bits - not a varint
    value = buffer[0];
    return buffer.subspan(sizeof(value));
}}

BytesSpan operator>>(BytesSpan buffer, Bytes& value)
{{
    uint64_t size = 0;
    buffer = buffer >> size;
    value.assign(buffer.begin(), std::next(buffer.begin(), size));
    return buffer.subspan(value.size());
}}


"""

            for message in messages:
                cpp_source += self.generate_cpp_encode(message)
                cpp_source += self.generate_cpp_decode(message)

            print(cpp_source)


def main(rfc_filename, output_name):
    """Main."""
    parser = ProtocolMessageParser()

    messages = []

    with open(rfc_filename, "r", encoding="utf8") as rfc_file:
        spec = rfc_file.read()
        messages = parser.parse_messages(spec)

        if len(messages) > 0:
            with open(f"{output_name}.h", "w", encoding="utf8") as header_file:
                parser.print_header(header_file, messages)
                print(f"Generated: {output_name}.h")
            with open(
                f"{output_name}.cpp", "w", encoding="utf8"
            ) as source_file:
                parser.print_source(source_file, messages, output_name)
                print(f"Generated: {output_name}.cpp")
        else:
            print("Protocol parser returned empty parsed messsages list.")


if __name__ == "__main__":
    if len(sys.argv) != 3:
        print("Usage error - rfc filename required:")
        print(f"    {sys.argv[0]} rfc_filename output_name")
    else:
        main(sys.argv[1], sys.argv[2])
