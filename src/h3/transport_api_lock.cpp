/*
 *  transport_api_lock.cpp
 *
 *  Copyright (C) 2023
 *  Cisco Systems, Inc.
 *  All Rights Reserved.
 *
 *  Description:
 *      This file implements the class used to control a mutex protecting
 *      the QUICR Transport API since it is not thread-safe.
 *
 *  Portability Issues:
 *      None.
 */

#include <mutex>
#include "transport_api_lock.h"

namespace quicr::h3 {

// Mutex used to protect access to the QUICR Transport API
static std::mutex transport_lock;

/*
 *  TransportAPILock::Lock()
 *
 *  Description:
 *      Locks the mutex protecting the Transport API.
 *
 *  Parameters:
 *      None.
 *
 *  Returns:
 *      Nothing.
 *
 *  Comments:
 *      None.
 */
void
TransportAPILock::lock()
{
  transport_lock.lock();
}

/*
 *  TransportAPILock::unlock()
 *
 *  Description:
 *      Unlocks the mutex protecting the QUICR Transport API.
 *
 *  Parameters:
 *      None.
 *
 *  Returns:
 *      Nothing.
 *
 *  Comments:
 *      None.
 */
void
TransportAPILock::unlock()
{
  transport_lock.unlock();
}

/*
 *  TransportAPILock::try_lock()
 *
 *  Description:
 *      Unlocks the mutex protecting the QUICR Transport API.
 *
 *  Parameters:
 *      None.
 *
 *  Returns:
 *      Nothing.
 *
 *  Comments:
 *      None.
 */
bool
TransportAPILock::try_lock()
{
  return transport_lock.try_lock();
}

} // namespace quicr::h3
