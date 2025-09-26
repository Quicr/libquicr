// SPDX-FileCopyrightText: Copyright (c) 2024 Cisco Systems
// SPDX-License-Identifier: BSD-2-Clause

#include "custom_event_loop.h"

#include <picoquic.h>
#include <picoquic_utils.h>

#include <arpa/inet.h>
#include <cstring>
#include <netinet/in.h>

namespace quicr {

    CustomEventLoopManager::CustomEventLoopManager()
      : quic_(nullptr)
      , callback_(nullptr)
      , callback_ctx_(nullptr)
    {
    }

    CustomEventLoopManager::~CustomEventLoopManager()
    {
        stop_async();
    }

    int CustomEventLoopManager::run_packet_loop(picoquic_quic_t* quic,
                                                int local_port,
                                                int local_af,
                                                int /* dest_if */,
                                                int /* socket_buffer_size */,
                                                int /* do_not_use_gso */,
                                                picoquic_packet_loop_cb_fn loop_callback,
                                                void* loop_callback_ctx)
    {
        // Use static manager to ensure proper lifecycle management
        static CustomEventLoopManager manager;

        // Start the async event loop
        int ret = manager.start_async(quic, local_port, local_af, loop_callback, loop_callback_ctx);
        if (ret != 0) {
            return ret;
        }

        // Block until the event loop thread completes (maintains original semantics)
        // This ensures the function only returns when the packet loop actually terminates
        int exit_code = 0;
        if (manager.exit_promise_) {
            auto exit_future = manager.exit_promise_->get_future();
            exit_code = exit_future.get();
        }

        if (manager.event_thread_.joinable()) {
            manager.event_thread_.join();
        }

        // Return the actual exit code from the event loop
        return exit_code;
    }

    int CustomEventLoopManager::start_async(picoquic_quic_t* quic,
                                            int local_port,
                                            int local_af,
                                            picoquic_packet_loop_cb_fn loop_callback,
                                            void* loop_callback_ctx)
    {
        // Check if already running
        if (running_.load()) {
            return 0; // Already started
        }

        // Set up instance variables
        quic_ = quic;
        callback_ = loop_callback;
        callback_ctx_ = loop_callback_ctx;

        // Create promises for initialization and exit
        init_promise_ = std::make_unique<std::promise<int>>();
        exit_promise_ = std::make_unique<std::promise<int>>();
        auto init_future = init_promise_->get_future();

        // Start the event loop thread
        event_thread_ = std::thread(&CustomEventLoopManager::run_event_loop_thread, this, local_port, local_af);

        // Wait for initialization to complete
        return init_future.get();
    }

    void CustomEventLoopManager::stop_async()
    {
        if (!running_.load()) {
            return; // Not running
        }

        // Signal stop
        should_stop_.store(true);

        // Stop the libuv loop from the event thread
        if (initialized_.load()) {
            uv_stop(&loop_);
        }

        // Wait for thread to complete
        if (event_thread_.joinable()) {
            event_thread_.join();
        }

        running_.store(false);
    }

    void CustomEventLoopManager::run_event_loop_thread(int local_port, int local_af)
    {
        int ret = initialize(local_port, local_af);
        if (ret != 0) {
            if (init_promise_) {
                init_promise_->set_value(ret);
            }
            if (exit_promise_) {
                exit_promise_->set_value(ret);
            }
            return;
        }

        running_.store(true);

        // Call ready callback (mimics picoquic_packet_loop behavior)
        {
            std::lock_guard<std::mutex> lock(picoquic_mutex_);
            picoquic_packet_loop_options_t options = {};
            options.do_time_check = 1;
            ret = callback_(quic_, picoquic_packet_loop_ready, callback_ctx_, &options);
        }

        if (ret != 0) {
            cleanup();
            if (init_promise_) {
                init_promise_->set_value(ret);
            }
            if (exit_promise_) {
                exit_promise_->set_value(ret);
            }
            running_.store(false);
            return;
        }

        // Start timer for first wake-up
        schedule_next_timer();

        // Signal successful initialization
        if (init_promise_) {
            init_promise_->set_value(0);
        }

        // Run event loop in this dedicated thread
        ret = uv_run(&loop_, UV_RUN_DEFAULT);

        // Cleanup after loop exits
        cleanup();
        running_.store(false);

        // Signal event loop completion with exit code
        if (exit_promise_) {
            exit_promise_->set_value(ret);
        }
    }

