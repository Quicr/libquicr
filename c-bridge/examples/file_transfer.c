// SPDX-FileCopyrightText: Copyright (c) 2024 Cisco Systems
// SPDX-License-Identifier: BSD-2-Clause

/*
 * File Transfer Example using QuicR Bridge
 *
 * This example demonstrates how to send or receive files using QuicR.
 * Files are split into chunks and transmitted as objects.
 *
 * Sender mode: Reads a file and publishes it in chunks
 * Receiver mode: Subscribes to receive file chunks and reconstructs the file
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <sys/stat.h>
#include <time.h>

#include "quicr/quicr_bridge.h"

#define CHUNK_SIZE 1024  // 1KB chunks
#define TRANSFER_TIMEOUT_SECONDS 10  // Timeout if no chunks received for this long

// Metadata packet types
#define METADATA_TYPE_FILE_INFO 0x01
#define METADATA_TYPE_END_OF_TRANSFER 0x02

static volatile int keep_running = 1;
static volatile int can_send_data = 0;
static volatile int transfer_complete = 0;

// Metadata structure for file information (sent in first chunk)
typedef struct {
    uint8_t type;           // METADATA_TYPE_FILE_INFO
    uint64_t file_size;     // Total file size in bytes
    uint64_t total_chunks;  // Total number of chunks
} __attribute__((packed)) file_metadata_t;

// End of transfer marker structure
typedef struct {
    uint8_t type;           // METADATA_TYPE_END_OF_TRANSFER
    uint64_t total_chunks;  // Total chunks sent (for verification)
    uint64_t total_bytes;   // Total bytes sent (for verification)
} __attribute__((packed)) end_metadata_t;

typedef struct {
    FILE* fp;
    uint64_t total_bytes;
    uint64_t total_chunks;
    uint64_t chunks_received;
    uint64_t expected_file_size;
    time_t last_chunk_time;
    int metadata_received;
} receiver_state_t;

void signal_handler(int signum) {
    printf("\nReceived signal %d, shutting down...\n", signum);
    keep_running = 0;
}

void status_callback(qbridge_connection_status_t status, void* user_data) {
    printf("Client status changed: %s\n", qbridge_status_to_string(status));
}

void object_published_callback(qbridge_group_id_t group_id,
                               qbridge_object_id_t object_id,
                               qbridge_result_t result,
                               void* user_data) {
    if (result == QBRIDGE_OK) {
        printf("Sent chunk %llu of group %llu\n", object_id, group_id);
    } else {
        printf("Failed to send chunk: %s\n", qbridge_result_to_string(result));
    }
}

void publish_status_callback(qbridge_publish_status_t status,
                              bool can_publish,
                              void* user_data) {
    can_send_data = can_publish;

    switch (status) {
        case QBRIDGE_PUBLISH_STATUS_OK:
            printf("Ready to send file\n");
            break;
        case QBRIDGE_PUBLISH_STATUS_NO_SUBSCRIBERS:
            printf("Waiting for receivers...\n");
            break;
        case QBRIDGE_PUBLISH_STATUS_PAUSED:
            printf("Transfer paused\n");
            break;
        default:
            break;
    }
}

void object_received_callback(const qbridge_object_t* object, void* user_data) {
    receiver_state_t* state = (receiver_state_t*)user_data;

    if (!object || !object->payload.data || !state) {
        return;
    }

    // Update last chunk time
    state->last_chunk_time = time(NULL);

    // Check if this is a metadata packet
    if (object->payload.length >= 1) {
        uint8_t packet_type = ((uint8_t*)object->payload.data)[0];

        // Handle file metadata (first packet)
        if (packet_type == METADATA_TYPE_FILE_INFO && object->payload.length == sizeof(file_metadata_t)) {
            file_metadata_t* metadata = (file_metadata_t*)object->payload.data;
            state->expected_file_size = metadata->file_size;
            state->total_chunks = metadata->total_chunks;
            state->metadata_received = 1;

            printf("Received file metadata:\n");
            printf("  File size: %llu bytes\n", metadata->file_size);
            printf("  Total chunks: %llu\n", metadata->total_chunks);
            printf("\n");
            return;
        }

        // Handle end of transfer marker
        if (packet_type == METADATA_TYPE_END_OF_TRANSFER && object->payload.length == sizeof(end_metadata_t)) {
            end_metadata_t* end_marker = (end_metadata_t*)object->payload.data;
            printf("\nReceived end of transfer marker\n");
            printf("  Total chunks: %llu (received: %llu)\n", end_marker->total_chunks, state->chunks_received);
            printf("  Total bytes: %llu (received: %llu)\n", end_marker->total_bytes, state->total_bytes);

            // Verify the counts match
            if (end_marker->total_chunks == state->chunks_received &&
                end_marker->total_bytes == state->total_bytes) {
                printf("Transfer verification: OK\n");
            } else {
                printf("Transfer verification: MISMATCH\n");
            }

            transfer_complete = 1;
            return;
        }
    }

    // Regular data chunk - write to file
    if (!state->fp) {
        return;
    }

    size_t written = fwrite(object->payload.data, 1, object->payload.length, state->fp);
    if (written != object->payload.length) {
        printf("Error writing to file\n");
        return;
    }

    state->chunks_received++;
    state->total_bytes += object->payload.length;

    // Show progress
    if (state->total_chunks > 0) {
        printf("Received chunk %llu/%llu (%llu bytes, total: %llu bytes)\n",
               state->chunks_received,
               state->total_chunks,
               (uint64_t)object->payload.length,
               state->total_bytes);
    } else {
        printf("Received chunk %llu (%llu bytes, total: %llu bytes)\n",
               state->chunks_received,
               (uint64_t)object->payload.length,
               state->total_bytes);
    }

    // Check if transfer is complete based on metadata
    if (state->metadata_received && state->total_chunks > 0 &&
        state->chunks_received >= state->total_chunks) {
        printf("\nAll chunks received (waiting for end marker)...\n");
    }
}

void print_usage(const char* program_name) {
    printf("Usage: %s [OPTIONS]\n", program_name);
    printf("QuicR Bridge File Transfer Example\n\n");
    printf("Options:\n");
    printf("  -h, --help              Show this help message\n");
    printf("  -s, --server HOSTNAME   Server hostname (default: 127.0.0.1)\n");
    printf("  -p, --port PORT         Server port (default: 33435)\n");
    printf("  -n, --namespace NS      Namespace (default: example/file)\n");
    printf("  -t, --track TRACK       Track name (default: transfer)\n");
    printf("  -a, --announce          Use announce flow instead of publish flow\n");
    printf("\n");
    printf("Sender mode:\n");
    printf("  --send FILE             Send the specified file\n");
    printf("\n");
    printf("Receiver mode:\n");
    printf("  --receive FILE          Receive file and save to specified path\n");
    printf("  --chunks NUM            Expected number of chunks (optional, for early progress tracking)\n");
    printf("\nExample:\n");
    printf("  Sender:   %s --send myfile.txt --announce\n", program_name);
    printf("  Receiver: %s --receive received.txt\n", program_name);
}

int send_file(qbridge_client_t* client,
              const char* namespace_str,
              const char* track_name_str,
              const char* filename,
              int use_announce) {

    // Get file size
    struct stat st;
    if (stat(filename, &st) != 0) {
        printf("Error: Cannot access file %s\n", filename);
        return 1;
    }

    FILE* fp = fopen(filename, "rb");
    if (!fp) {
        printf("Error: Cannot open file %s\n", filename);
        return 1;
    }

    uint64_t file_size = st.st_size;
    uint64_t total_chunks = (file_size + CHUNK_SIZE - 1) / CHUNK_SIZE;

    printf("Sending file: %s\n", filename);
    printf("File size: %llu bytes\n", file_size);
    printf("Total chunks: %llu\n\n", total_chunks);

    // Create namespace
    qbridge_namespace_t ns;
    qbridge_result_t result = qbridge_namespace_from_string(&ns, namespace_str);
    if (result != QBRIDGE_OK) {
        fclose(fp);
        return 1;
    }

    if (use_announce) {
        qbridge_client_publish_namespace(client, &ns);
    }

    // Create publish track
    qbridge_publish_track_config_t pub_config;
    qbridge_publish_track_config_init(&pub_config);

    result = qbridge_full_track_name_from_strings(&pub_config.full_track_name,
                                                       namespace_str, track_name_str);
    if (result != QBRIDGE_OK) {
        fclose(fp);
        return 1;
    }

    pub_config.use_announce = use_announce;
    pub_config.delivery_mode = QBRIDGE_DELIVERY_MODE_DATAGRAM;
    pub_config.default_priority = QBRIDGE_PRIORITY_HIGH;
    pub_config.default_ttl_ms = 10000;
    pub_config.default_cacheable = true;

    qbridge_publish_track_handler_t* publish_handler =
        qbridge_create_publish_track_handler_with_status(&pub_config,
                                                               object_published_callback,
                                                               publish_status_callback,
                                                               NULL);

    if (!publish_handler) {
        fclose(fp);
        return 1;
    }

    result = qbridge_client_publish_track(client, publish_handler);
    if (result != QBRIDGE_OK) {
        fclose(fp);
        qbridge_destroy_publish_track_handler(publish_handler);
        return 1;
    }

    // Wait for subscribers
    printf("Waiting for subscribers...\n");
    while (keep_running && !can_send_data) {
        usleep(100000);
    }

    if (!keep_running) {
        fclose(fp);
        qbridge_client_unpublish_track(client, publish_handler);
        qbridge_destroy_publish_track_handler(publish_handler);
        return 0;
    }

    printf("Starting file transfer...\n\n");

    // Send metadata first
    file_metadata_t metadata = {
        .type = METADATA_TYPE_FILE_INFO,
        .file_size = file_size,
        .total_chunks = total_chunks
    };

    qbridge_object_headers_t meta_headers = {
        .group_id = 0,
        .subgroup_id = 0,
        .object_id = 0,
        .priority = QBRIDGE_PRIORITY_HIGH,
        .ttl_ms = 10000,
        .cacheable = true
    };

    result = qbridge_publish_object_with_headers(publish_handler,
                                                      &meta_headers,
                                                      (uint8_t*)&metadata,
                                                      sizeof(metadata));

    if (result != QBRIDGE_OK) {
        printf("Failed to send metadata: %s\n", qbridge_result_to_string(result));
        fclose(fp);
        qbridge_client_unpublish_track(client, publish_handler);
        qbridge_destroy_publish_track_handler(publish_handler);
        return 1;
    }

    printf("Sent file metadata\n\n");

    // Send file in chunks
    uint8_t buffer[CHUNK_SIZE];
    uint64_t group_id = 0;
    uint64_t object_id = 1;  // Start from 1 since 0 was metadata
    uint64_t bytes_sent = 0;
    uint64_t chunks_sent = 0;

    while (keep_running && !feof(fp)) {
        if (!can_send_data || !qbridge_publish_track_can_publish(publish_handler)) {
            usleep(10000);
            continue;
        }

        size_t bytes_read = fread(buffer, 1, CHUNK_SIZE, fp);
        if (bytes_read == 0) {
            break;
        }

        qbridge_object_headers_t headers = {
            .group_id = group_id,
            .subgroup_id = 0,
            .object_id = object_id,
            .priority = QBRIDGE_PRIORITY_HIGH,
            .ttl_ms = 10000,
            .cacheable = true
        };

        result = qbridge_publish_object_with_headers(publish_handler,
                                                          &headers,
                                                          buffer,
                                                          bytes_read);

        if (result == QBRIDGE_OK) {
            bytes_sent += bytes_read;
            chunks_sent++;
            object_id++;

            // Progress update every 10 chunks
            if (chunks_sent % 10 == 0) {
                printf("Progress: %llu/%llu chunks (%llu/%llu bytes, %.1f%%)\n",
                       chunks_sent, total_chunks,
                       bytes_sent, file_size,
                       (bytes_sent * 100.0) / file_size);
            }

            // Start new group every 1000 objects
            if (object_id >= 1000) {
                group_id++;
                object_id = 0;
            }
        }

        // Small delay between chunks
        usleep(1000);
    }

    printf("\n");
    if (bytes_sent == file_size) {
        printf("File transfer complete! Sent %llu bytes in %llu chunks\n",
               bytes_sent, chunks_sent);
    } else {
        printf("File transfer incomplete: sent %llu/%llu bytes\n", bytes_sent, file_size);
    }

    // Send end of transfer marker
    printf("Sending end of transfer marker...\n");
    end_metadata_t end_marker = {
        .type = METADATA_TYPE_END_OF_TRANSFER,
        .total_chunks = chunks_sent,
        .total_bytes = bytes_sent
    };

    qbridge_object_headers_t end_headers = {
        .group_id = group_id,
        .subgroup_id = 0,
        .object_id = object_id,
        .priority = QBRIDGE_PRIORITY_HIGH,
        .ttl_ms = 10000,
        .cacheable = true
    };

    result = qbridge_publish_object_with_headers(publish_handler,
                                                      &end_headers,
                                                      (uint8_t*)&end_marker,
                                                      sizeof(end_marker));

    if (result == QBRIDGE_OK) {
        printf("End of transfer marker sent\n");
    } else {
        printf("Failed to send end marker: %s\n", qbridge_result_to_string(result));
    }

    // Give time for the end marker to be transmitted
    sleep(1);

    fclose(fp);

    // Cleanup
    qbridge_client_unpublish_track(client, publish_handler);
    if (use_announce) {
        qbridge_client_unpublish_namespace(client, &ns);
    }
    qbridge_destroy_publish_track_handler(publish_handler);

    return 0;
}

int receive_file(qbridge_client_t* client,
                 const char* namespace_str,
                 const char* track_name_str,
                 const char* filename,
                 uint64_t expected_chunks) {

    FILE* fp = fopen(filename, "wb");
    if (!fp) {
        printf("Error: Cannot create file %s\n", filename);
        return 1;
    }

    printf("Receiving file to: %s\n", filename);
    if (expected_chunks > 0) {
        printf("Expected chunks: %llu (this is optional, sender will provide actual count)\n", expected_chunks);
    }
    printf("\n");

    // Initialize receiver state
    receiver_state_t state = {
        .fp = fp,
        .total_bytes = 0,
        .total_chunks = expected_chunks,
        .chunks_received = 0,
        .expected_file_size = 0,
        .last_chunk_time = time(NULL),
        .metadata_received = 0
    };

    // Create subscribe track
    qbridge_subscribe_track_config_t sub_config;
    qbridge_subscribe_track_config_init(&sub_config);

    qbridge_result_t result = qbridge_full_track_name_from_strings(
        &sub_config.full_track_name, namespace_str, track_name_str);

    if (result != QBRIDGE_OK) {
        fclose(fp);
        return 1;
    }

    sub_config.priority = QBRIDGE_PRIORITY_HIGH;

    qbridge_subscribe_track_handler_t* subscribe_handler =
        qbridge_create_subscribe_track_handler(&sub_config,
                                                     object_received_callback,
                                                     &state);

    if (!subscribe_handler) {
        fclose(fp);
        return 1;
    }

    result = qbridge_client_subscribe_track(client, subscribe_handler);
    if (result != QBRIDGE_OK) {
        fclose(fp);
        qbridge_destroy_subscribe_track_handler(subscribe_handler);
        return 1;
    }

    printf("Waiting for file transfer...\n\n");

    // Wait for transfer to complete with timeout detection
    while (keep_running && !transfer_complete) {
        sleep(1);

        // Check for timeout (no chunks received for TRANSFER_TIMEOUT_SECONDS)
        time_t now = time(NULL);
        time_t elapsed = now - state.last_chunk_time;

        if (state.chunks_received > 0 && elapsed > TRANSFER_TIMEOUT_SECONDS) {
            printf("\nTransfer timeout: No chunks received for %d seconds\n", TRANSFER_TIMEOUT_SECONDS);
            printf("This may indicate the transfer is complete but no end marker was received.\n");
            break;
        }

        // Show waiting message periodically
        if (state.chunks_received == 0 && elapsed > 5 && (elapsed % 5) == 0) {
            printf("Still waiting for data... (%ld seconds)\n", elapsed);
        }
    }

    // Cleanup
    qbridge_client_unsubscribe_track(client, subscribe_handler);
    qbridge_destroy_subscribe_track_handler(subscribe_handler);
    fclose(fp);

    printf("Received %llu chunks, %llu total bytes\n",
           state.chunks_received, state.total_bytes);
    printf("File saved to: %s\n", filename);

    return 0;
}

int main(int argc, char* argv[]) {
    printf("QuicR Bridge File Transfer Example\n\n");

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

    const char* namespace_str = "example/file";
    const char* track_name_str = "transfer";
    const char* filename = NULL;
    int mode = 0;  // 0 = none, 1 = send, 2 = receive
    int use_announce = 0;
    uint64_t expected_chunks = 0;

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
        } else if (strcmp(argv[i], "--send") == 0 && i + 1 < argc) {
            filename = argv[++i];
            mode = 1;
        } else if (strcmp(argv[i], "--receive") == 0 && i + 1 < argc) {
            filename = argv[++i];
            mode = 2;
        } else if (strcmp(argv[i], "--chunks") == 0 && i + 1 < argc) {
            expected_chunks = (uint64_t)atoll(argv[++i]);
        } else if (strcmp(argv[i], "-a") == 0 || strcmp(argv[i], "--announce") == 0) {
            use_announce = 1;
        }
    }

    if (mode == 0 || !filename) {
        printf("Error: Must specify either --send or --receive with a filename\n\n");
        print_usage(argv[0]);
        return 1;
    }

    config.debug_logs = false;
    printf("Connecting to %s:%d\n", config.server_hostname, config.server_port);

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

    printf("Connected!\n\n");

    // Execute send or receive
    int ret;
    if (mode == 1) {
        ret = send_file(client, namespace_str, track_name_str, filename, use_announce);
    } else {
        ret = receive_file(client, namespace_str, track_name_str, filename, expected_chunks);
    }

    // Cleanup
    qbridge_client_disconnect(client);
    qbridge_client_destroy(client);

    printf("\nFile transfer example shut down complete.\n");
    return ret;
}
