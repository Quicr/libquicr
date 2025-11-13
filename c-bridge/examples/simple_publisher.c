// SPDX-FileCopyrightText: Copyright (c) 2024 Cisco Systems
// SPDX-License-Identifier: BSD-2-Clause

#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "quicr/quicr_bridge.h"

static volatile int keep_running = 1;
static int publish_clock = 0;          // send one second intervals objects
static volatile int can_send_data = 0; // Track if we can send data (forward state)

void
signal_handler(int signum)
{
    printf("\nReceived signal %d, shutting down...\n", signum);
    keep_running = 0;
}

void
get_time_string(char* buffer, size_t buffer_size)
{
    struct timespec ts;
    struct tm tm_result;

    clock_gettime(CLOCK_REALTIME, &ts);
    localtime_r(&ts.tv_sec, &tm_result);

    snprintf(buffer,
             buffer_size,
             "%04d-%02d-%02d %02d:%02d:%02d.%06ld",
             tm_result.tm_year + 1900,
             tm_result.tm_mon + 1,
             tm_result.tm_mday,
             tm_result.tm_hour,
             tm_result.tm_min,
             tm_result.tm_sec,
             ts.tv_nsec / 1000);
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
    if (result == QBRIDGE_OK) {
        printf("Published object: group=%llu, object=%llu\n", group_id, object_id);
    } else {
        printf("Failed to publish object: group=%llu, object=%llu, error=%s\n",
               group_id,
               object_id,
               qbridge_result_to_string(result));
    }
}

void
publish_status_callback(qbridge_publish_status_t status, bool can_publish, void* user_data)
{
    const char* status_str;
    switch (status) {
        case QBRIDGE_PUBLISH_STATUS_OK:
            status_str = "OK - Ready to publish";
            break;
        case QBRIDGE_PUBLISH_STATUS_NO_SUBSCRIBERS:
            status_str = "No subscribers";
            break;
        case QBRIDGE_PUBLISH_STATUS_SUBSCRIPTION_UPDATED:
            status_str = "Subscription updated";
            break;
        case QBRIDGE_PUBLISH_STATUS_NEW_GROUP_REQUESTED:
            status_str = "New group requested";
            break;
        case QBRIDGE_PUBLISH_STATUS_PAUSED:
            status_str = "Paused - Subscriber requested pause (forward=0)";
            break;
        case QBRIDGE_PUBLISH_STATUS_NOT_ANNOUNCED:
            status_str = "Not announced";
            break;
        case QBRIDGE_PUBLISH_STATUS_PENDING_ANNOUNCE_RESPONSE:
            status_str = "Pending announce response";
            break;
        default:
            status_str = "Unknown status";
            break;
    }

    printf("Publisher status changed: %s (can_publish=%d)\n", status_str, can_publish);

    // Update the global flag that controls whether we send data
    // When paused (forward=0), can_publish will be false
    // When resumed (forward=1), can_publish will be true
    can_send_data = can_publish;
}

void
print_usage(const char* program_name)
{
    printf("Usage: %s [OPTIONS]\n", program_name);
    printf("QuicR Bridge Simple Publisher\n\n");
    printf("Options:\n");
    printf("  -h, --help              Show this help message\n");
    printf("  -s, --server HOSTNAME   Server hostname (default: 127.0.0.1)\n");
    printf("  -p, --port PORT         Server port (default: 33435)\n");
    printf("  -n, --namespace NS      Namespace to publish (default: example/publisher)\n");
    printf("  -t, --track TRACK       Track name (default: video_stream)\n");
    printf("  -c, --clock             Publish current timestamp every second instead of manual messages\n");
    printf("  -a, --announce          Use announce flow instead of publish flow\n");
    printf("\nExample:\n");
    printf("  %s --server 127.0.0.1 --port 33435 --clock\n", program_name);
    printf("  %s --server 127.0.0.1 --port 33435 --announce --clock\n", program_name);
}

