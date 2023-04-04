#pragma once

#include <quicr/quicr_name.h>

#include <algorithm>
#include <array>
#include <bitset>
#include <cassert>
#include <cstdint>
#include <iomanip>
#include <span>
#include <vector>

namespace quicr {
/**
 * @brief Encodes/Decodes a hex string from/into a list of unsigned integers
 *        values.
 *
 * The hex string created by/passed to this class is of the format:
 *     0xXX...XYY...YZZ...Z....
 *       |____||____||____|
 *        Dist0 Dist1 Dist2   ...
 *       |_____________________|
 *                Size
 * Where Dist is the distribution of bits for each value provided. For Example:
 *   HexEndec<64, 32, 24, 8>
 * Describes a 64 bit hex value, distributed into 3 sections 32bit, 24bit, and
 * 8bit respectively.
 *
 * @tparam Size The maximum size in bits of the Hex string
 * @tparam ...Dist The distribution of bits for each value passed.
 */
template<uint16_t Size, uint8_t... Dist>
class HexEndec
{
  template<typename... UInt_ts>
  struct is_valid_uint
    : std::bool_constant<(std::is_unsigned_v<UInt_ts> && ...)>
  {
  };

public:
  HexEndec()
  {
    static_assert((Size & (Size - 1)) == 0, "Size must be a power of 2");
    static_assert(Size == (Dist + ...), "Total bits must be equal to Size");
  }

  /**
   * @brief Encodes the last Dist bits of values in order according to
   * distribution of Dist and builds a hex string that is the size in bits of
   * Size.
   *
   * @tparam ...UInt_ts The unsigned integer types to be passed.
   * @param ...values The unsigned values to be encoded into the hex string.
   *
   * @returns Hex string containing the provided values distributed according to
   *          Dist in order.
   */
  template<typename... UInt_ts>
  static inline std::string Encode(UInt_ts... values)
  {
    static_assert((Size & (Size - 1)) == 0, "Size must be a power of 2");
    static_assert(Size == (Dist + ...),
                  "Total bits cannot exceed specified size");
    static_assert(sizeof...(Dist) == sizeof...(UInt_ts),
                  "Number of values should match distribution of bits");
    static_assert(is_valid_uint<UInt_ts...>::value,
                  "Arguments must all be unsigned integers");

    std::array<uint8_t, sizeof...(UInt_ts)> distribution{ Dist... };
    return Encode(std::span<uint8_t>(distribution),
                  std::forward<UInt_ts>(values)...);
  }

  template<typename... UInt_ts>
  static inline std::string Encode(std::span<uint8_t> distribution,
                                   UInt_ts... values)
  {
    static_assert((Size & (Size - 1)) == 0, "Size must be a power of 2");
    static_assert(is_valid_uint<UInt_ts...>::value,
                  "Arguments must all be unsigned integers");
    assert(distribution.size() == sizeof...(UInt_ts) &&
           "Number of values should match distribution of bits");

    std::array<uint64_t, sizeof...(UInt_ts)> vals{ values... };
    return Encode(distribution, std::span<uint64_t>(vals));
  }

  template<size_t N, typename... UInt_ts>
  static inline std::string Encode(std::array<uint8_t, N> distribution,
                                   UInt_ts... values)
  {
    return Encode(std::span<uint8_t>(distribution),
                  std::forward<UInt_ts>(values)...);
  }

  template<bool B = Size <= sizeof(uint64_t) * 8>
  static inline typename std::enable_if<B, std::string>::type Encode(
    std::span<uint8_t> distribution,
    std::span<uint64_t> values)
  {
    static_assert((Size & (Size - 1)) == 0, "Size must be a power of 2");
    assert(distribution.size() == values.size() &&
           "Number of values should match distribution of bits");

    uint64_t bits = 0;
    for (size_t i = 0; i < values.size(); ++i) {
      const uint64_t& value = values[i];
      uint8_t dist = distribution[i];
      bits <<= dist;
      bits |= (value & ~(~0x0ull << dist));
    };

    char hex[Size / 4 + 1];
    std::snprintf(hex, Size / 4 + 1, "%0*llX", Size / 4, bits);

    return std::string("0x") + hex;
  }

