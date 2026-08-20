// SPDX-FileCopyrightText: Copyright (c) 2026 Cisco Systems
// SPDX-License-Identifier: BSD-2-Clause

#include <doctest/doctest.h>

#include "picoquic_connection.h"

using namespace quicr;

TEST_CASE("Removing a data context unregisters it")
{
    // The connection only stores the picoquic pointer, so a stand-in address is enough here.
    const auto connection = std::make_shared<PicoQuicConnection>(reinterpret_cast<picoquic_cnx_t*>(0x5000));

    const auto data_ctx = connection->AddDataContext(true, false);
    REQUIRE(data_ctx != nullptr);
    CHECK(data_ctx->IsRegistered());
    CHECK(connection->GetDataContext(data_ctx->GetID()) == data_ctx);

    CHECK(connection->RemoveDataContext(data_ctx->GetID()) == data_ctx);

    // The handle stays usable so teardown can finish, but the context is no longer registered.
    CHECK_FALSE(data_ctx->IsRegistered());
    CHECK(connection->GetDataContext(data_ctx->GetID()) == nullptr);

    // A second removal must report that there is nothing left to tear down.
    CHECK(connection->RemoveDataContext(data_ctx->GetID()) == nullptr);
}
