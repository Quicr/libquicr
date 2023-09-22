/*
 *  quicr_client_common.h
 *
 *  Copyright (C) 2023
 *  Cisco Systems, Inc.
 *  All Rights Reserved
 *
 *  Description:
 *      This defines types and values that are common to both the QuicRClient
 *      and QuicRClientSession object.
 *
 *  Portability Issues:
 *      None.
 */

#pragma once

constexpr int MAX_FRAGMENT_NAMES_PENDING_PER_BUFFER = 5000;
constexpr int MAX_FRAGMENT_BUFFERS = 20;

