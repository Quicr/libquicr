#include <doctest/doctest.h>

#include <transport/stream_buffer.h>

#include <transport/transport.h>
#include <chrono>
#include <thread>

TEST_CASE("StreamBuffer Reader/Writer")
{
  using streamBuf_t = qtransport::stream_buffer<uint32_t>;
  std::shared_ptr<streamBuf_t> buf = std::make_shared<streamBuf_t>();
  bool stop{ false };
  size_t rcount {0}, wcount {0};

  std::thread reader([&buf, &rcount, &stop]() {
    while (!stop) {
      if (const auto val = buf->front()) {
        FAST_CHECK_EQ(*val, rcount);

        ++rcount;
        buf->pop();
      } else {
        std::this_thread::sleep_for(std::chrono::microseconds(60));
      }
    }

    rcount += buf->size();
    buf->pop(buf->size());
  });

  std::thread writer([&buf, &wcount, &stop]() {
    while (!stop) {
      buf->push(wcount++);
      std::this_thread::sleep_for(std::chrono::microseconds(50));
    }
  });

  std::this_thread::sleep_for(std::chrono::seconds(1));
  stop = true;

  writer.join();
  reader.join();

  CHECK_EQ(buf->size(), 0);
  CHECK_EQ(rcount, wcount);
}

