"""Define MessageSpec Class and Field Class"""

from typing import List
from dataclasses import dataclass


@dataclass
class Field:
    """Messgae field class."""

    name: str
    field_type: str
    spec_name: str
    spec_type: str = None
    cpp_type: str = None
    cpp_using_type: str = None
    cpp_using_name: str = None
    length: str = None
    default_value: str = None
    is_optional: bool = False
    is_repeated: bool = False
    is_variable_length: bool = False
    group_name: str = None
    group_fields: List[("Field")] = None


class MessageSpec:
    """Messgae specification class."""

    def __init__(
        self,
        name: str,
        spec_name: str,
        message_type: str,
        message_enum: int,
        fields: List[Field],
        optional_groups: List[str],
    ):
        """Protocol message spec constructor."""
        self.name = name
        self.spec_name = spec_name
        self.message_type = message_type
        self.message_enum = message_enum
        # self.spec_name = name
        self.fields = fields
        self.optional_groups = optional_groups
