/*
 *  quic_identifier.h
 *
 *  Copyright (C) 2023
 *  Cisco Systems, Inc.
 *  All Rights Reserved.
 *
 *  Description:
 *      This file defines various identifier types used by QUIC.  These
 *      identifiers will initialize by default with a buffer holding the
 *      maximum number of octets held by the specific type.  For example,
 *      a QUICConnectionID will be created with an empty 20 octet buffer.
 *      If a different size is desired, the length may be passed as the
 *      parameter in the constructor.  Other constructors will create
 *      identifiers of a fixed size or size given the length of the data
 *      provided.
 *
 *      Since identifiers have a maximum octet length, attempts to append
 *      data beyond the allowable length will fail silently.  However, the
 *      the operators [] access the underlying std::vector directly and
 *      could result in an exception being thrown if not used properly.
 *
 *  Portability Issues:
 *      None.
 */

#pragma once

#include <cstdint>
#include <iomanip>
#include <ostream>
#include <sstream>
#include <string>
#include <vector>

namespace quicr {

// Template class to hold a QUIC identifier type
template<std::size_t L>
class QUICIdentifier
{
public:
  // Buffer for identifier is silently constrained to this length
  static constexpr std::size_t Max_Length{ L };

  // Create a QUICIdentifier with a full data buffer allocated
  QUICIdentifier()
    : data(Max_Length)
  {
  }

  // Create a QUICIdentifier with a data buffer of the given size
  // (constrained by the Max_Length)
  QUICIdentifier(std::size_t length)
    : data(std::min(length, Max_Length))
  {
  }

  // Create QUICIdentifier that copies value to this object
  QUICIdentifier(const QUICIdentifier& value) { data = value.data; }

  // Create QUICIdentifier, moving the value to this object
  QUICIdentifier(QUICIdentifier&& value) noexcept
    : data(std::move(value.data))
  {
  }

  // Create a QUICIdentifier initialized with the given buffer
  QUICIdentifier(const std::uint8_t* buffer, std::size_t length)
  {
    Assign(buffer, length);
  }

  // Create a QUICIdentifier initialized from the given string
  QUICIdentifier(const std::string& value)
  {
    Assign(reinterpret_cast<const std::uint8_t*>(value.data()), value.length());
  }

  // Create a QUICIdentifier initialized from the given vector
  QUICIdentifier(const std::vector<std::uint8_t>& value)
  {
    Assign(value.data(), value.size());
  }

  // Default destructor
  ~QUICIdentifier() = default;

  // Returns a const pointer to the underlying buffer
  const std::vector<std::uint8_t>& GetData() const { return data; }

  // Returns a const pointer to the underlying buffer
  const std::uint8_t* GetDataBuffer() const { return data.data(); }

  // Returns a mutable pointer to the underlying buffer
  std::uint8_t* GetDataBuffer() { return data.data(); }

  // Returns the number of octets in the internal data buffer
  std::size_t GetDataLength() const { return data.size(); }

  // Set the length of the buffer contents; useful if populated via
  // the raw buffer pointer directly (max length is Max_Length)
  void SetDataLength(std::size_t length)
  {
    data.resize(std::min(length, Max_Length));
  }

  // Resets the object to the default data length
  void Reset() { data.resize(Max_Length); }

  // Clear the contents of the object (resulting in zero length)
  void Clear() { data.clear(); }

  // Assigns the given buffer to the object, erasing anything present
  inline void Assign(const QUICIdentifier& value) { Assign(value.data); }
  inline void Assign(const std::vector<std::uint8_t>& value)
  {
    Assign(value.data(), value.size());
  }
  void Assign(const std::uint8_t* value, std::size_t length)
  {
    data.clear();
    if (length == 0) return;
    data.assign(value, value + std::min(length, Max_Length));
  }

  // Append the given buffer to the object
  inline void Append(const QUICIdentifier& value) { Append(value.data); }
  inline void Append(const std::uint8_t value) { Append(&value, 1); }
  inline void Append(const std::vector<std::uint8_t>& value)
  {
    Append(value.data(), value.size());
  }
  void Append(const std::uint8_t* value, std::size_t length)
  {
    // Reduce length so as to not exceed Max_Length octets
    length = std::min(length, Max_Length - data.size());
    if (length == 0) return;
    data.insert(data.end(), value, value + length);
  }

  // Assigns the other object to this one
  QUICIdentifier& operator=(const QUICIdentifier& other)
  {
    data = other.data;
    return *this;
  }

  // Various comparison functions
  bool operator==(const QUICIdentifier& other) const
  {
    return data == other.data;
  }
  bool operator!=(const QUICIdentifier& other) const
  {
    return !(*this == other);
  }
  bool operator<(const QUICIdentifier& other) const
  {
    return data < other.data;
  }
  bool operator<=(const QUICIdentifier& other) const
  {
    return data <= other.data;
  }
  bool operator>(const QUICIdentifier& other) const
  {
    return data > other.data;
  }
  bool operator>=(const QUICIdentifier& other) const
  {
    return data >= other.data;
  }

  // Access operators
  std::uint8_t& operator[](std::size_t index) { return data[index]; }
  const std::uint8_t& operator[](std::size_t index) const
  {
    return data[index];
  }

  // Returns the object as a string suitable for logging
  std::string ToString() const
  {
    std::ostringstream oss;
    oss << "[" << std::hex << std::setfill('0');
    if (data.size() > 0) oss << "0x";
    for (auto v : data) oss << std::setw(2) << unsigned(v);
    oss << "]";

    return oss.str();
  }

  // Returns a hex string containing up to the specified number of octets
  // at the tail of the identifier (intended for making logs a bit
  // easier to follow)
  std::string SuffixString(std::size_t count = 4) const
  {
    std::ostringstream oss;

    // Get the data size, as we'll need that several times
    std::size_t data_size = data.size();

    // Adjust count to prevent stepping outside the bounds
    count = std::min(count, data_size);

    // Exit early if the string would be empty
    if (count == 0) return oss.str();

    oss << std::hex << std::setfill('0');
    for (std::size_t i = data_size - count; i < data_size; i++) {
      oss << std::setw(2) << unsigned(data[i]);
    }

    return oss.str();
  }

protected:
  std::vector<std::uint8_t> data;
};

// Function to allow streaming the QUICIdentifier to an output stream
template<std::size_t L>
std::ostream&
operator<<(std::ostream& o, const QUICIdentifier<L>& id)
{
  o << id.ToString();

  return o;
}

// Define the QUIC Connection ID type
typedef class QUICIdentifier<20> QUICConnectionID;

// Define the QUIC token type
typedef class QUICIdentifier<256> QUICToken;

}