  template<bool B = Size <= sizeof(uint64_t) * 8>
  static inline typename std::enable_if<B, std::string>::type Encode(
    std::vector<uint8_t> distribution,
    std::vector<uint64_t> values)
  {
    return Encode(std::span<uint8_t>(distribution),
                  std::span<uint64_t>(values));
  }

  template<size_t N, bool B = Size <= sizeof(uint64_t) * 8>
  static inline typename std::enable_if<B, std::string>::type Encode(
    std::array<uint8_t, N> distribution,
    std::array<uint64_t, N> values)
  {
    return Encode(std::span<uint8_t>(distribution),
                  std::span<uint64_t>(values));
  }

  template<bool B = Size <= sizeof(uint64_t) * 8>
  static inline typename std::enable_if<!B, std::string>::type Encode(
    std::span<uint8_t> distribution,
    std::span<uint64_t> values)
  {
    static_assert((Size & (Size - 1)) == 0, "Size must be a power of 2");
    assert(distribution.size() == values.size() &&
           "Number of values should match distribution of bits");

    std::bitset<Size> bits;
    for (size_t i = 0; i < values.size(); ++i) {
      const uint64_t& value = values[i];
      uint8_t dist = distribution[i];
      bits <<= dist;
      bits |= std::bitset<Size>(
        dist >= sizeof(uint64_t) * 8 ? value : (value & ~(~0x0ull << dist)));
    };

    static const std::bitset<Size> bits_mask =
      (std::bitset<Size>().set() >> 64).flip();
    constexpr size_t sizeof_uint64_bits = sizeof(uint64_t) * 8;

    std::string out_hex = "0x";
    for (size_t i = 0; i < Size / sizeof_uint64_bits; ++i) {
      char hex[sizeof(uint64_t) * 2 + 1];
      std::snprintf(
        hex,
        sizeof(uint64_t) * 2 + 1,
        "%0*llX",
        int(sizeof(uint64_t) * 2),
        ((bits & bits_mask) >> (Size - sizeof_uint64_bits)).to_ullong());
      out_hex += hex;
      bits <<= sizeof_uint64_bits;
    }

    return out_hex;
  }

  template<bool B = Size <= sizeof(uint64_t) * 8>
  static inline typename std::enable_if<!B, std::string>::type Encode(
    std::vector<uint8_t> distribution,
    std::vector<uint64_t> values)
  {
    return Encode(std::span<uint8_t>(distribution),
                  std::span<uint64_t>(values));
  }

  template<size_t N, bool B = Size <= sizeof(uint64_t) * 8>
  static inline typename std::enable_if<!B, std::string>::type Encode(
    std::array<uint8_t, N> distribution,
    std::array<uint64_t, N> values)
  {
    return Encode(std::span<uint8_t>(distribution),
                  std::span<uint64_t>(values));
  }

  /**
   * @brief Decodes a hex string that has size in bits of Size into a list of
   *        values sized according to Dist in order.
   *
   * @tparam Uint_t The unsigned integer type to return.
   * @param hex The hex string to decode. Must have a length in bytes
   *            corresponding to the size in bits of Size.
   *
   * @returns Structured binding of values decoded from hex string corresponding
   *          in order to the size of Dist.
   */
  template<typename Uint_t = uint64_t>
  static inline std::array<Uint_t, sizeof...(Dist)> Decode(
    const std::string& hex)
  {
    static_assert((Size & (Size - 1)) == 0, "Size must be a power of 2");
    static_assert(Size >= (Dist + ...),
                  "Total bits cannot exceed specified size");
    static_assert(is_valid_uint<Uint_t>::value,
                  "Type must be unsigned integer");

    std::array<uint8_t, sizeof...(Dist)> distribution{ Dist... };
    auto result = Decode(distribution, hex);
    std::array<Uint_t, sizeof...(Dist)> out;
    std::copy_n(
      std::make_move_iterator(result.begin()), sizeof...(Dist), out.begin());

    return out;
  }

