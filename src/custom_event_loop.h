// SPDX-FileCopyrightText: Copyright (c) 2025 Cisco Systems
// SPDX-License-Identifier: BSD-2-Clause

#pragma once

#include <picoquic_packet_loop.h>
#include <uv.h>

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <future>
#include <mutex>
#include <thread>

namespace quicr {
    class CustomEventLoopManager
    {
      public:
        static int run_packet_loop(picoquic_quic_t* quic,
                                   int local_port,
                                   int local_af,
                                   int dest_if,
                                   int socket_buffer_size,
                                   int do_not_use_gso,
                                   picoquic_packet_loop_cb_fn loop_callback,
                                   void* loop_callback_ctx);

        CustomEventLoopManager();
        ~CustomEventLoopManager();

        int start_async(picoquic_quic_t* quic,
                        int local_port,
                        int local_af,
                        picoquic_packet_loop_cb_fn loop_callback,
                        void* loop_callback_ctx);
        void stop_async();

      private:
        int initialize(int local_port, int local_af);
        void cleanup();
        void on_udp_recv(const uv_buf_t* buf, ssize_t nread, const struct sockaddr* addr);
        void on_timer();
        void process_outbound_packets();
        void schedule_next_timer();

        // libuv callbacks
        static void static_udp_recv_cb(uv_udp_t* handle,
                                       ssize_t nread,
                                       const uv_buf_t* buf,
                                       const struct sockaddr* addr,
                                       unsigned flags);
        static void static_timer_cb(uv_timer_t* handle);
        static void static_alloc_cb(uv_handle_t* handle, size_t suggested_size, uv_buf_t* buf);
        static void static_close_cb(uv_handle_t* handle);
        uv_loop_t loop_;
        uv_udp_t udp_socket_;
        uv_timer_t timer_;

        picoquic_quic_t* quic_;
        picoquic_packet_loop_cb_fn callback_;
        void* callback_ctx_;

        static constexpr size_t BUFFER_SIZE = 64 * 1024;
        uint8_t recv_buffer_[BUFFER_SIZE];
        uint8_t send_buffer_[BUFFER_SIZE];

        std::atomic<bool> should_stop_{ false };
        std::atomic<bool> initialized_{ false };
        std::atomic<bool> running_{ false };

        struct sockaddr_storage cached_local_addr_;
        bool local_addr_cached_ = false;

        std::thread event_thread_;
        mutable std::mutex picoquic_mutex_;
        std::unique_ptr<std::promise<int>> init_promise_;
        std::unique_ptr<std::promise<int>> exit_promise_;
        std::condition_variable stop_cv_;
        std::mutex stop_mutex_;

        void run_event_loop_thread(int local_port, int local_af);
    };

    int custom_picoquic_packet_loop(picoquic_quic_t* quic,
                                    int local_port,
                                    int local_af,
                                    int dest_if,
                                    int socket_buffer_size,
                                    int do_not_use_gso,
                                    picoquic_packet_loop_cb_fn loop_callback,
                                    void* loop_callback_ctx);

} // namespace quicr
