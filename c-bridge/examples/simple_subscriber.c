// SPDX-FileCopyrightText: Copyright (c) 2024 Cisco Systems
// SPDX-License-Identifier: BSD-2-Clause

#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "quicr/quicr_bridge.h"

static volatile int keep_running = 1;

void
signal_handler(int signum)
{
    printf("\nReceived signal %d, shutting down...\n", signum);
    keep_running = 0;
}

void
status_callback(qbridge_connection_status_t status, void* user_data)
{
    printf("Client status changed: %s\n", qbridge_status_to_string(status));
}

void
object_received_callback(const qbridge_object_t* object, void* user_data)
{
    printf("Received object: group=%llu, subgroup=%llu, object=%llu, priority=%d, ttl=%u, cacheable=%s, size=%zu\n",
           object->headers.group_id,
           object->headers.subgroup_id,
           object->headers.object_id,
           object->headers.priority,
           object->headers.ttl_ms,
           object->headers.cacheable ? "true" : "false",
           object->payload.length);

    // Print first 64 bytes of data as string (if printable)
    if (object->payload.data && object->payload.length > 0) {
        size_t print_len = object->payload.length > 64 ? 64 : object->payload.length;
        printf("Data: ");
        for (size_t i = 0; i < print_len; i++) {
            char c = object->payload.data[i];
            if (c >= 32 && c <= 126) {
                printf("%c", c);
            } else {
                printf(".");
            }
        }
        if (object->payload.length > 64) {
            printf("... (truncated)");
        }
        printf("\n");
    }
}

void
print_usage(const char* program_name)
{
    printf("Usage: %s [OPTIONS]\n", program_name);
    printf("QuicR Bridge Simple Subscriber\n\n");
    printf("Options:\n");
    printf("  -h, --help              Show this help message\n");
    printf("  -s, --server HOSTNAME   Server hostname (default: 127.0.0.1)\n");
    printf("  -p, --port PORT         Server port (default: 33435)\n");
    printf("  -n, --namespace NS      Namespace to subscribe (default: example/, track: example/publisher)\n");
    printf("  -t, --track TRACK       Track name (default: video_stream)\n");
    printf("  --start-group ID        Starting group ID (default: 0)\n");
    printf("  --end-group ID          Ending group ID (default: UINT64_MAX)\n");
    printf("  --start-object ID       Starting object ID (default: 0)\n");
    printf("  --end-object ID         Ending object ID (default: UINT64_MAX)\n");
    printf("  --priority LEVEL        Priority level 0-4 (default: 3=HIGH)\n");
    printf("  --group-order ORDER     Group order (default: 0)\n");
    printf("\nExample:\n");
    printf("  %s --server 127.0.0.1 --port 33435 --namespace example/publisher --track video_stream\n", program_name);
    printf("  %s --namespace example/publisher --track video_stream --start-group 5 --end-group 10\n", program_name);
}

