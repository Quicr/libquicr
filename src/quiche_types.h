/*
 *  quiche_types.h
 *
 *  Copyright (C) 2023
 *  Cisco Systems, Inc.
 *  All Rights Reserved.
 *
 *  Description:
 *      This file defines types used in quiche for which no C interface
 *      exists to define them.
 *
 *  Portability Issues:
 *      None.
 */

#pragma once

#include <string>
#include <cstdint>

namespace quicr {

enum QuicheQUICMsgTypes
{
    QuicheQUICMsgInvalid = 0,
    QuicheQUICMsgInitial = 1,
    QuicheQUICMsgRetry = 2,
    QuicheQUICMsgHandshake = 3,
    QuicheQUICMsgZeroRTT = 4,
    QuicheQUICMsgShort = 5,
    QuicheQUICMsgVersionNegotiation = 6
};

// Function to translate type values to strings
std::string QuicheQUICMsgTypeString(int type);

// Stream identifiers
typedef std::uint64_t QUICStreamID;

} // namespace quicr
