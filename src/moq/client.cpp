/*
 *  Copyright (C) 2024
 *  Cisco Systems, Inc.
 *  All Rights Reserved
 */

#include <moq/client.h>

namespace moq {

    Client::Status Client::Connect()
    {
        Transport::Start();
        return Status::kConnecting;
    }

    Client::Status Client::Disconnect()
    {
        Transport::Stop();
        return Status::kDisconnecting;
    }




} // namespace moq
