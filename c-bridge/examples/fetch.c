// SPDX-FileCopyrightText: Copyright (c) 2024 Cisco Systems
// SPDX-License-Identifier: BSD-2-Clause

/*
 * Fetch Example using QuicR Bridge
 *
 * This example demonstrates how to fetch objects from a specific range
 * (group and object IDs) from a track. This is useful for retrieving
 * historical data or cached content.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>

#include "quicr/quicr_bridge.h"

static volatile int keep_running = 1;
static volatile int fetch_complete = 0;
static int objects_received = 0;

void signal_handler(int signum) {
    printf("\nReceived signal %d, shutting down...\n", signum);
    keep_running = 0;
}

void status_callback(qbridge_connection_status_t status, void* user_data) {
    printf("Client status changed: %s\n", qbridge_status_to_string(status));
}

void object_received_callback(const qbridge_object_t* object, void* user_data) {
    if (!object || !object->payload.data) {
        return;
    }

    objects_received++;

    printf("Fetched object [group=%llu, object=%llu, size=%zu]: %.*s\n",
           object->headers.group_id,
           object->headers.object_id,
           object->payload.length,
           (int)object->payload.length,
           (char*)object->payload.data);
}

void print_usage(const char* program_name) {
    printf("Usage: %s [OPTIONS]\n", program_name);
    printf("QuicR Bridge Fetch Example\n\n");
    printf("Options:\n");
    printf("  -h, --help              Show this help message\n");
    printf("  -s, --server HOSTNAME   Server hostname (default: 127.0.0.1)\n");
    printf("  -p, --port PORT         Server port (default: 33435)\n");
    printf("  -n, --namespace NS      Namespace (default: example/fetch)\n");
    printf("  -t, --track TRACK       Track name (default: data)\n");
    printf("  --start-group ID        Starting group ID (default: 0)\n");
    printf("  --end-group ID          Ending group ID (default: 10)\n");
    printf("  --start-object ID       Starting object ID (default: 0)\n");
    printf("  --end-object ID         Ending object ID (default: 100)\n");
    printf("\nExample:\n");
    printf("  %s --server 127.0.0.1 --start-group 0 --end-group 5 --start-object 0 --end-object 50\n", program_name);
}

int main(int argc, char* argv[]) {
    printf("Starting QuicR Bridge Fetch Example\n");

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

    const char* namespace_str = "example/fetch";
    const char* track_name_str = "data";
    uint64_t start_group = 0;
    uint64_t end_group = 10;
    uint64_t start_object = 0;
    uint64_t end_object = 100;

    // Parse command line arguments
    for (int i = 1; i < argc; i++) {
        if ((strcmp(argv[i], "-s") == 0 || strcmp(argv[i], "--server") == 0) && i + 1 < argc) {
            strncpy(config.server_hostname, argv[++i], sizeof(config.server_hostname) - 1);
        } else if ((strcmp(argv[i], "-p") == 0 || strcmp(argv[i], "--port") == 0) && i + 1 < argc) {
            config.server_port = (uint16_t)atoi(argv[++i]);
        } else if ((strcmp(argv[i], "-n") == 0 || strcmp(argv[i], "--namespace") == 0) && i + 1 < argc) {
            namespace_str = argv[++i];
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
        }
    }

    config.debug_logs = true;
    printf("Connecting to %s:%d\n", config.server_hostname, config.server_port);
    printf("Fetching from namespace: %s, track: %s\n", namespace_str, track_name_str);
    printf("Range: group [%llu - %llu], object [%llu - %llu]\n\n",
           start_group, end_group, start_object, end_object);

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

    if (!keep_running) {
        qbridge_client_destroy(client);
        return 0;
    }

    if (qbridge_client_get_status(client) != QBRIDGE_STATUS_READY) {
        printf("Failed to connect to server\n");
        qbridge_client_destroy(client);
        return 1;
    }

    printf("Connected successfully!\n\n");

    // Create fetch track handler
    qbridge_fetch_track_config_t fetch_config;
    qbridge_fetch_track_config_init(&fetch_config);

    result = qbridge_full_track_name_from_strings(&fetch_config.full_track_name,
                                                       namespace_str,
                                                       track_name_str);
    if (result != QBRIDGE_OK) {
        printf("Failed to create track name: %s\n", qbridge_result_to_string(result));
        qbridge_client_destroy(client);
        return 1;
    }

    fetch_config.priority = QBRIDGE_PRIORITY_NORMAL;
    fetch_config.start_group_id = start_group;
    fetch_config.end_group_id = end_group;
    fetch_config.start_object_id = start_object;
    fetch_config.end_object_id = end_object;

    qbridge_fetch_track_handler_t* fetch_handler =
        qbridge_create_fetch_track_handler(&fetch_config, object_received_callback, NULL);

    if (!fetch_handler) {
        printf("Failed to create fetch track handler\n");
        qbridge_client_destroy(client);
        return 1;
    }

    printf("Starting fetch...\n");

    // Fetch track
    result = qbridge_client_fetch_track(client, fetch_handler);
    if (result != QBRIDGE_OK) {
        printf("Failed to fetch track: %s\n", qbridge_result_to_string(result));
        qbridge_destroy_fetch_track_handler(fetch_handler);
        qbridge_client_destroy(client);
        return 1;
    }

    // Wait for fetch to complete or user to cancel
    // In a real implementation, you would want to add logic to detect
    // when the fetch is complete based on status callbacks
    printf("Fetching... Press Ctrl+C to cancel\n\n");

    while (keep_running && !fetch_complete) {
        sleep(1);
    }

    printf("\nFetch operation finished\n");
    printf("Total objects received: %d\n", objects_received);

    // Cleanup
    qbridge_client_cancel_fetch_track(client, fetch_handler);
    qbridge_destroy_fetch_track_handler(fetch_handler);

    qbridge_client_disconnect(client);
    qbridge_client_destroy(client);

    printf("Fetch example shut down complete.\n");
    return 0;
}
