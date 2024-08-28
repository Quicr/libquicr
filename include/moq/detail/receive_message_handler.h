/*
 *  Copyright (C) 2024
 *  Cisco Systems, Inc.
 *  All Rights Reserved
 */

#pragma once

#include "transport.h"

#include <any>

namespace moq {

    /**
     * @brief Receive MoQT message handler
     *
     * @details Transport creates an instance of the receive message handler to
     *      process MoQT messages received.
     */
    class ReceiveMessageHandler
    {
      public:
        enum class ControlMessageStatus : uint8_t
        {
            kMessageIncomplete,            ///< control message is incomplete and more data is needed
            kMessageComplete,              ///< control message is complete and stream buffer get any has complete message
            kStreamBufferCannotBeZero,     ///< stream buffer cannot be zero when parsing message type
            kStreamBufferMissingType,      ///< connection context is missing message type
            kUnsupportedMessageType,       ///< Unsupported MOQT message type
        };

        struct ControlMessage {
            ControlMessageStatus status;                      ///< Status of the parse, if complete moq_message will be set
            messages::MoqMessageType message_type;            ///< MoQ message type parsed and stored in the **any**
        };

        enum class StreamDataMessageStatus : uint8_t
        {

        };

        ReceiveMessageHandler()
          : logger_(spdlog::stderr_color_mt("MRMH"))
        {}

        /**
         * @brief Process receive control messages
         *
         * @details Process the received control message from the stream buffer. When the returned
         *      status indicates **kMessageComplete**, the caller **SHOULD** use StreamBuffer::GetAny<type>()
         *      to get the message to be copied or used. Calling this method again after it was previously
         *      complete will result in the previous object being reset using StreamBuffer::RestAny()
         *
         * @param conn_ctx              connection context for the connection
         * @param stream_buffer         stream buffer reference to read data from (control stream)
         *
         * @return ControlMessage with status set to indicate if the message is complete and set
         */
        ControlMessage ProcessCtrlMessage(Transport::ConnectionContext& conn_ctx,
                                          std::shared_ptr<StreamBuffer<uint8_t>>& stream_buffer);

        /**
         * @brief Process receive object data from stream
         *
         * @param conn_ctx              connection context for the connection
         * @param stream_buffer         stream buffer reference to read data from (not control stream)
         *
         * @return StreamDataMessageStatus to indicate the result of the stream data processing
         */
        StreamDataMessageStatus ProcessStreamDataMessage(Transport::ConnectionContext& conn_ctx,
                                                         std::shared_ptr<StreamBuffer<uint8_t>>& stream_buffer);

      private:
        std::shared_ptr<spdlog::logger> logger_;

        bool last_control_message_complete_ { false };
    };

} // namespace moq
