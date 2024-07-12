#pragma once

#include <quicr/namespace.h>

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

/**
 * @brief Holds actual data of a measurement field.
 */
struct Metric
{
  std::string name;
  std::string type;
  MetricValueType value;
};
void to_json(json& j, const Metric& m);
void from_json(const json& j, Metric& m);

/**
 * @brief Equivalent to a tag
 */
struct Attribute
{
  // TODO(trigaux): Should possibly make this optional
  std::string name;
  std::string value;
  std::optional<std::string> type = std::nullopt;
};
void to_json(json& j, const Attribute& a);
void from_json(const json& j, Attribute& a);

/**
 * @brief Builder pattern measurement which holds several different metrics.
 */
class Measurement
{
public:
  Measurement() = default;
  Measurement(const std::string& name);

  Measurement& SetTime(const std::chrono::system_clock::time_point& time) noexcept;

  Measurement& AddAttribute(const Attribute& attr);
  Measurement& AddAttribute(const std::string& name, const std::string& value, std::optional<std::string> type = std::nullopt);

  Measurement& SetAttribute(const std::string& name, const std::string& value);

  Attribute& GetAttribute(const std::string& name) { return _attributes.at(name); }

  Measurement& AddMetric(const std::string& name, const MetricValueType& value);

  template<typename T>
  T& GetMetricValue(const std::string& name);

  template<typename T>
  const T& GetMetricValue(const std::string& name) const;

  template<typename T>
  Measurement& SetMetric(const std::string& name, const T& value);

  friend void to_json(json& j, const Measurement& m);
  friend void from_json(const json& j, Measurement& m);

private:
  std::string _name;
  std::chrono::system_clock::time_point _timestamp;
  std::unordered_map<std::string, Attribute> _attributes;
  std::unordered_map<std::string, Metric> _metrics;
};

/**
 * @brief Config for publishing measurements.
 */
struct MeasurementsConfig
{
  quicr::Namespace metrics_namespace;
  std::uint8_t priority;
  std::uint16_t ttl;
};
}
