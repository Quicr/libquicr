#include <quicr/message_buffer.h>

#include <iomanip>
#include <sstream>

namespace quicr {
uintVar_t
to_varint(uint64_t v)
{
  assert(v < ((uint64_t)0x1 << 61));
  return static_cast<uintVar_t>(v);
}

uint64_t
from_varint(uintVar_t v)
{
  return static_cast<uint64_t>(v);
}

namespace messages {
MessageBuffer::MessageBuffer(MessageBuffer&& other)
  : _buffer{std::move(other._buffer)}
{
}

MessageBuffer::MessageBuffer(const std::vector<uint8_t>& buffer)
  : _buffer{buffer}
{
}

MessageBuffer::MessageBuffer(std::vector<uint8_t>&& buffer)
  : _buffer{std::move(buffer)}
{
}
  
void MessageBuffer::push_back(const std::vector<uint8_t>& data)
{
  _buffer.insert(_buffer.end(), data.begin(), data.end());
}

void MessageBuffer::pop_back(uint16_t len)
{
  if (len > _buffer.size())
    throw std::out_of_range("len cannot be longer than the size of the buffer");

  const auto delta = _buffer.size() - len;
  _buffer.erase(_buffer.begin() + delta, _buffer.end());
};

std::vector<uint8_t> MessageBuffer::back(uint16_t len)
{
  if (len > _buffer.size())
    throw std::out_of_range("len cannot be longer than the size of the buffer");

  std::vector<uint8_t> vec(len);
  const auto delta = _buffer.size() - len;
  std::copy(_buffer.begin() + delta, _buffer.end(), vec.begin());

  return vec;
}

std::string
MessageBuffer::to_hex() const
{
  std::ostringstream hex;
  hex << std::hex << std::setfill('0');
  for (const auto& byte : _buffer) {
    hex << std::setw(2) << int(byte);
  }
  return hex.str();
}

void MessageBuffer::operator=(MessageBuffer&& other)
{
  _buffer = std::move(other._buffer);
}

MessageBuffer&
operator<<(MessageBuffer& msg, const uint8_t val)
{
  msg.push_back(val);
  return msg;
}

bool
operator>>(MessageBuffer& msg, uint8_t& val)
{
  val = msg.back();
  msg.pop_back();
  return true;
}

MessageBuffer&
operator<<(MessageBuffer& msg, const uint64_t& val)
{
  // TODO - std::copy version for little endian machines optimization

  // buffer on wire is little endian (that is *not* network byte order)
  msg.push_back(uint8_t((val >> 0) & 0xFF));
  msg.push_back(uint8_t((val >> 8) & 0xFF));
  msg.push_back(uint8_t((val >> 16) & 0xFF));
  msg.push_back(uint8_t((val >> 24) & 0xFF));
  msg.push_back(uint8_t((val >> 32) & 0xFF));
  msg.push_back(uint8_t((val >> 40) & 0xFF));
  msg.push_back(uint8_t((val >> 48) & 0xFF));
  msg.push_back(uint8_t((val >> 56) & 0xFF));

  return msg;
}

bool
operator>>(MessageBuffer& msg, uint64_t& val)
{
  uint8_t byte[8] = { 0, 0, 0, 0, 0, 0, 0, 0 };

  bool ok = true;
  ok &= msg >> byte[7];
  ok &= msg >> byte[6];
  ok &= msg >> byte[5];
  ok &= msg >> byte[4];
  ok &= msg >> byte[3];
  ok &= msg >> byte[2];
  ok &= msg >> byte[1];
  ok &= msg >> byte[0];

  val = (uint64_t(byte[0]) << 0) + (uint64_t(byte[1]) << 8) +
        (uint64_t(byte[2]) << 16) + (uint64_t(byte[3]) << 24) +
        (uint64_t(byte[4]) << 32) + (uint64_t(byte[5]) << 40) +
        (uint64_t(byte[6]) << 48) + (uint64_t(byte[7]) << 56);

  return ok;
}

MessageBuffer&
operator<<(MessageBuffer& msg, const std::vector<uint8_t>& val)
{
  msg.push_back(val);
  msg << to_varint(val.size());
  return msg;
}

bool
operator>>(MessageBuffer& msg, std::vector<uint8_t>& val)
{
  uintVar_t vecSize{0};
  msg >> vecSize;

  size_t len = from_varint(vecSize);
  if (len == 0) {
    return false;
  }

  val.resize(len);
  val = msg.back(len);
  msg.pop_back(len);
  
  return true;
}

MessageBuffer&
operator<<(MessageBuffer& msg, const uintVar_t& v)
{
  uint64_t val = from_varint(v);

  assert(val < ((uint64_t)1 << 61));

  if (val <= ((uint64_t)1 << 7)) {
    msg.push_back(uint8_t(((val >> 0) & 0x7F)) | 0x00);
    return msg;
  }

  if (val <= ((uint64_t)1 << 14)) {
    msg.push_back(uint8_t((val >> 0) & 0xFF));
    msg.push_back(uint8_t(((val >> 8) & 0x3F) | 0x80));
    return msg;
  }

  if (val <= ((uint64_t)1 << 29)) {
    msg.push_back(uint8_t((val >> 0) & 0xFF));
    msg.push_back(uint8_t((val >> 8) & 0xFF));
    msg.push_back(uint8_t((val >> 16) & 0xFF));
    msg.push_back(uint8_t(((val >> 24) & 0x1F) | 0x80 | 0x40));
    return msg;
  }

  msg.push_back(uint8_t((val >> 0) & 0xFF));
  msg.push_back(uint8_t((val >> 8) & 0xFF));
  msg.push_back(uint8_t((val >> 16) & 0xFF));
  msg.push_back(uint8_t((val >> 24) & 0xFF));
  msg.push_back(uint8_t((val >> 32) & 0xFF));
  msg.push_back(uint8_t((val >> 40) & 0xFF));
  msg.push_back(uint8_t((val >> 48) & 0xFF));
  msg.push_back(uint8_t(((val >> 56) & 0x0F) | 0x80 | 0x40 | 0x20));

  return msg;
}

bool
operator>>(MessageBuffer& msg, uintVar_t& v)
{
  uint8_t byte[8] = { 0, 0, 0, 0, 0, 0, 0, 0 };
  bool ok = true;

  uint8_t first = msg.back();

  if ((first & (0x80)) == 0) {
    ok &= msg >> byte[0];
    uint8_t val = ((byte[0] & 0x7F) << 0);
    v = to_varint(val);
    return ok;
  }

  if ((first & (0x80 | 0x40)) == 0x80) {
    ok &= msg >> byte[1];
    ok &= msg >> byte[0];
    uint16_t val = (((uint16_t)byte[1] & 0x3F) << 8) + ((uint16_t)byte[0] << 0);
    v = to_varint(val);
    return ok;
  }

  if ((first & (0x80 | 0x40 | 0x20)) == (0x80 | 0x40)) {
    ok &= msg >> byte[3];
    ok &= msg >> byte[2];
    ok &= msg >> byte[1];
    ok &= msg >> byte[0];
    uint32_t val = ((uint32_t)(byte[3] & 0x1F) << 24) +
                   ((uint32_t)byte[2] << 16) + ((uint32_t)byte[1] << 8) +
                   ((uint32_t)byte[0] << 0);
    v = to_varint(val);
    return ok;
  }

  ok &= msg >> byte[7];
  ok &= msg >> byte[6];
  ok &= msg >> byte[5];
  ok &= msg >> byte[4];
  ok &= msg >> byte[3];
  ok &= msg >> byte[2];
  ok &= msg >> byte[1];
  ok &= msg >> byte[0];
  uint64_t val = ((uint64_t)(byte[7] & 0x0F) << 56) +
                 ((uint64_t)(byte[6]) << 48) +
                 ((uint64_t)(byte[5]) << 40) +
                 ((uint64_t)(byte[4]) << 32) +
                 ((uint64_t)(byte[3]) << 24) +
                 ((uint64_t)(byte[2]) << 16) +
                 ((uint64_t)(byte[1]) << 8) +
                 ((uint64_t)(byte[0]) << 0);
  v = to_varint(val);
  return ok;
}

} // namespace quicr::messages
} // namespace quicr