int
main(int argc, char* argv[])
{
    printf("Starting QuicR Bridge Simple Subscriber\n");

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

    // Set default server configuration
    strncpy(config.server_hostname, "127.0.0.1", sizeof(config.server_hostname) - 1);
    config.server_port = 33435;

    // Default namespace and track
    const char* namespace_str = "example/";
    const char* track_namespace_str = "example/publisher";
    const char* track_name_str = "video_stream";
    uint64_t start_group = 0;
    uint64_t end_group = UINT64_MAX;
    uint64_t start_object = 0;
    uint64_t end_object = UINT64_MAX;
    uint64_t group_order = 0;
    qbridge_priority_t priority = QBRIDGE_PRIORITY_HIGH;

    // Parse command line arguments
    for (int i = 1; i < argc; i++) {
        if ((strcmp(argv[i], "-s") == 0 || strcmp(argv[i], "--server") == 0) && i + 1 < argc) {
            strncpy(config.server_hostname, argv[++i], sizeof(config.server_hostname) - 1);
        } else if ((strcmp(argv[i], "-p") == 0 || strcmp(argv[i], "--port") == 0) && i + 1 < argc) {
            config.server_port = (uint16_t)atoi(argv[++i]);
        } else if ((strcmp(argv[i], "-n") == 0 || strcmp(argv[i], "--namespace") == 0) && i + 1 < argc) {
            namespace_str = argv[++i];
            track_namespace_str = namespace_str; // Use same namespace for track
        } else if ((strcmp(argv[i], "-t") == 0 || strcmp(argv[i], "--track") == 0) && i + 1 < argc) {
            track_name_str = argv[++i];
        } else if (strcmp(argv[i], "--start-group") == 0 && i + 1 < argc) {
            start_group = (uint64_t)atoll(argv[++i]);
        } else if (strcmp(argv[i], "--end-group") == 0 && i + 1 < argc) {
            end_group = (uint64_t)atoll(argv[++i]);
        } else if (strcmp(argv[i], "--start-object") == 0 && i + 1 < argc) {
            start_object = (uint64_t)atoll(argv[++i]);
        } else if (strcmp(argv[i], "--end-object") == 0 && i + 1 < argc) {
            end_object = (uint64_t)atoll(argv[++i]);
        } else if (strcmp(argv[i], "--priority") == 0 && i + 1 < argc) {
            int prio = atoi(argv[++i]);
            if (prio >= 0 && prio <= 4) {
                priority = (qbridge_priority_t)prio;
            }
        } else if (strcmp(argv[i], "--group-order") == 0 && i + 1 < argc) {
            group_order = (uint64_t)atoll(argv[++i]);
        } else if (argv[i][0] != '-') {
            // Legacy support: first arg is hostname, second is port
            if (i == 1) {
                strncpy(config.server_hostname, argv[i], sizeof(config.server_hostname) - 1);
            } else if (i == 2) {
                config.server_port = (uint16_t)atoi(argv[i]);
            }
        }
    }

    config.debug_logs = true;
    printf("Connecting to %s:%d\n", config.server_hostname, config.server_port);
    printf("Namespace: %s, Track: %s/%s\n", namespace_str, track_namespace_str, track_name_str);
    printf("Group range: %llu - %llu\n", start_group, end_group);
    printf("Object range: %llu - %llu\n", start_object, end_object);
    printf("Priority: %d, Group order: %llu\n\n", priority, group_order);

    // Create client
    qbridge_client_t* client = qbridge_client_create(&config);
    if (!client) {
        printf("Failed to create client\n");
        return 1;
    }

    // Set up callbacks
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
        usleep(100000); // 100ms
    }

    if (!keep_running) {
        qbridge_client_destroy(client);
        return 0;
    }

    if (qbridge_client_get_status(client) != QBRIDGE_STATUS_READY) {
        printf("Failed to connect to server\n");
        qbridge_client_destroy(client);
        return 1;
    }

    printf("Connected successfully!\n");

    // Subscribe to namespace to discover announcements
    qbridge_namespace_t namespace;
    result = qbridge_namespace_from_string(&namespace, namespace_str);
    if (result != QBRIDGE_OK) {
        printf("Failed to create namespace: %s\n", qbridge_result_to_string(result));
        qbridge_client_destroy(client);
        return 1;
    }

    // Create subscribe track handler
    qbridge_subscribe_track_config_t track_config;
    qbridge_subscribe_track_config_init(&track_config);

    result = qbridge_full_track_name_from_strings(&track_config.full_track_name, track_namespace_str, track_name_str);
    if (result != QBRIDGE_OK) {
        printf("Failed to create track name: %s\n", qbridge_result_to_string(result));
        qbridge_client_destroy(client);
        return 1;
    }

    track_config.track_alias = 1;
    track_config.priority = priority;
    track_config.group_order = group_order;
    track_config.start_group_id = start_group;
    track_config.start_object_id = start_object;
    track_config.end_group_id = end_group;
    track_config.end_object_id = end_object;

    qbridge_subscribe_track_handler_t* track_handler =
      qbridge_create_subscribe_track_handler(&track_config, object_received_callback, NULL);

    if (!track_handler) {
        printf("Failed to create subscribe track handler\n");
        qbridge_client_destroy(client);
        return 1;
    }

    // Subscribe to track
    result = qbridge_client_subscribe_track(client, track_handler);
    if (result != QBRIDGE_OK) {
        printf("Failed to subscribe to track: %s\n", qbridge_result_to_string(result));
        qbridge_destroy_subscribe_track_handler(track_handler);
        qbridge_client_destroy(client);
        return 1;
    }

    printf("Subscribed to track: %s/%s\n", track_namespace_str, track_name_str);

    printf("Waiting for objects (Ctrl+C to stop)...\n");

    // Main loop - just wait for objects to arrive via callbacks
    while (keep_running) {
        usleep(100000); // 100ms
    }

    printf("Shutting down subscriber...\n");

    // Cleanup
    qbridge_client_unsubscribe_track(client, track_handler);
    qbridge_destroy_subscribe_track_handler(track_handler);

    qbridge_client_disconnect(client);
    qbridge_client_destroy(client);

    printf("Subscriber shut down complete.\n");
    return 0;
}
