#include <doctest/doctest.h>

#include <transport/stream_buffer.h>

#include <random>
#include <transport/transport.h>
#include <vector>
#include <thread>

TEST_CASE("StreamBuffer Load")
{
  qtransport::stream_buffer<uint8_t> buf;

  std::cout << "size: " << buf.size() << std::endl;
  buf.push(10);
  std::cout << "size: " << buf.size() << std::endl;

  for (int i=0; i < 10; i++) {
    buf.push(i);
  }

  buf.pop();
  std::cout << "size: " << buf.size() << " asize: " << buf.actual_size() << std::endl;

  buf.pop(5);
  buf.push(100);
  std::cout << "size: " << buf.size() << " asize: " << buf.actual_size() << std::endl;

  buf.pop(100);
  std::cout << "size: " << buf.size() << " asize: " << buf.actual_size() << std::endl;

  buf.push(1);
  std::cout << "size: " << buf.size() << " asize: " << buf.actual_size() << std::endl;

}

