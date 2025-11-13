// SPDX-FileCopyrightText: Copyright (c) 2024 Cisco Systems
// SPDX-License-Identifier: BSD-2-Clause

/*
 * Chat Application using QuicR Bridge
 *
 * This example demonstrates a bidirectional chat application where a user can
 * both send and receive messages. The application can act as both publisher
 * and subscriber simultaneously.
 *
 * Based on the qclient.cpp example from libquicr.
 */

#include <pthread.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "quicr/quicr_bridge.h"

static volatile int keep_running = 1;
static volatile int can_send_data = 0;
static char username[256] = "user";
static pthread_t input_thread_handle = 0;

void
signal_handler(int signum)
{
    printf("\nReceived signal %d, shutting down...\n", signum);
    keep_running = 0;

    // Cancel the input thread to unblock it from fgets()
    if (input_thread_handle != 0) {
        pthread_cancel(input_thread_handle);
    }
}

void
status_callback(qbridge_connection_status_t status, void* user_data)
{
    printf("Client status changed: %s\n", qbridge_status_to_string(status));
}

void
object_published_callback(qbridge_group_id_t group_id,
                          qbridge_object_id_t object_id,
                          qbridge_result_t result,
                          void* user_data)
{
    if (result != QBRIDGE_OK) {
        printf("Failed to publish message: %s\n", qbridge_result_to_string(result));
    }
}

void
publish_status_callback(qbridge_publish_status_t status, bool can_publish, void* user_data)
{
    can_send_data = can_publish;

    switch (status) {
        case QBRIDGE_PUBLISH_STATUS_OK:
            printf("Ready to send messages\n");
            break;
        case QBRIDGE_PUBLISH_STATUS_NO_SUBSCRIBERS:
            printf("Waiting for participants...\n");
            break;
        case QBRIDGE_PUBLISH_STATUS_PAUSED:
            printf("Chat paused\n");
            break;
        default:
            break;
    }
}

void
object_received_callback(const qbridge_object_t* object, void* user_data)
{
    if (!object || !object->payload.data || object->payload.length == 0) {
        return;
    }

    // Extract the message
    char received_msg[1280];
    size_t msg_len =
      object->payload.length < sizeof(received_msg) - 1 ? object->payload.length : sizeof(received_msg) - 1;
    memcpy(received_msg, object->payload.data, msg_len);
    received_msg[msg_len] = '\0';

    // Check if this message is from us (to avoid printing duplicates)
    // Format is "[HH:MM:SS] username: message"
    // We'll look for "] username: " pattern
    char pattern[280];
    snprintf(pattern, sizeof(pattern), "] %s: ", username);

    if (strstr(received_msg, pattern) != NULL) {
        // This is our own message coming back from the server, skip it
        return;
    }

    // Print the received message from another user with remote label
    // Message already contains the sender's username from when they sent it
    printf("[Remote] %s\n", received_msg);
    fflush(stdout);
}

void
print_usage(const char* program_name)
{
    printf("Usage: %s [OPTIONS]\n", program_name);
    printf("QuicR Bridge Chat Application\n\n");
    printf("Options:\n");
    printf("  -h, --help              Show this help message\n");
    printf("  -s, --server HOSTNAME   Server hostname (default: 127.0.0.1)\n");
    printf("  -p, --port PORT         Server port (default: 33435)\n");
    printf("  -r, --room ROOM         Chat room name (default: general)\n");
    printf("  -u, --username NAME     Your username (default: user)\n");
    printf("  -a, --announce          Use announce flow instead of publish flow\n");
    printf("\nExample:\n");
    printf("  %s --server 127.0.0.1 --port 33435 --room general --username Alice\n", program_name);
}

typedef struct
{
    qbridge_publish_track_handler_t* publish_handler;
    uint64_t group_id;
    uint64_t object_id;
    pthread_mutex_t* mutex;
} input_thread_args_t;