    int CustomEventLoopManager::initialize(int local_port, int local_af)
    {
        if (initialized_) {
            return 0;
        }

        // Initialize libuv loop
        int ret = uv_loop_init(&loop_);
        if (ret != 0) {
            return ret;
        }

        // Initialize UDP socket
        ret = uv_udp_init(&loop_, &udp_socket_);
        if (ret != 0) {
            uv_loop_close(&loop_);
            return ret;
        }
        udp_socket_.data = this;

        // Initialize timer
        ret = uv_timer_init(&loop_, &timer_);
        if (ret != 0) {
            uv_close((uv_handle_t*)&udp_socket_, nullptr);
            uv_loop_close(&loop_);
            return ret;
        }
        timer_.data = this;

        // Bind UDP socket
        struct sockaddr_storage bind_addr = {};
        if (local_af == AF_INET6) {
            struct sockaddr_in6* addr6 = (struct sockaddr_in6*)&bind_addr;
            addr6->sin6_family = AF_INET6;
            addr6->sin6_port = htons(local_port);
            addr6->sin6_addr = in6addr_any;
        } else {
            struct sockaddr_in* addr4 = (struct sockaddr_in*)&bind_addr;
            addr4->sin_family = AF_INET;
            addr4->sin_port = htons(local_port);
            addr4->sin_addr.s_addr = INADDR_ANY;
        }

        ret = uv_udp_bind(&udp_socket_, (struct sockaddr*)&bind_addr, 0);
        if (ret != 0) {
            uv_close((uv_handle_t*)&timer_, nullptr);
            uv_close((uv_handle_t*)&udp_socket_, nullptr);
            uv_loop_close(&loop_);
            return ret;
        }

        // Start receiving
        ret = uv_udp_recv_start(&udp_socket_, static_alloc_cb, static_udp_recv_cb);
        if (ret != 0) {
            uv_close((uv_handle_t*)&timer_, nullptr);
            uv_close((uv_handle_t*)&udp_socket_, nullptr);
            uv_loop_close(&loop_);
            return ret;
        }

        // Cache local address to avoid syscall in hot path
        int local_addr_len = sizeof(cached_local_addr_);
        ret = uv_udp_getsockname(&udp_socket_, (struct sockaddr*)&cached_local_addr_, &local_addr_len);
        if (ret == 0) {
            local_addr_cached_ = true;
        }

        initialized_ = true;
        return 0;
    }

    void CustomEventLoopManager::cleanup()
    {
        if (!initialized_) {
            return;
        }

        should_stop_ = true;

        // Stop timer and UDP receiving
        uv_timer_stop(&timer_);
        uv_udp_recv_stop(&udp_socket_);

        // Close handles
        uv_close((uv_handle_t*)&timer_, static_close_cb);
        uv_close((uv_handle_t*)&udp_socket_, static_close_cb);

        // Run loop once more to process close callbacks
        uv_run(&loop_, UV_RUN_ONCE);

        // Close loop
        uv_loop_close(&loop_);

        initialized_ = false;
    }

    // Static callback implementations
    void CustomEventLoopManager::static_alloc_cb(uv_handle_t* handle, size_t /* suggested_size */, uv_buf_t* buf)
    {
        auto* manager = static_cast<CustomEventLoopManager*>(handle->data);
        buf->base = (char*)manager->recv_buffer_;
        buf->len = sizeof(manager->recv_buffer_);
    }

    void CustomEventLoopManager::static_udp_recv_cb(uv_udp_t* handle,
                                                    ssize_t nread,
                                                    const uv_buf_t* buf,
                                                    const struct sockaddr* addr,
                                                    unsigned /* flags */)
    {
        auto* manager = static_cast<CustomEventLoopManager*>(handle->data);
        if (nread > 0 && addr && !manager->should_stop_) {
            manager->on_udp_recv(buf, nread, addr);
        }
    }

    void CustomEventLoopManager::static_timer_cb(uv_timer_t* handle)
    {
        auto* manager = static_cast<CustomEventLoopManager*>(handle->data);
        if (!manager->should_stop_) {
            manager->on_timer();
        }
    }

    void CustomEventLoopManager::static_close_cb(uv_handle_t* /* handle */)
    {
        // Nothing to do - cleanup handled elsewhere
    }

