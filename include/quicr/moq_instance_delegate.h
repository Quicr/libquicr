/*
 *  Copyright (C) 2024
 *  Cisco Systems, Inc.
 *  All Rights Reserved
 */

#pragma once

namespace quicr {

/**
 * @brief Delegate for all MOQ/MOQT callbacks and interaction
 *
 * @details A new MOQ instance is created with this delegate as a shared pointer.
 *   All interaction with the instance is handled via this delegate. This delegate
 *   is a base class implementation and interface for callbacks.
 */

class MoQInstanceDelegate
{

};

} // namespace quicr
