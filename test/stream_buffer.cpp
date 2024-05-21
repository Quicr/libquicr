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
  size_t rcount {0}, wcount {0};

  std::thread reader([&buf, &rcount, &stop]() {
    size_t prev_val {0};
    while (!stop) {
      if (const auto val = buf->front()) {
        if (prev_val && *val != prev_val + 1) {
          std::cout << "val: " << *val << " rcount: " << rcount << std::endl;
        }

        prev_val = *val;

        ++rcount;
        buf->pop();
      } else {
        std::this_thread::sleep_for(std::chrono::microseconds(50));
      }
    }
  });

  std::thread writer([&buf, &wcount, &stop]() {
    while (!stop) {
      buf->push(wcount++);
      std::this_thread::sleep_for(std::chrono::microseconds(25));
    }
  });

  std::this_thread::sleep_for(std::chrono::seconds(1));
  stop = true;

  writer.join();
  reader.join();

  std::cout << "reader/writer count: " << rcount << " / " << wcount
            << " buf size: " << buf->size()
            << " actual sz: " << buf->actual_size()
            << std::endl;


  std::cout << "Buf size before purge: " << buf->size() << std::endl;
  buf->purge();
  std::cout << "Buf size after purge: " << buf->size() << std::endl;

  CHECK_EQ(buf->size(), 0);
}