void*
input_thread_func(void* arg)
{
    input_thread_args_t* args = (input_thread_args_t*)arg;
    char message[1024];

    while (keep_running) {
        if (fgets(message, sizeof(message), stdin) == NULL) {
            break;
        }

        // Remove newline
        size_t len = strlen(message);
        if (len > 0 && message[len - 1] == '\n') {
            message[len - 1] = '\0';
            len--;
        }

        if (len == 0 || !keep_running) {
            continue;
        }

        // Format message with username
        char formatted_message[1280];
        time_t now = time(NULL);
        struct tm* tm_info = localtime(&now);
        char time_str[32];
        strftime(time_str, sizeof(time_str), "%H:%M:%S", tm_info);

        snprintf(formatted_message, sizeof(formatted_message), "[%s] %s: %s", time_str, username, message);

        // Echo the formatted message locally so user sees their own username
        printf("[Local] %s\n", formatted_message);
        fflush(stdout);

        // Publish the message
        if (can_send_data && qbridge_publish_track_can_publish(args->publish_handler)) {
            pthread_mutex_lock(args->mutex);

            qbridge_object_headers_t headers = { .group_id = args->group_id,
                                                 .subgroup_id = 0,
                                                 .object_id = args->object_id,
                                                 .priority = QBRIDGE_PRIORITY_NORMAL,
                                                 .ttl_ms = 5000,
                                                 .cacheable = true };

            qbridge_result_t result = qbridge_publish_object_with_headers(
              args->publish_handler, &headers, (const uint8_t*)formatted_message, strlen(formatted_message));

            if (result == QBRIDGE_OK) {
                args->object_id++;
                if (args->object_id % 100 == 0) {
                    args->group_id++;
                    args->object_id = 0;
                }
            }

            pthread_mutex_unlock(args->mutex);
        }
    }

    return NULL;
}

