#pragma once

#include <condition_variable>
#include <deque>
#include <optional>

template<typename T>
struct AsyncQueue
{
  bool empty()
  {
    std::unique_lock<std::mutex> lock(mutex);
    return queue.empty();
  }

  void push(const T& val)
  {
    std::unique_lock<std::mutex> lock(mutex);
    queue.push_back(val);
    lock.unlock();
    nonempty.notify_all();
  }

  T pop()
  {
    std::unique_lock<std::mutex> lock(mutex);
    nonempty.wait(lock, [&] { return !queue.empty(); });
    const auto val = queue.front();
    queue.pop_front();
    return val;
  }

  std::optional<T> pop(std::chrono::milliseconds wait_time)
  {
    std::unique_lock<std::mutex> lock(mutex);
    const auto success =
      nonempty.wait_for(lock, wait_time, [&] { return !queue.empty(); });
    if (!success) {
      return std::nullopt;
    }

    const auto val = queue.front();
    queue.pop_front();
    return val;
  }

  std::deque<T> take()
  {
    std::unique_lock<std::mutex> lock(mutex);
    auto out = std::move(queue);
    queue = {};
    return out;
  }

private:
  std::mutex mutex;
  std::condition_variable nonempty;
  std::deque<T> queue;
};