  template<typename Uint_t = uint64_t>
  static inline std::array<Uint_t, sizeof...(Dist)> Decode(
    const quicr::Name& name)
  {
    static_assert((Size & (Size - 1)) == 0, "Size must be a power of 2");
    static_assert(Size >= (Dist + ...),
                  "Total bits cannot exceed specified size");
    static_assert(is_valid_uint<Uint_t>::value,
                  "Type must be unsigned integer");

    std::array<uint8_t, sizeof...(Dist)> distribution{ Dist... };
    auto result = Decode(distribution, name.to_hex());
    std::array<Uint_t, sizeof...(Dist)> out;
    std::copy_n(
      std::make_move_iterator(result.begin()), sizeof...(Dist), out.begin());

    return out;
  }

  template<typename Uint_t = uint64_t, bool B = Size <= sizeof(uint64_t) * 8>
  static inline typename std::enable_if<B, std::vector<Uint_t>>::type Decode(
    std::span<uint8_t> distribution,
    const std::string& hex)
  {
    static_assert((Size & (Size - 1)) == 0, "Size must be a power of 2");
    static_assert(is_valid_uint<Uint_t>::value,
                  "Type must be unsigned integer");

    const auto dist_size = distribution.size();
    std::vector<uint64_t> result(dist_size);
    uint64_t bits = std::stoull(hex, nullptr, 16);
    for (int i = dist_size - 1; i >= 0; --i) {
      const auto dist = distribution[i];
      result[i] = bits & ~(~0x0ull << dist);
      bits >>= dist;
    }

    return result;
  }

  template<typename Uint_t = uint64_t, bool B = Size <= sizeof(uint64_t) * 8>
  static inline typename std::enable_if<!B, std::vector<Uint_t>>::type Decode(
    std::span<uint8_t> distribution,
    const std::string& hex)
  {
    static_assert((Size & (Size - 1)) == 0, "Size must be a power of 2");
    static_assert(is_valid_uint<Uint_t>::value,
                  "Type must be unsigned integer");

    uint8_t start_pos = 0;
    if (hex.substr(0, 2) == "0x")
      start_pos = 2;

    constexpr uint8_t hex_length = Size / 4;
    if (hex.length() - start_pos != hex_length)
      throw std::runtime_error(
        "Hex string value must be " + std::to_string(hex_length) +
        " characters (" + std::to_string(Size / 8) +
        " bytes). Got: " + std::to_string(hex.length() - start_pos));

    std::bitset<Size> bits;
    const uint8_t section_length = sizeof(uint64_t) * 2;
    constexpr size_t sizeof_uint64_bits = sizeof(uint64_t) * 8;
    constexpr size_t num_sections = Size / sizeof_uint64_bits;
    for (size_t i = start_pos, j = 0; i < (section_length * num_sections);
         i += section_length) {
      std::string section = hex.substr(i, section_length);
      bits |= std::bitset<Size>(std::stoull(section, nullptr, 16))
              << (sizeof_uint64_bits * (num_sections - ++j));
    }

    const auto dist_size = distribution.size();
    std::vector<uint64_t> result(dist_size);
    for (size_t i = 0; i < dist_size; ++i) {
      const auto dist = distribution[i];
      result[i] = (bits >> (Size - dist)).to_ullong();
      bits <<= dist;
    }

    return result;
  }

  template<typename Uint_t = uint64_t>
  static inline std::vector<Uint_t> Decode(std::span<uint8_t> distribution,
                                           const quicr::Name& name)
  {
    return Decode(distribution, name.to_hex());
  }
};
}
