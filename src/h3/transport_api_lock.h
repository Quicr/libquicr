/*
 *  transport_api_lock.h
 *
 *  Copyright (C) 2023
 *  Cisco Systems, Inc.
 *  All Rights Reserved.
 *
 *  Description:
 *      This file defines a class that can be used to control a mutex protecting
 *      the QUICR Transport API since it is not thread-safe.  It implements the
 *      Lockable interface and is intended to be used with std::lock_guard
 *      or std::unique_lock.
 *
 *  Portability Issues:
 *      None.
 */

#pragma once

#include <mutex>

namespace quicr::h3 {

// Class that implements a Lockable interface
class TransportAPILock
{
public:
  TransportAPILock() = default;
  ~TransportAPILock() = default;
  void lock();
  void unlock();
  bool try_lock();
};

// Function to wrap Transport API calls that will lock a mutex while invoking;
// expected to be called with a lambda or std::function parameter
template<typename T>
auto
TransportCall(T func)
{
  TransportAPILock transport_lock;

  std::lock_guard<TransportAPILock> lock(transport_lock);

  return func();
}

} // namespace quicr::h3
