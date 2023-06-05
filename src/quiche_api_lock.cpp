/*
 *  quiche_api_lock.cpp
 *
 *  Copyright (C) 2023
 *  Cisco Systems, Inc.
 *  All Rights Reserved.
 *
 *  Description:
 *      This file implements the class used to control a mutex protecting
 *      the Quiche C API since it is not thread-safe.
 *
 *  Portability Issues:
 *      None.
 */

#include <mutex>
#include "quiche_api_lock.h"

namespace quicr {

// Mutex used to protect access to the Quiche C API
static std::mutex quiche_lock;

/*
 *  QuicheAPILock::Lock()
 *
 *  Description:
 *      Locks the mutex protecting the Quiche C API.
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
QuicheAPILock::lock()
{
  quiche_lock.lock();
}

/*
 *  QuicheAPILock::unlock()
 *
 *  Description:
 *      Unlocks the mutex protecting the Quiche C API.
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
QuicheAPILock::unlock()
{
  quiche_lock.unlock();
}

/*
 *  QuicheAPILock::try_lock()
 *
 *  Description:
 *      Unlocks the mutex protecting the Quiche C API.
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
QuicheAPILock::try_lock()
{
  return quiche_lock.try_lock();
}

}
