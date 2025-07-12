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
        self, message_name: str, line: str, group_name: str = None
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

        cpp_using_name = name.replace(" ", "")
        comp_name = f"{message_name}::{name.replace(' ','')}"
        if comp_name in self.field_type_map:
            cpp_type = self.field_type_map[comp_name]
            # name = f"{message_name}_{name.replace(" ","")}"
            cpp_using_name = f"{message_name}{name.replace(' ','')}"
        elif name in self.field_type_map:
            cpp_type = self.field_type_map[name]
        elif spec_type in self.field_type_map:
            cpp_type = self.field_type_map[spec_type]
        else:
            cpp_type = f'#error "field_type_map look error for {name}"'

        cpp_using_type = f"{cpp_type}"

        if is_repeated:  # this is a vector of {cpp_type} in C++
            cpp_using_type = f"std::vector<{cpp_type}>"

        return Field(
            name=name.lower().replace(" ", "_"),
            field_type=field_type,
            cpp_type=cpp_type,
            cpp_using_type=cpp_using_type,
            cpp_using_name=cpp_using_name,
            spec_name=name,
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
                    spec_name=current_group_name,
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
                                message_name, line, current_group
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

                    field = self.parse_field_definition(
                        message_name, line, current_group
                    )
                    if field is not None:
                        if current_group_field is not None:
                            if field.is_variable_length:
                                current_group_field.group_fields.pop()
                                prev_field = group_fields.pop()
                                field.variable_length_size_cpp_using_type = (
                                    prev_field.cpp_using_type
                                )
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
                field = self.parse_field_definition(message_name, line)
                if field.name == "type":
                    if field.default_value is not None:
                        message_enum = int(field.default_value, 16)
                if field is not None:
                    if field.is_repeated or field.is_variable_length:
                        # NOTE: this field is a vector (is_repeated). Assuming previous field is the
                        # size of the vector. Since C++ vectors include a size - remove the previous
                        # field.
                        prev_field = fields.pop()
                        field.variable_length_size_cpp_using_type = (
                            prev_field.cpp_using_type
                        )

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


class TableParser:
    def __init__(self):
        # This can be used to store any additional configuration if needed.
        pass

    def parse_tables(self, text):
        # Regular expressions to match the table structure and extract data
        # r"\s*(?:\+[-=]+\+[-=]+\+|\|\s*[^|]+\|\s*[^|]+\|)\s*"
        # r"^\s*(?:\+[-=]+\+[-=]+\+|\|\s*[^|]+\|\s*[^|]+\|)\s*$"
        table_header_pattern = (
            r"^\s*(?:\+[-=]+\+[-=]+\+|\|\s*([^|]+)\|\s*([^|]+)\|)\s*$"
        )
        row_pattern = r"\s*\|\s*([^|]+?)\s*\|\s*(.*?)\s*\|"

        # Extract all tables from the text
        matches = re.findall(table_header_pattern, text, re.MULTILINE)
        start_state = True
        title_state = False
        data_begin = False
        end_state = False
        for match in matches:
            if start_state is True:
                start_state = False
                title_state = True
                continue
            if title_state is True:
                title_state = False
                data_divider = True
                print(match)
                continue
            if data_divider is True:
                data_divider = False
                data_read = True
                continue
            if data_read is True:
                # check if match for second group...
                if len(match[0]) == 0:
                    data_read = True
                else:
                    data_read = False
                    print(match)
                data_read = False
                continue
            break

            print(match)
        return

        # Convert each match into a dictionary of rows
        tables = []
        for match in matches:
            # table_rows = re.findall(row_pattern, match.replace('|', ''))
            table_rows = re.findall(row_pattern, match)
            header = [
                item.strip()
                for item in match.split(" +")[1].split(" +")
                if item.strip()
            ]

            current_table = {}
            for row in table_rows:
                if not row:  # Skip empty lines
                    continue
                row_data = dict(
                    zip(
                        header,
                        (
                            item.strip().replace(" ", "_")
                            for item in row.split(" | ")
                        ),
                    )
                )
                current_table[row_data["Code"]] = row_data

            tables.append(current_table)

        return tables
