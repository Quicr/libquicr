// SPDX-FileCopyrightText: Copyright (c) 2026 Cisco Systems
// SPDX-License-Identifier: BSD-2-Clause

#pragma once

#include "quicr/detail/control_messages/message_reader.h"

namespace quicr::messages::control {

    struct RequestOk
    {
        static constexpr std::uint64_t kType = 0x7;

        const Parameters parameters;
        const TrackExtensions track_properties;

        explicit RequestOk(BytesSpan payload)
          : RequestOk(MessageReader{ payload })
        {
        }

      private:
        explicit RequestOk(MessageReader reader)
          : parameters(reader.Read<Parameters>())
          , track_properties(reader.Read<TrackExtensions>())
        {
            reader.ExpectDone();
        }
    };

    using PublishOk = RequestOk;
    using PublishNamespaceOk = RequestOk;
    using RequestUpdateOk = RequestOk;
    using SubscribeNamespaceOk = RequestOk;
    using SubscribeTracksOk = RequestOk;
    using TrackStatusOk = RequestOk;

} // namespace quicr::messages::control
