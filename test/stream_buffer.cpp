#include <doctest/doctest.h>

#include <transport/stream_buffer.h>

#include <random>
#include <transport/transport.h>
#include <chrono>
#include <thread>

TEST_CASE("StreamBuffer Reader/Writer")
{
  using streamBuf_t = qtransport::stream_buffer<uint32_t>;
  std::shared_ptr<streamBuf_t> buf = std::make_shared<streamBuf_t>();
  bool stop{ false };

  std::thread reader([&buf, &stop]() {
    while (!stop) {
      if (const auto b = buf->front()) {
        buf->pop();
      } else {
      }
      std::this_thread::sleep_for(std::chrono::microseconds(100));
    }

    // Finish reading the last items after stop
    buf->pop(buf->size());
  });

  std::thread writer([&buf, &stop]() {
    int i = 0;
    while (!stop) {
      buf->push(i++);
      std::this_thread::sleep_for(std::chrono::microseconds(100));
    }
  });

  std::this_thread::sleep_for(std::chrono::seconds(1));
  stop = true;

  writer.join();
  reader.join();

  //buf->push(1);

  CHECK_EQ(buf->size(), 1);
}

