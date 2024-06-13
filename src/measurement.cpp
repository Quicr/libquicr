#include "quicr/measurement.h"

namespace quicr {

Measurement::Measurement(const std::string& name)
  : _name{ name }
{
  AddAttribute("an", name, "imn");
}

Measurement&
Measurement::SetTime(const std::chrono::system_clock::time_point& time) noexcept
{
  _timestamp = time;
  return *this;
}

Measurement&
Measurement::AddAttribute(const Attribute& attr) noexcept
{
  _attributes.emplace(attr.value, attr);
  return *this;
}

Measurement&
Measurement::AddAttribute(const std::string& name,
                          const std::string& value,
                          std::optional<std::string> type) noexcept
{
  _attributes.emplace(name, Attribute{ name, value, type });
  return *this;
}

Measurement&
Measurement::SetAttribute(const std::string& name, const std::string& value)
{
  _attributes.at(name).value = value;
  return *this;
}

template<typename T>
T&
Measurement::GetMetricValue(const std::string& name)
{
  return std::get<T>(_metrics.at(name).value);
}

template std::uint8_t&
Measurement::GetMetricValue<std::uint8_t>(const std::string&);
template std::uint16_t&
Measurement::GetMetricValue<std::uint16_t>(const std::string&);
template std::uint32_t&
Measurement::GetMetricValue<std::uint32_t>(const std::string&);
template std::uint64_t&
Measurement::GetMetricValue<std::uint64_t>(const std::string&);
template float&
Measurement::GetMetricValue<float>(const std::string&);
template double&
Measurement::GetMetricValue<double>(const std::string&);
template std::string&
Measurement::GetMetricValue<std::string>(const std::string&);

Measurement&
Measurement::AddMetric(const std::string& name,
                       const std::uint8_t& value)
{
  _metrics.emplace(name, Metric{ name, "uint8", value });
  return *this;
}

Measurement&
Measurement::AddMetric(const std::string& name,
                       const std::uint16_t& value)
{
  _metrics.emplace(name, Metric{ name, "uint16", value });
  return *this;
}

Measurement&
Measurement::AddMetric(const std::string& name,
                       const std::uint32_t& value)
{
  _metrics.emplace(name, Metric{ name, "uint32", value });
  return *this;
}

Measurement&
Measurement::AddMetric(const std::string& name,
                       const std::uint64_t& value)
{
  _metrics.emplace(name, Metric{ name, "uint64", value });
  return *this;
}

Measurement&
Measurement::AddMetric(const std::string& name,
                       const float& value)
{
  _metrics.emplace(name, Metric{ name, "float32", value });
  return *this;
}

Measurement&
Measurement::AddMetric(const std::string& name,
                       const double& value)
{
  _metrics.emplace(name, Metric{ name, "float64", value });
  return *this;
}

Measurement&
Measurement::AddMetric(const std::string& name,
                       const std::string& value)
{
  _metrics.emplace(name, Metric{ name, "string", value });
  return *this;
}
template<typename T>
Measurement&
Measurement::SetMetric(const std::string& name, const T& value)
{
  _metrics.at(name).value = value;
  return *this;
}

template Measurement&
Measurement::SetMetric<std::uint8_t>(const std::string&, const std::uint8_t&);
template Measurement&
Measurement::SetMetric<std::uint16_t>(const std::string&, const std::uint16_t&);
template Measurement&
Measurement::SetMetric<std::uint32_t>(const std::string&, const std::uint32_t&);
template Measurement&
Measurement::SetMetric<std::uint64_t>(const std::string&, const std::uint64_t&);
template Measurement&
Measurement::SetMetric<float>(const std::string&, const float&);
template Measurement&
Measurement::SetMetric<double>(const std::string&, const double&);
template Measurement&
Measurement::SetMetric<std::string>(const std::string&, const std::string&);

void
to_json(json& j, const Metric& m)
{
  j["mn"] = m.name;
  j["mt"] = m.type;
  std::visit([&](auto v) { j["mv"] = v; }, m.value);
}

void
from_json(const json& j, Metric& m)
{
  m.name = j["mn"];
  if (j.contains("mt"))
  {
    m.type = j["mt"];
  }

  auto& value_json = j["mv"];
  switch (value_json.type())
  {
    case json::value_t::number_unsigned:
      m.value = value_json.template get<std::uint64_t>();
      break;
    case json::value_t::number_float:
      m.value = value_json.template get<float>();
      break;
    case json::value_t::string: [[fallthrough]];
    default:
      m.value = value_json.template get<std::string>();
      break;
  }
}

void
to_json(json& j, const Attribute& a)
{
  j[a.name] = a.value;

  if (a.type.has_value()) {
    j["at"] = *a.type;
  }
}

void
to_json(json& j, const Measurement& m)
{
  j["ts"] = m._timestamp.time_since_epoch().count();
  j["tp"] = "u";

  std::vector<json> attrs_json;
  attrs_json.reserve(m._attributes.size());
  for (const auto& [_, attr] : m._attributes) {
    attrs_json.push_back(attr);
  }
  j["attrs"] = attrs_json;

  std::vector<json> metrics_json;
  metrics_json.reserve(m._metrics.size());
  for (const auto& [_, metric] : m._metrics) {
    metrics_json.push_back(metric);
  }
  j["metrics"] = metrics_json;
}

void
from_json(const json& j, Measurement& m)
{
  m._timestamp = std::chrono::time_point<std::chrono::system_clock>(std::chrono::system_clock::duration(j["ts"].template get<std::int64_t>()));

  for (const auto& [name, attr_json] : j["attrs"].items()) {
    Attribute attr;
    attr.name = name;
    attr.value = name;
    if (attr_json.at("at"))
    {
      attr.type = attr_json.at("at");
    }
    m._attributes.emplace(name, std::move(attr));
  }

  for (const auto& [name, metric] : j["metrics"].items()) {
    m._metrics.emplace(name, metric);
  }
}
}