    void CustomEventLoopManager::on_udp_recv(const uv_buf_t* buf, ssize_t nread, const struct sockaddr* addr)
    {
        // Use cached local address to avoid syscall in hot path
        struct sockaddr_storage local_addr;
        if (local_addr_cached_) {
            local_addr = cached_local_addr_;
        } else {
            // Fallback to syscall if cache failed
            int local_addr_len = sizeof(local_addr);
            uv_udp_getsockname(&udp_socket_, (struct sockaddr*)&local_addr, &local_addr_len);
        }

        struct sockaddr_storage addr_from = {};
        std::memcpy(&addr_from, addr, sizeof(struct sockaddr_storage));

        // Get current time once outside critical section
        uint64_t current_time = picoquic_current_time();

        // Minimize critical section - only picoquic_incoming_packet_ex
        {
            std::lock_guard<std::mutex> lock(picoquic_mutex_);

            picoquic_cnx_t* first_cnx = nullptr;
            int ret = picoquic_incoming_packet_ex(quic_,
                                                  (uint8_t*)buf->base,
                                                  nread,
                                                  (struct sockaddr*)&addr_from,
                                                  (struct sockaddr*)&local_addr,
                                                  0, // TODO: Interface?
                                                  0, // TODO: ECN bits?
                                                  &first_cnx,
                                                  current_time);

            // Only process outbound packets if incoming packet was successful
            if (ret == 0) {
                process_outbound_packets();
            }
        }

        // Schedule timer outside mutex to reduce contention
        schedule_next_timer();
    }

    void CustomEventLoopManager::on_timer()
    {
        static uint64_t last_timer_time = 0;
        uint64_t current_time = picoquic_current_time();
        uint64_t delta_t = last_timer_time ? (current_time - last_timer_time) : 0;
        last_timer_time = current_time;

        {
            std::lock_guard<std::mutex> lock(picoquic_mutex_);

            // Call the existing PqLoopCb
            packet_loop_time_check_arg_t time_check = { .current_time = current_time,
                                                        .delta_t = static_cast<int64_t>(delta_t) };

            int ret = callback_(quic_, picoquic_packet_loop_time_check, callback_ctx_, &time_check);
            if (ret == PICOQUIC_NO_ERROR_TERMINATE_PACKET_LOOP) {
                should_stop_.store(true);
                uv_stop(&loop_);
                return;
            }

            // Process any pending outbound packets
            process_outbound_packets();
        }

        // Schedule next timer (outside mutex)
        schedule_next_timer();
    }

    void CustomEventLoopManager::process_outbound_packets()
    {
        // Note: This method is called from within picoquic_mutex_ lock context
        // so we don't need additional locking here

        struct sockaddr_storage addr_to = {};
        struct sockaddr_storage addr_from = {};
        size_t send_length = 0;
        int if_index = 0;
        picoquic_connection_id_t log_cid = picoquic_null_connection_id;
        picoquic_cnx_t* last_cnx = nullptr;

        // Loop until no more packets to send
        while (!should_stop_.load()) {
            int ret = picoquic_prepare_next_packet_ex(quic_,
                                                      picoquic_current_time(),
                                                      send_buffer_,
                                                      sizeof(send_buffer_),
                                                      &send_length,
                                                      &addr_to,
                                                      &addr_from,
                                                      &if_index,
                                                      &log_cid,
                                                      &last_cnx,
                                                      nullptr);

            if (ret != 0 || send_length == 0) {
                break; // Nothing more to send
            }

            // Send the packet using libuv
            uv_buf_t send_buf = uv_buf_init((char*)send_buffer_, send_length);

            uv_udp_send_t* send_req = new uv_udp_send_t;

            int send_ret = uv_udp_send(send_req,
                                       &udp_socket_,
                                       &send_buf,
                                       1,
                                       (struct sockaddr*)&addr_to,
                                       [](uv_udp_send_t* req, int /* status */) {
                                           delete req; // Clean up
                                       });

            if (send_ret != 0) {
                delete send_req;
                break;
            }
        }
    }

    void CustomEventLoopManager::schedule_next_timer()
    {
        std::unique_lock<std::mutex> lock(picoquic_mutex_, std::try_to_lock);
        uint64_t delay_ms = 1;

        if (lock.owns_lock()) {
            uint64_t current_time = picoquic_current_time();
            int64_t delay_us = picoquic_get_next_wake_delay(quic_, current_time, 500);

            if (delay_us <= 0) {
                delay_us = 500;
            }

            delay_ms = delay_us / 1000; // Convert to milliseconds
            if (delay_ms == 0) {
                delay_ms = 1; // Minimum 1ms
            }
        }

        // Start timer outside of mutex scope
        uv_timer_start(&timer_, static_timer_cb, delay_ms, 0);
    }

    int custom_picoquic_packet_loop(picoquic_quic_t* quic,
                                    int local_port,
                                    int local_af,
                                    int dest_if,
                                    int socket_buffer_size,
                                    int do_not_use_gso,
                                    picoquic_packet_loop_cb_fn loop_callback,
                                    void* loop_callback_ctx)
    {
        return CustomEventLoopManager::run_packet_loop(
          quic, local_port, local_af, dest_if, socket_buffer_size, do_not_use_gso, loop_callback, loop_callback_ctx);
    }

} // namespace quicr
