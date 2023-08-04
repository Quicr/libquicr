/*
 *  quiche_types.cpp
 *
 *  Copyright (C) 2023
 *  Cisco Systems, Inc.
 *  All Rights Reserved.
 *
 *  Description:
 *      This file defines functions used in quiche for which no C interface
 *      exists to define them.
 *
 *  Portability Issues:
 *      None.
 */

#include "quiche_types.h"

namespace quicr::h3 {

std::string QuicheQUICMsgTypeString(int type)
{
    std::string type_string;

    switch (type)
    {
        case 1:
            type_string = "Initial";
            break;

        case 2:
            type_string = "Retry";
            break;

        case 3:
            type_string = "Handshake";
            break;

        case 4:
            type_string = "ZeroRTT";
            break;

        case 5:
            type_string = "Short";
            break;

        case 6:
            type_string = "Negotiation";
            break;

        default:
            type_string = "Invalid";
            break;
    }

    return type_string;
}

} // namespace quicr::h3
