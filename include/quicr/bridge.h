#pragma once

#include <variant>
#include <quicr_name>

namespace quicr {

    using NamespeceRef = std::variant<quicr::Namespace, std::string>;
    using NameRef = std::variant<quicr::Name, std::string>;

}