// SPDX-FileCopyrightText: Copyright (c) 2026 Cisco Systems
// SPDX-License-Identifier: BSD-2-Clause

#include <quicr/quicr.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(f)                                                                                                       \
    do {                                                                                                               \
        quicr_status_t retval = (f);                                                                                   \
        if (retval != QUICR_STATUS_OK) {                                                                               \
            fprintf(stderr, "Quicr function call failed (retval=%d)", retval);                                         \
            return (int)retval;                                                                                        \
        }                                                                                                              \
    } while (0)

int
main()
{
    quicr_client_config_t config;
    quicr_client_config_init(&config);
    config.transport_config.time_queue_max_duration = 5000;
    config.transport_config.tls_cert_filename = "";
    config.transport_config.tls_key_filename = "";

    char relay_uri[256];
    fprintf(stderr, "Enter relay URI (prefixed with either moq:// or moqt://): ");
    if (fgets(relay_uri, sizeof(relay_uri), stdin) == NULL) {
        fprintf(stderr, "Failed to input relay URI\n");

        return EXIT_FAILURE;
    }

    config.connect_uri = relay_uri;

    quicr_session_manager_t* session_manager = NULL;
    if (quicr_session_manager_create(NULL, &session_manager) != QUICR_STATUS_OK) {
        return EXIT_FAILURE;
    }

    quicr_session_t* session = NULL;
    CHECK(quicr_session_manager_add_client(session_manager, &config, NULL, &session));

    quicr_session_status_t status;
    CHECK(quicr_session_get_status(session, &status));

    while (status != QUICR_SESSION_STATUS_READY) {
        CHECK(quicr_session_get_status(session, &status));
        if (status == QUICR_SESSION_STATUS_NOT_READY || status == QUICR_SESSION_STATUS_INTERNAL_ERROR ||
            status == QUICR_SESSION_STATUS_INVALID_PARAMS || status == QUICR_SESSION_STATUS_NOT_CONNECTED ||
            status == QUICR_SESSION_STATUS_FAILED_TO_CONNECT) {
            fprintf(stderr, "Failed to connect\n");
            return EXIT_FAILURE;
        }

        // TODO: Sleep cross-platform 20ms
    }

    char namespace_input[1024];
    fprintf(stderr, "Enter track namespace entries delimited by / (e.g. test/track): ");
    if (!fgets(namespace_input, sizeof(namespace_input), stdin)) {
        return EXIT_FAILURE;
    }

    namespace_input[strcspn(namespace_input, "\r\n")] = '\0';

    quicr_byte_span_t entries[32];
    size_t entry_count = 0;

    for (char* entry = namespace_input;;) {
        char* separator = strchr(entry, '/');
        if (entry_count == 32) {
            fprintf(stderr, "Too many namespace entries\n");
            return EXIT_FAILURE;
        }
        entries[entry_count++] = (quicr_byte_span_t){
            .data = (const uint8_t*)entry,
            .size = separator ? (size_t)(separator - entry) : strlen(entry),
        };
        if (!separator) {
            break;
        }
        entry = separator + 1;
    }

    quicr_track_namespace_t track_namespace = {
        .entries = entries,
        .count = entry_count,
    };

    char track_name_input[1024];
    fprintf(stderr, "Enter a name for the track: ");
    if (!fgets(track_name_input, sizeof(track_name_input), stdin)) {
        return EXIT_FAILURE;
    }

    track_name_input[strcspn(track_name_input, "\r\n")] = '\0';
    quicr_full_track_name_t full_track_name = {
        .ns = track_namespace,
        .track_name = {
            .data = (const uint8_t*)track_name_input,
            .size = strlen(track_name_input),
        },
    };

    quicr_publish_track_handler_t* handler = NULL;
    CHECK(
      quicr_publish_track_handler_create(&full_track_name, QUICR_TRACK_MODE_STREAM, 127, 127, 5000, NULL, &handler));

    if (!handler) {
        fprintf(stderr, "Failed to create handler\n");
        return EXIT_FAILURE;
    }

    CHECK(quicr_session_manager_add_publish_track_handler(session_manager, session, handler));

    while (!quicr_publish_track_handler_can_publish(handler)) {
        quicr_publish_track_handler_status_t status;
        CHECK(quicr_publish_track_handler_get_status(handler, &status));

        if (status != QUICR_PUBLISH_TRACK_HANDLER_STATUS_OK &&
            status != QUICR_PUBLISH_TRACK_HANDLER_STATUS_PENDING_PUBLISH_OK &&
            status != QUICR_PUBLISH_TRACK_HANDLER_STATUS_PAUSED &&
            status != QUICR_PUBLISH_TRACK_HANDLER_STATUS_NO_SUBSCRIBERS) {
            fprintf(stderr, "Publisher failed to become ready (status=%d)\n", status);
            return EXIT_FAILURE;
        }

        // TODO: Sleep cross-platform 20ms
    }

    char text[4096];
    quicr_object_headers_t headers;
    quicr_object_headers_init(&headers);
    for (;;) {
        fprintf(stderr, "Enter text (/quit to exit): ");
        if (!fgets(text, sizeof(text), stdin)) {
            fprintf(stderr, "\n");
            break;
        }
        text[strcspn(text, "\r\n")] = '\0';
        if (strcmp(text, "/quit") == 0) {
            fprintf(stderr, "\n");
            break;
        }
        quicr_byte_span_t data = {
            .data = (const uint8_t*)text,
            .size = strlen(text),
        };
        headers.payload_length = data.size;
        headers.has_priority = true;
        headers.priority = 127;
        headers.status = QUICR_OBJECT_STATUS_AVAILABLE;

        CHECK(quicr_publish_track_handler_publish_object(handler, &headers, data, NULL, NULL));

        headers.group_id++;
    }

    quicr_publish_track_handler_destroy(handler);
    quicr_session_manager_destroy(session_manager);

    fprintf(stderr, "Done publishing! Goodbye\n");

    return EXIT_SUCCESS;
}