int
main(int argc, char* argv[])
{
    printf("QuicR Bridge Chat Application\n");
    printf("Type your messages and press Enter to send\n");
    printf("Press Ctrl+C to exit\n\n");

    // Check for help option
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
            print_usage(argv[0]);
            return 0;
        }
    }

    // Set up signal handlers
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);

    // Initialize client configuration
    qbridge_client_config_t config;
    qbridge_client_config_init(&config);

    strncpy(config.server_hostname, "127.0.0.1", sizeof(config.server_hostname) - 1);
    config.server_port = 33435;

    const char* room_name = "general";
    int use_announce = 0;

    // Parse command line arguments
    for (int i = 1; i < argc; i++) {
        if ((strcmp(argv[i], "-s") == 0 || strcmp(argv[i], "--server") == 0) && i + 1 < argc) {
            strncpy(config.server_hostname, argv[++i], sizeof(config.server_hostname) - 1);
        } else if ((strcmp(argv[i], "-p") == 0 || strcmp(argv[i], "--port") == 0) && i + 1 < argc) {
            config.server_port = (uint16_t)atoi(argv[++i]);
        } else if ((strcmp(argv[i], "-r") == 0 || strcmp(argv[i], "--room") == 0) && i + 1 < argc) {
            room_name = argv[++i];
        } else if ((strcmp(argv[i], "-u") == 0 || strcmp(argv[i], "--username") == 0) && i + 1 < argc) {
            strncpy(username, argv[++i], sizeof(username) - 1);
        } else if (strcmp(argv[i], "-a") == 0 || strcmp(argv[i], "--announce") == 0) {
            use_announce = 1;
        }
    }

    config.debug_logs = false;
    printf("Connecting to %s:%d\n", config.server_hostname, config.server_port);
    printf("Joining chat room: %s\n", room_name);
    printf("Username: %s\n\n", username);

    // Create client
    qbridge_client_t* client = qbridge_client_create(&config);
    if (!client) {
        printf("Failed to create client\n");
        return 1;
    }

    qbridge_client_set_status_callback(client, status_callback, NULL);

    // Connect to server
    qbridge_result_t result = qbridge_client_connect(client);
    if (result != QBRIDGE_OK) {
        printf("Failed to connect: %s\n", qbridge_result_to_string(result));
        qbridge_client_destroy(client);
        return 1;
    }

    // Wait for connection
    printf("Waiting for connection...\n");
    while (keep_running && qbridge_client_get_status(client) == QBRIDGE_STATUS_CONNECTING) {
        usleep(100000);
    }

    if (!keep_running || qbridge_client_get_status(client) != QBRIDGE_STATUS_READY) {
        printf("Failed to connect to server\n");
        qbridge_client_destroy(client);
        return 1;
    }

    printf("Connected!\n\n");

    // Create namespace for the chat room
    char namespace_str[256];
    snprintf(namespace_str, sizeof(namespace_str), "chat/%s", room_name);

    qbridge_namespace_t ns;
    result = qbridge_namespace_from_string(&ns, namespace_str);
    if (result != QBRIDGE_OK) {
        printf("Failed to create namespace: %s\n", qbridge_result_to_string(result));
        qbridge_client_destroy(client);
        return 1;
    }

    // Publish namespace if using announce flow
    if (use_announce) {
        result = qbridge_client_publish_namespace(client, &ns);
        if (result != QBRIDGE_OK) {
            printf("Failed to publish namespace: %s\n", qbridge_result_to_string(result));
        }
    }

    // Subscribe to receive messages from others
    qbridge_subscribe_track_config_t sub_config;
    qbridge_subscribe_track_config_init(&sub_config);

    result = qbridge_full_track_name_from_strings(&sub_config.full_track_name, namespace_str, "messages");
    if (result != QBRIDGE_OK) {
        printf("Failed to create subscribe track name: %s\n", qbridge_result_to_string(result));
        qbridge_client_destroy(client);
        return 1;
    }

    sub_config.priority = QBRIDGE_PRIORITY_NORMAL;

    qbridge_subscribe_track_handler_t* subscribe_handler =
      qbridge_create_subscribe_track_handler(&sub_config, object_received_callback, NULL);

    if (!subscribe_handler) {
        printf("Failed to create subscribe track handler\n");
        qbridge_client_destroy(client);
        return 1;
    }

    result = qbridge_client_subscribe_track(client, subscribe_handler);
    if (result != QBRIDGE_OK) {
        printf("Failed to subscribe to track: %s\n", qbridge_result_to_string(result));
        qbridge_destroy_subscribe_track_handler(subscribe_handler);
        qbridge_client_destroy(client);
        return 1;
    }

    // Create publish track for sending messages
    qbridge_publish_track_config_t pub_config;
    qbridge_publish_track_config_init(&pub_config);

    result = qbridge_full_track_name_from_strings(&pub_config.full_track_name, namespace_str, "messages");
    if (result != QBRIDGE_OK) {
        printf("Failed to create publish track name: %s\n", qbridge_result_to_string(result));
        qbridge_client_unsubscribe_track(client, subscribe_handler);
        qbridge_destroy_subscribe_track_handler(subscribe_handler);
        qbridge_client_destroy(client);
        return 1;
    }

    pub_config.use_announce = use_announce;
    pub_config.delivery_mode = QBRIDGE_DELIVERY_MODE_DATAGRAM;
    pub_config.default_priority = QBRIDGE_PRIORITY_NORMAL;
    pub_config.default_ttl_ms = 5000;

    qbridge_publish_track_handler_t* publish_handler = qbridge_create_publish_track_handler_with_status(
      &pub_config, object_published_callback, publish_status_callback, NULL);

    if (!publish_handler) {
        printf("Failed to create publish track handler\n");
        qbridge_client_unsubscribe_track(client, subscribe_handler);
        qbridge_destroy_subscribe_track_handler(subscribe_handler);
        qbridge_client_destroy(client);
        return 1;
    }

    result = qbridge_client_publish_track(client, publish_handler);
    if (result != QBRIDGE_OK) {
        printf("Failed to publish track: %s\n", qbridge_result_to_string(result));
        qbridge_destroy_publish_track_handler(publish_handler);
        qbridge_client_unsubscribe_track(client, subscribe_handler);
        qbridge_destroy_subscribe_track_handler(subscribe_handler);
        qbridge_client_destroy(client);
        return 1;
    }

    printf("Chat ready! Start typing...\n\n");

    // Start input thread for reading user messages
    pthread_mutex_t pub_mutex = PTHREAD_MUTEX_INITIALIZER;

    input_thread_args_t thread_args = {
        .publish_handler = publish_handler, .group_id = 0, .object_id = 0, .mutex = &pub_mutex
    };

    pthread_create(&input_thread_handle, NULL, input_thread_func, &thread_args);

    // Main loop - just keep the application running
    while (keep_running) {
        usleep(100000);
    }

    // Wait for input thread to finish (it will exit quickly after being cancelled)
    pthread_join(input_thread_handle, NULL);
    pthread_mutex_destroy(&pub_mutex);

    printf("\nShutting down chat...\n");

    // Cleanup
    qbridge_client_unpublish_track(client, publish_handler);
    qbridge_client_unsubscribe_track(client, subscribe_handler);

    if (use_announce) {
        qbridge_client_unpublish_namespace(client, &ns);
    }

    qbridge_destroy_publish_track_handler(publish_handler);
    qbridge_destroy_subscribe_track_handler(subscribe_handler);

    qbridge_client_disconnect(client);
    qbridge_client_destroy(client);

    printf("Chat shut down complete.\n");
    return 0;
}