int
main(int argc, char* argv[])
{
    printf("Starting QuicR Bridge Simple Publisher\n");

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
    const char* namespace_str = "example/publisher";
    const char* track_name_str = "video_stream";
    int use_announce = 0;

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
        } else if (strcmp(argv[i], "-c") == 0 || strcmp(argv[i], "--clock") == 0) {
            publish_clock = 1;
        } else if (strcmp(argv[i], "-a") == 0 || strcmp(argv[i], "--announce") == 0) {
            use_announce = 1;
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

    // Publish namespace
    qbridge_namespace_t namespace;
    result = qbridge_namespace_from_string(&namespace, namespace_str);
    if (result != QBRIDGE_OK) {
        printf("Failed to create namespace: %s\n", qbridge_result_to_string(result));
        qbridge_client_destroy(client);
        return 1;
    }

    result = qbridge_client_publish_namespace(client, &namespace);
    if (result != QBRIDGE_OK) {
        printf("Failed to publish namespace: %s\n", qbridge_result_to_string(result));
        qbridge_client_destroy(client);
        return 1;
    }

    printf("Published namespace: %s\n", namespace_str);

    // Create publish track handler
    qbridge_publish_track_config_t track_config;
    qbridge_publish_track_config_init(&track_config);

    result = qbridge_full_track_name_from_strings(&track_config.full_track_name, namespace_str, track_name_str);
    if (result != QBRIDGE_OK) {
        printf("Failed to create track name: %s\n", qbridge_result_to_string(result));
        qbridge_client_destroy(client);
        return 1;
    }

    track_config.track_alias = qbridge_compute_track_alias(&track_config.full_track_name);
    printf("Computed track alias: %llu\n", track_config.track_alias);
    track_config.delivery_mode = QBRIDGE_DELIVERY_MODE_DATAGRAM;
    track_config.default_priority = QBRIDGE_PRIORITY_HIGH;
    track_config.default_ttl_ms = 5000;
    track_config.default_cacheable = true;
    track_config.use_announce = use_announce;

    if (use_announce) {
        printf("Using announce flow\n");
    } else {
        printf("Using publish flow\n");
    }

    qbridge_publish_track_handler_t* track_handler =
      qbridge_create_publish_track_handler(&track_config, object_published_callback, publish_status_callback, NULL);

    if (!track_handler) {
        printf("Failed to create publish track handler\n");
        qbridge_client_destroy(client);
        return 1;
    }

    // Publish track
    result = qbridge_client_publish_track(client, track_handler);
    if (result != QBRIDGE_OK) {
        printf("Failed to publish track: %s\n", qbridge_result_to_string(result));
        qbridge_destroy_publish_track_handler(track_handler);
        qbridge_client_destroy(client);
        return 1;
    }

    printf("Published track: %s/%s\n", namespace_str, track_name_str);

    // Start publishing objects
    uint64_t group_id = 0;
    uint64_t object_id = 0;

    if (publish_clock) {
        printf("Starting to publish clock timestamps every second (Ctrl+C to stop)...\n");

        while (keep_running) {
            // Create time string
            char test_data[256];
            get_time_string(test_data, sizeof(test_data));
            printf("Group:%llu Object:%llu, Timestamp:%s\n", group_id, object_id, test_data);

            // Create object headers
            qbridge_object_headers_t headers = { .group_id = group_id,
                                                 .subgroup_id = 0,
                                                 .object_id = object_id,
                                                 .priority = QBRIDGE_PRIORITY_HIGH,
                                                 .ttl_ms = 5000,
                                                 .cacheable = true };

            // Check if we can publish before attempting to publish
            if (!can_send_data) {
                // Skip publishing when paused
            } else if (!qbridge_publish_track_can_publish(track_handler)) {
                // Skip if not ready
            } else {
                // Publish object
                result = qbridge_publish_object_with_headers(
                  track_handler, &headers, (const uint8_t*)test_data, strlen(test_data));

                if (result != QBRIDGE_OK) {
                    printf("Failed to publish object: %s\n", qbridge_result_to_string(result));
                }
            }

            // Increment object ID, occasionally increment group ID
            object_id++;
            if (object_id % 10 == 0) {
                group_id++;
                object_id = 0;
            }

            // Sleep for 1 second
            sleep(1);
        }
    } else {
        printf("Type messages and press Enter to publish (Ctrl+C to stop)...\n");
        printf("Message format: 'Test object data - <your message>'\n\n");

        char input_buffer[1024];
        while (keep_running && fgets(input_buffer, sizeof(input_buffer), stdin) != NULL) {
            // Remove newline
            size_t len = strlen(input_buffer);
            if (len > 0 && input_buffer[len - 1] == '\n') {
                input_buffer[len - 1] = '\0';
                len--;
            }

            if (len == 0) {
                continue;
            }

            // Create test data with user input
            char test_data[1280];
            time_t now = time(NULL);
            snprintf(test_data,
                     sizeof(test_data),
                     "Test object data - %s (timestamp: %ld, group: %llu, object: %llu)",
                     input_buffer,
                     now,
                     group_id,
                     object_id);

            // Create object headers
            qbridge_object_headers_t headers = { .group_id = group_id,
                                                 .subgroup_id = 0,
                                                 .object_id = object_id,
                                                 .priority = QBRIDGE_PRIORITY_HIGH,
                                                 .ttl_ms = 5000,
                                                 .cacheable = true };

            // Check if we can publish before attempting to publish
            if (!can_send_data) {
                printf("Cannot publish: paused state (forward=0)\n");
            } else if (!qbridge_publish_track_can_publish(track_handler)) {
                printf("Cannot publish: not ready\n");
            } else {
                // Publish object
                result = qbridge_publish_object_with_headers(
                  track_handler, &headers, (const uint8_t*)test_data, strlen(test_data));

                if (result != QBRIDGE_OK) {
                    printf("Failed to publish object: %s\n", qbridge_result_to_string(result));
                } else {
                    printf("Published: group=%llu, object=%llu\n", group_id, object_id);
                }
            }

            // Increment object ID, occasionally increment group ID
            object_id++;
            if (object_id % 10 == 0) {
                group_id++;
                object_id = 0;
            }
        }
    }

    printf("Shutting down publisher...\n");

    // Cleanup
    qbridge_client_unpublish_track(client, track_handler);
    qbridge_client_unpublish_namespace(client, &namespace);
    qbridge_destroy_publish_track_handler(track_handler);

    qbridge_client_disconnect(client);
    qbridge_client_destroy(client);

    printf("Publisher shut down complete.\n");
    return 0;
}
