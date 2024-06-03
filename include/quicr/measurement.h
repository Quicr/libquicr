#pragma once

#include <nlohmann/json.hpp>
using json = nlohmann::json;

#include <any>
#include <chrono>
#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <variant>

namespace quicr {

using MetricValueType = std::variant<std::uint8_t,
                                     std::uint16_t,
                                     std::uint32_t,
                                     std::uint64_t,
                                     float,
                                     double,
                                     std::string>;

struct Metric
{
  std::string name;
  std::string type;
  MetricValueType value;
};
void to_json(json& j, const Metric& m);

struct Attribute
{
  std::string name;
  std::string value;
  std::optional<std::string> type = std::nullopt;
};
void to_json(json& j, const Attribute& a);

class Measurement
{
public:
  Measurement() = default;
  Measurement(const std::string& name);

  Measurement& SetTime(const std::chrono::system_clock::time_point& time) noexcept;

  Measurement& AddAttribute(const Attribute& attr) noexcept;
  Measurement& AddAttribute(const std::string& name, const std::string& value, std::optional<std::string> type = std::nullopt) noexcept;
  Measurement& SetAttribute(const std::string& name, const std::string& value);
  Attribute& GetAttribute(const std::string& name) { return _attributes.at(name); }

  Measurement& AddMetric(const std::string& name, const std::uint8_t& value);
  Measurement& AddMetric(const std::string& name, const std::uint16_t& value);
  Measurement& AddMetric(const std::string& name, const std::uint32_t& value);
  Measurement& AddMetric(const std::string& name, const std::uint64_t& value);
  Measurement& AddMetric(const std::string& name, const float& value);
  Measurement& AddMetric(const std::string& name, const double& value);
  Measurement& AddMetric(const std::string& name, const std::string& value);

  template<typename T>
  T& GetMetricValue(const std::string& name);

  template<typename T>
  Measurement& SetMetric(const std::string& name, const T& value);

  friend void to_json(json& j, const Measurement& m);

private:
  std::string _name;
  std::chrono::system_clock::time_point _timestamp;
  std::unordered_map<std::string, Attribute> _attributes;
  std::unordered_map<std::string, Metric> _metrics;
};
}
