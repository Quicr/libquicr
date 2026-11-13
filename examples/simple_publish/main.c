// SPDX-FileCopyrightText: Copyright (c) 2026 Cisco Systems
// SPDX-License-Identifier: BSD-2-Clause

#include <quicr/quicr.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <windows.h>
#else
#include <time.h>
#endif

static void
sleep_ms(unsigned int milliseconds)
{
#ifdef _WIN32
    Sleep(milliseconds);
#else
    const struct timespec duration = {
        .tv_sec = milliseconds / 1000,
        .tv_nsec = (long)(milliseconds % 1000) * 1000000L,
    };
    nanosleep(&duration, NULL);
#endif
}

static quicr_error_reply_t
setup_received(quicr_session_t* session, const quicr_setup_attributes_t* attributes, void* user_data)
{
    (void)session;
    (void)user_data;

    const quicr_byte_span_t endpoint_id = attributes->endpoint_id;
    fprintf(stderr, "Setup received from '");
    fwrite(endpoint_id.data, sizeof(*endpoint_id.data), endpoint_id.size, stderr);
    fprintf(stderr, "'\n");

    return (quicr_error_reply_t){
        .result = QUICR_REPLY_RESULT_ACCEPT,
    };
}

int
main()
{
    int result = EXIT_FAILURE;
    bool handler_added = false;
    quicr_session_manager_t* session_manager = NULL;
    quicr_session_t* session = NULL;
    quicr_track_namespace_t* track_namespace = NULL;
    quicr_full_track_name_t* full_track_name = NULL;
    quicr_publish_track_handler_t* handler = NULL;

    quicr_client_config_t config;
    quicr_client_config_init(&config);
    config.transport_config.time_queue_max_duration = 5000;
    config.transport_config.tls_cert_filename = "";
    config.transport_config.tls_key_filename = "";

    char relay_uri[256];
    fprintf(stderr, "Enter relay URI (prefixed with either moq:// or moqt://): ");
    if (fgets(relay_uri, sizeof(relay_uri), stdin) == NULL) {
        fprintf(stderr, "Failed to input relay URI\n");
        goto cleanup;
    }

    relay_uri[strcspn(relay_uri, "\r\n")] = '\0';
    config.connect_uri = relay_uri;

    if (quicr_session_manager_create(NULL, &session_manager) != QUICR_STATUS_OK) {
        goto cleanup;
    }

    quicr_session_client_callbacks_t callbacks = {
        .setup_received = setup_received,
    };
    if (quicr_session_manager_add_client(session_manager, &config, &callbacks, &session) != QUICR_STATUS_OK) {
        goto cleanup;
    }

    quicr_session_status_t status;
    if (quicr_session_get_status(session, &status) != QUICR_STATUS_OK) {
        goto cleanup;
    }

    while (status != QUICR_SESSION_STATUS_READY) {
        if (quicr_session_get_status(session, &status) != QUICR_STATUS_OK) {
            goto cleanup;
        }

        if (status == QUICR_SESSION_STATUS_NOT_READY || status == QUICR_SESSION_STATUS_INTERNAL_ERROR ||
            status == QUICR_SESSION_STATUS_INVALID_PARAMS || status == QUICR_SESSION_STATUS_NOT_CONNECTED ||
            status == QUICR_SESSION_STATUS_FAILED_TO_CONNECT) {
            fprintf(stderr, "Failed to connect\n");
            goto cleanup;
        }

        sleep_ms(20);
    }

    char namespace_input[1024];
    fprintf(stderr, "Enter track namespace entries delimited by / (e.g. test/track): ");
    if (!fgets(namespace_input, sizeof(namespace_input), stdin)) {
        goto cleanup;
    }

    namespace_input[strcspn(namespace_input, "\r\n")] = '\0';

    quicr_byte_span_t entries[32];
    size_t entry_count = 0;

    for (char* entry = namespace_input;;) {
        char* separator = strchr(entry, '/');
        if (entry_count == 32) {
            fprintf(stderr, "Too many namespace entries\n");
            goto cleanup;
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

    if (quicr_track_namespace_create(entries, entry_count, &track_namespace) != QUICR_STATUS_OK) {
        goto cleanup;
    }

    char track_name_input[1024];
    fprintf(stderr, "Enter a name for the track: ");
    if (!fgets(track_name_input, sizeof(track_name_input), stdin)) {
        goto cleanup;
    }

    track_name_input[strcspn(track_name_input, "\r\n")] = '\0';
    if (quicr_full_track_name_create(track_namespace,
                                     (quicr_byte_span_t){
                                       .data = (const uint8_t*)track_name_input,
                                       .size = strlen(track_name_input),
                                     },
                                     &full_track_name) != QUICR_STATUS_OK) {
        goto cleanup;
    }

    if (quicr_publish_track_handler_create(
          NULL, full_track_name, QUICR_TRACK_MODE_STREAM, 127, 127, 5000, NULL, &handler) != QUICR_STATUS_OK) {
        goto cleanup;
    }

    if (quicr_session_manager_add_handler(session_manager, session, handler) != QUICR_STATUS_OK) {
        goto cleanup;
    }
    handler_added = true;

    while (!quicr_publish_track_handler_can_publish(handler)) {
        quicr_publish_track_handler_status_t status;
        if (quicr_track_handler_get_status(handler, &status) != QUICR_STATUS_OK) {
            goto cleanup;
        }

        if (status != QUICR_PUBLISH_TRACK_HANDLER_STATUS_OK &&
            status != QUICR_PUBLISH_TRACK_HANDLER_STATUS_PENDING_PUBLISH_OK &&
            status != QUICR_PUBLISH_TRACK_HANDLER_STATUS_PAUSED &&
            status != QUICR_PUBLISH_TRACK_HANDLER_STATUS_NO_SUBSCRIBERS) {
            fprintf(stderr, "Publisher failed to become ready (status=%d)\n", status);
            goto cleanup;
        }

        sleep_ms(20);
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

        if (quicr_publish_track_handler_publish_object(handler, &headers, data, NULL, NULL) != QUICR_STATUS_OK) {
            goto cleanup;
        }

        headers.group_id++;
    }

    fprintf(stderr, "Done publishing! Goodbye\n");
    result = EXIT_SUCCESS;

cleanup:
    if (handler_added && quicr_session_manager_remove_handler(session_manager, session, handler) != QUICR_STATUS_OK) {
        result = EXIT_FAILURE;
        quicr_session_manager_destroy(session_manager);
        session_manager = NULL;
    }

    quicr_track_handler_destroy(handler);
    quicr_full_track_name_destroy(full_track_name);
    quicr_track_namespace_destroy(track_namespace);
    quicr_session_manager_destroy(session_manager);

    return result;
}
