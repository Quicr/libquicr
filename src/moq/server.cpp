/*
 *  Copyright (C) 2024
 *  Cisco Systems, Inc.
 *  All Rights Reserved
 */

#include <moq/server.h>

namespace moq {
    Server::Status Server::Start()
    {
        return Transport::Start();
    }

    void Server::Stop()
    {
        stop_ = true;
        Transport::Stop();
    }

    void Server::NewConnectionAccepted(moq::ConnectionHandle, const int&) {
    }
} // namespace moq
