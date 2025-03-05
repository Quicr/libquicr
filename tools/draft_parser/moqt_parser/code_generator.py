from typing import List, Dict
from importlib import resources
from .message_spec import MessageSpec, Field
from jinja2 import Environment, FileSystemLoader


class CodeGenerator:
    """MessageSpec Code Generator"""

    def __init__(self, language: str):
        self.package_name = "moqt_parser"
        self.template_language = language
        self.package_path = resources.files(__package__)
        self.template_path = (
            f"{self.package_path}/templates/{self.template_language}"
        )
        self.template_env = Environment(
            loader=FileSystemLoader(self.template_path),
            trim_blocks=True,
            lstrip_blocks=True,
        )

    def generate_header_message_begin(self) -> str:
        gen_begin = ""
        template = self.template_env.get_template(
            "MessageSpec_Header_Begin_tmpl.jinja2"
        )
        gen_begin += template.render()
        return gen_begin

    def generate_header_message_end(self, using_map) -> str:
        gen_end = ""
        template = self.template_env.get_template(
            "MessageSpec_Header_End_tmpl.jinja2"
        )
        gen_end += template.render(using_map=using_map)
        return gen_end

    def generate_header_message_using(self, using_map: Dict) -> str:
        gen_usings = ""
        template = self.template_env.get_template(
            "MessageSpec_Header_Using_tmpl.jinja2"
        )
        gen_usings += template.render(using_map=using_map)
        return gen_usings

    def generate_header_message_enums(
        self, messages: List[MessageSpec]
    ) -> str:
        gen_enums = ""
        template = self.template_env.get_template(
            "MessageSpec_Header_Enums_tmpl.jinja2"
        )
        messages.sort(key=lambda message: message.message_enum, reverse=False)
        gen_enums += template.render(messages=messages)
        return gen_enums

    def generate_header_message_structs(
        self, messages: List[MessageSpec]
    ) -> str:
        gen = ""
        template = self.template_env.get_template(
            "MessageSpec_Header_Structs_tmpl.jinja2"
        )
        for message in messages:
            gen += template.render(message=message)
        return gen

    def generate_source_message_begin(self, message: List[MessageSpec]) -> str:
        gen_begin = ""
        template = self.template_env.get_template(
            "MessageSpec_Source_Begin_tmpl.jinja2"
        )
        gen_begin += template.render()
        return gen_begin

    def generate_source_message_encode_decode(
        self, messages: List[MessageSpec]
    ) -> str:
        gen = ""
        template = self.template_env.get_template(
            "MessageSpec_Source_Structs_tmple.jinja2"
        )
        for message in messages:
            gen += template.render(message=message)
        return gen

    def generate_source_message_using(self, using_map: Dict) -> str:
        gen_usings = ""
        template = self.template_env.get_template(
            "MessageSpec_Source_Using_templ.jinja2"
        )
        gen_usings += template.render(using_map=using_map)
        return gen_usings

    def generate_source_message_end(self, using_map: Dict) -> str:
        gen_end = ""
        template = self.template_env.get_template(
            "MessageSpec_Source_End_tmpl.jinja2"
        )
        gen_end += template.render(using_map=using_map)
        return gen_end

    def generate_header(
        self, messages: List[MessageSpec], using_map: Dict
    ) -> str:
        # Header
        header = ""
        header += self.generate_header_message_begin()
        header += self.generate_header_message_using(using_map)
        header += self.generate_header_message_enums(messages)
        header += self.generate_header_message_structs(messages)
        header += self.generate_header_message_end(using_map)
        return header

    def generate_source(
        self, messages: List[MessageSpec], using_map: Dict
    ) -> str:
        # Generate source
        source = ""
        source += self.generate_source_message_begin(messages)
        # source += self.generate_source_message_using(using_map)
        source += self.generate_source_message_encode_decode(messages)
        source += self.generate_source_message_end(using_map)
        return source
