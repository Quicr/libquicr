#pragma once

#include <cstdint>

/*
 * Compiler NOTE:
 *
 * __attribute__((__packed__, aligned(1))); is used over "#pragma pack(push, 1)" and "#pragma pack(pop)"
 *      because only modern compilers support pragma
 *
 *  #if __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__ is used over "__attribute__((scalar_storage_order("big-endian")))"
 *      and "#pragma scalar_storage_order big-endian" because only modern compilers support scalar_storage_order.
 */

namespace qtransport {
    namespace udp_protocol {
        /* ------------------------------------------------------------------------
         * Wire messages
         * ------------------------------------------------------------------------
         */
        constexpr uint8_t kProtocolVersion = 1;

        /**
         * @brief UDP Protocol Types
         * @details Each UDP packet is encoded with a common header, which includes a type.
         */
        enum class ProtocolType : uint8_t {
            kConnect = 0,
            kConnectOk = 1,
            kDisconnect = 2,
            kReport = 3,
            kKeepalive = 4,

            kData = 10,
        };

        /**
         * @brief UDP Protocol Common Header
         * @brief Every UDP packet starts with this common header. The data that follows is defined by the type
         */
        struct CommonHeader {
            uint8_t version {kProtocolVersion};       /// Protocol version
            ProtocolType type;                        /// Indicates this is a peering message
        };

        /**
         * @brief Connect Message
         *
         * @details UDP Protocol starts off with a connect message. Messages will be discarded by the remote
         *      until the new connection sends a connect message.
         */
        struct ConnectMsg : CommonHeader {
            ConnectMsg() { type = ProtocolType::kConnect; }

            uint16_t idle_timeout { 120 };            /// Idle timeout in seconds. Must not be zero

        } __attribute__((__packed__, aligned(1)));

        /**
         * @brief Connect OK Message
         */
        struct ConnectOkMsg : CommonHeader {
            ConnectOkMsg() { type = ProtocolType::kConnectOk; }
        } __attribute__((__packed__, aligned(1)));

        /**
         * @brief Disconnect Message
         *
         * @details Disconnect notification. Remote will immediately purge/close the active connection
         */
        struct DisconnectMsg : CommonHeader {
            DisconnectMsg() { type = ProtocolType::kDisconnect; }
        } __attribute__((__packed__, aligned(1)));

        /**
         * @brief Keepalive Message
         *
         * @details Keepalive message. Sent only when no other messages have been sent in idle_timeout / 3.
         */
        struct KeepaliveMsg : CommonHeader {
            KeepaliveMsg() { type = ProtocolType::kKeepalive; }

            uint16_t ticks_ms { 0 };            /// Senders Tick millisecond value from start of report period, reset to zero on new report

        } __attribute__((__packed__, aligned(1)));

        /**
         * @brief Data Message
         *
         * @details Data message. Bytes following the header is the data
         *
         * @note Can be either type of DATA
         */
        struct DataMsg : CommonHeader {
            DataMsg() { type = ProtocolType::kData; }

            struct {
#if __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
                uint8_t discard     : 1;        /// Indicates that data should be discard
                uint8_t reserved    : 7;        /// Least significant bits reserved
#else
                uint8_t reserved    : 7;        /// Least significant bits reserved
                uint8_t discard     : 1;        /// Indicates that data should be discard
#endif
            } flags { 0 };                      /// Data header flags

            uint16_t report_id { 0 };           /// Report ID this data applies to
            uint16_t ticks_ms { 0 };            /// Senders Tick millisecond value from start of report period, reset to zero on new report

            /*
             * Following the data header are additional variable length integers
             */
            // remote_data_ctx_id -- The remote side data context ID. The data_ctx_id is learned out of band of the transport
        } __attribute__((__packed__, aligned(1)));


        /**
         * @brief Report Metrics
         */
         struct ReportMetrics {

             uint32_t total_packets { 0 };       /// Total number of packets received
             uint32_t total_bytes { 0 };         /// Total number of data (sans header) bytes received
             uint32_t duration_ms { 0 };         /// Duration in milliseconds of time from first to latest packet received
             uint16_t recv_ott_ms { 0 };         /// Senders One-way trip time in milliseconds to receiver

         } __attribute__((__packed__, aligned(1)));

        /**
         * @brief Report Message
         *
         * @details Report message. The remote will send a report message upon
         */
        struct ReportMessage : CommonHeader {
            ReportMessage() { type = ProtocolType::kReport; }

            uint16_t report_id { 0 };           /// Report ID of this report
            ReportMetrics metrics;

        } __attribute__((__packed__, aligned(1)));


    } // end UdpProtocol namespace
} // end qtransport namespace
