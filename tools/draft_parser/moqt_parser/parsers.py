"""MOQ RFC Parser/Generator."""

import re
from typing import List, Dict
from .message_spec import MessageSpec, Field


class ProtocolMessageParser:
    """Messgae specification parser."""

    def __init__(self, type_map: Dict):
        """Protocol message parser constructor."""
        self.field_type_map = type_map

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

        name, parsed_field_type, default_value = match.groups()
        name = name.strip()
        parsed_field_type = parsed_field_type.strip()
        field_type = parsed_field_type
        spec_type = parsed_field_type
        cpp_type = None

        is_optional = line.startswith("[") or group_name is not None
        is_repeated = "..." in line
        is_variable_length = parsed_field_type == ".."

        if is_variable_length:
            cpp_type = self.field_type_map.get(
                name, f'#error "field_type_map look error for {name}"'
            )  # rename type to the name
            field_type = cpp_type

        elif field_type == "tuple":
            cpp_type = self.field_type_map.get(
                name, f'#error "field_type_map look error for {name}"'
            )  # rename type to the name
            field_type = cpp_type

        else:
            if name in self.field_type_map:
                cpp_type = self.field_type_map[name]
            else:
                cpp_type = self.field_type_map.get(
                    spec_type,
                    '#error "field_type_map look error for {spec_type}"',
                )

        cpp_using_type = None
        cpp_using_name = None
        if is_repeated:  # this is a vector in C++
            cpp_using_type = f"std::vector<{cpp_type}>"
            cpp_using_name = name.replace(" ", "")

        elif is_variable_length:  # this is a vector in C++
            cpp_using_type = f"{cpp_type}"
            cpp_using_name = name.replace(" ", "")

        else:
            cpp_using_type = f"{cpp_type}"
            cpp_using_name = name.replace(" ", "")

        return Field(
            name=name.lower().replace(" ", "_"),
            field_type=field_type,
            cpp_type=cpp_type,
            cpp_using_type=cpp_using_type,
            cpp_using_name=cpp_using_name,
            spec_type=spec_type,
            default_value=default_value.strip() if default_value else None,
            is_optional=is_optional,
            is_repeated=is_repeated,
            is_variable_length=is_variable_length,
            group_name=group_name,
        )

    def parse_message(self, message_text: str) -> MessageSpec:
        """Parse a complete message definition and generate C++ struct."""
        lines = message_text.strip().split("\n")

        # Extract message name
        message_spec = lines[0].split("{")[0].strip().replace(" Message", "")
        message_name = message_spec.title().replace("_", "")
        message_type = message_name
        message_enum = None

        # Parse fields and track optional groups
        fields = []
        optional_groups = {}
        current_group = None
        current_group_field = None
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
                current_group = f"Group_{len(optional_groups)}"
                current_group_name = current_group.lower()
                current_group_type = f"{message_type}::{current_group}"
                group_fields = []
                current_group_field = Field(
                    name=current_group_name,
                    field_type=current_group_type,
                    cpp_type=current_group_type,
                    cpp_using_type=None,
                    cpp_using_name=f"{message_type}::{current_group}",
                    spec_type="optional group",
                    default_value=None,
                    is_optional=True,
                    is_repeated=False,
                    is_variable_length=False,
                    group_name=current_group,
                    group_fields=[],
                )

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
                            if field is not None:
                                if current_group_field is not None:
                                    current_group_field.group_fields.append(
                                        field
                                    )
                                    group_fields.append(field)
                        break
                    elif line.startswith("["):
                        # Remove the opening bracket for the first line
                        line = line[1:].strip()

                    field = self.parse_field_definition(line, current_group)
                    if field is not None:
                        if current_group_field is not None:
                            if field.is_variable_length:
                                current_group_field.group_fields.pop()
                                group_fields.pop()
                            current_group_field.group_fields.append(field)
                            group_fields.append(field)
                    i += 1

                fields.append(current_group_field)
                self.field_type_map[current_group_field.field_type] = (
                    current_group_field.field_type
                )
                optional_groups[current_group] = group_fields
                current_group = None
            else:
                field = self.parse_field_definition(line)
                if field.name == "type":
                    if field.default_value is not None:
                        message_enum = int(field.default_value, 16)
                if field is not None:
                    if field.is_repeated or field.is_variable_length:
                        # NOTE: this field is a vector (is_repeated). Assuming previous field is the
                        # size of the vector. Since C++ vectors include a size - remove the previous
                        # field.
                        fields.pop()

                    if field:
                        fields.append(field)
            i += 1

        if message_enum is None:
            print(f"Dropping {message_name}")
            return None
        message = MessageSpec(
            message_name,
            message_spec,
            message_type,
            message_enum,
            fields,
            optional_groups,
        )
        return message

    def parse_messages(self, text: str) -> List[str]:
        """Parse multiple message definitions from text."""
        # Split text into individual message definitions
        message_pattern = r"([A-Za-z_][A-Za-z0-9_]*\s*Message\s*\{[^}]+\})"
        messages = re.finditer(message_pattern, text, re.MULTILINE | re.DOTALL)

        message_list = []
        for m in messages:
            message = self.parse_message(m.group(1))
            if message is not None:
                message_list.append(message)

        return message_list
