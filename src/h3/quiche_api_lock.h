/*
 *  quiche_api_lock.h
 *
 *  Copyright (C) 2023
 *  Cisco Systems, Inc.
 *  All Rights Reserved.
 *
 *  Description:
 *      This file defines a class that can be used to control a mutex protecting
 *      the Quiche C API since it is not thread-safe.  It implements the
 *      Lockable interface and is intended to be used with std::lock_guard
 *      or std::unique_lock.
 *
 *  Portability Issues:
 *      None.
 */

#pragma once

#include <mutex>

namespace quicr {

// Class that provides a simple wrapper around the Quiche C API
class QuicheAPILock
{
public:
  QuicheAPILock() = default;
  ~QuicheAPILock() = default;
  void lock();
  void unlock();
  bool try_lock();
};

// Function to wrap Quiche API calls to ensure the Quiche API mutex is locked
// since the Quiche library is not thread-safe
template<typename T, typename... Args>
auto
QuicheCall(T func, Args... args)
{
  QuicheAPILock quiche_lock;

  std::lock_guard<QuicheAPILock> lock(quiche_lock);

  return func(std::forward<Args>(args)...);
}

}
