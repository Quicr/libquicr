#ifndef QUICRQ_H
#define QUICRQ_H

#include <stdint.h>
#include <picoquic.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Version number
 * The number is formated as <major>.<minor><letter>
 * The major version will remain at 0 until we have a stable spec that can be standardized.
 * The minor version is updated when the protocol changes
 * Only the letter is updated if the code changes without changing the protocol
 */
#define QUICRQ_VERSION "0.20b"

/* QUICR ALPN and QUICR port
 * For version zero, the ALPN is set to "quicr-h<minor>", where <minor> is
 * the minor component of the version number. That means that binaries implementing
 * different protocol versions will not be compatible, and connections attempts
 * between such binaries will fail, forcing deployments of compatible versions.
 */
#define QUICRQ_ALPN "quicr-h20"
#define QUICRQ_PORT 853

/* QUICR error codes */
#define QUICRQ_ERROR_NO_ERROR 0x00
#define QUICRQ_ERROR_INTERNAL 0x01
#define QUICRQ_ERROR_PROTOCOL 0x02

/* QUICR client return codes
*/
typedef enum {
    quicrq_incoming_query = 0, /* Incoming callback query */
    quicrq_query_cancelled, /* Query cancelled before response provided */
    quicrq_response_complete, /* The last response to the current query arrived. */
    quicrq_response_partial, /* One of the first responses to a query has arrived */
    quicrq_response_cancelled, /* The response to the current query was cancelled by the peer. */
    quicrq_query_failed  /* Query failed for reasons other than cancelled. */
} quicrq_query_return_enum;


/* Connection context management functions.
 * The type quicrq_ctx_t is treated here as an opaque pointer, to
 * provide isolation between the app and the stack.
 */

typedef struct st_quicrq_ctx_t quicrq_ctx_t;
typedef struct st_quicrq_cnx_ctx_t quicrq_cnx_ctx_t;
typedef struct st_quicrq_stream_ctx_t quicrq_stream_ctx_t;

quicrq_ctx_t* quicrq_create_empty();
void quicrq_set_quic(quicrq_ctx_t* qr_ctx, picoquic_quic_t* quic);
quicrq_ctx_t* quicrq_create(char const* alpn,
    char const* cert_file_name, char const* key_file_name, char const* cert_root_file_name,
    char const* ticket_store_file_name, char const* token_store_file_name,
    const uint8_t* ticket_encryption_key, size_t ticket_encryption_key_length,
    uint64_t* simulated_time);
void quicrq_delete(quicrq_ctx_t* ctx);
picoquic_quic_t* quicrq_get_quic_ctx(quicrq_ctx_t* ctx);
void quicrq_init_transport_parameters(picoquic_tp_t* tp, int client_mode);
void quicrq_set_cache_duration(quicrq_ctx_t* qr_ctx, uint64_t cache_duration_max);
uint64_t quicrq_time_check(quicrq_ctx_t* qr_ctx, uint64_t current_time);

quicrq_cnx_ctx_t* quicrq_create_cnx_context(quicrq_ctx_t* qr_ctx, picoquic_cnx_t* cnx);
quicrq_cnx_ctx_t* quicrq_create_client_cnx(quicrq_ctx_t* qr_ctx,
    const char* sni, struct sockaddr* addr);
void quicrq_delete_cnx_context(quicrq_cnx_ctx_t* cnx_ctx);

void quicrq_get_peer_address(quicrq_cnx_ctx_t* cnx_ctx, struct sockaddr_storage* stored_addr);

/* Media stream definition.
 * Media is composed of series of objects, objects have
 * headers and content. Header provides information
 * about sufficient for synchronization and replay.
 */
typedef struct st_quicrq_media_object_header_t {
    uint64_t timestamp; /* time from start of media fragment */
    uint64_t number; /* start at 1 for media fragment */
    size_t length; /* number of content bytes */
} quicrq_media_object_header_t;

/* Media object publisher.
 * The media object publisher is a simpler, object based version of the 
 * media object API described below. It is push based, while the media
 * API is pull based.
 * 
 * The API has three components:
 * - Publish media object source: declare the URL of the source, and 
 *   other characteristics.
 * - Publish media object: a media object is defined as an array of
 *   bytes, identified by an object ID, and decorated with properties
 *   such as priority, use as synchronization point, or being marked
 *   as discardable.
 * - Delete a media object source: free the resource associated with that
 *   source.
 * 
 * The application is expected to publish a series of media objects
 * over time. The objects are added to a cache, and then sent to peers
 * after they subscribe to the media source, or if the media source
 * is posted. The cache management policy is specified upon opening
 * the object.
 * 
 * The API is implemented using the "media publisher" API. It defines
 * generic media_publisher_subscribe_fn and media_publisher_fn.
 */

typedef struct st_quicrq_media_object_source_properties_t {
    int tbd;
} quicrq_media_object_source_properties_t;

typedef struct st_quicrq_media_object_properties_t {
    uint8_t flags;
    int is_new_group;
} quicrq_media_object_properties_t;

typedef struct st_quicrq_media_object_source_ctx_t quicrq_media_object_source_ctx_t;

quicrq_media_object_source_ctx_t* quicrq_publish_object_source(quicrq_ctx_t* qr_ctx, const uint8_t* url, size_t url_length,
    quicrq_media_object_source_properties_t * properties);
int quicrq_object_source_set_start(quicrq_media_object_source_ctx_t* object_source_ctx, uint64_t start_group_id, uint64_t start_object_id);

int quicrq_publish_object(
    quicrq_media_object_source_ctx_t* object_source_ctx,
    uint8_t* object,
    size_t object_length,
    int is_new_group,
    quicrq_media_object_properties_t * properties,
    uint64_t* published_group_id,
    uint64_t* published_object_id);

void quicrq_publish_object_fin(quicrq_media_object_source_ctx_t* object_source_ctx);

void quicrq_delete_object_source(quicrq_media_object_source_ctx_t* object_source_ctx);

/* Management of default sources, used for example by proxies or relays.
 * The callback creates a context for the specified URL, returning the parameters that would be otherwise
 * specified in the function "quicrq_publish_source".
 * When a quicrq context is deleted, the stack calls the default source function one last time, setting
 * all parameters except the default_source_ctx to zero or NULL values. This gives the application
 * an opportunity to clear memory and resource used by the default publication function.
 */

typedef int (*quicrq_default_source_fn)(void * default_source_ctx, quicrq_ctx_t* qr_ctx, const uint8_t* url, const size_t url_length);
void quicrq_set_default_source(quicrq_ctx_t* qr_ctx, quicrq_default_source_fn default_source_fn, void * default_source_ctx);

/* Quic media object consumer.
 * The application sets a "media object consumer function" and a "media object consumer context" for
 * the media stream. On the client side, this is done by a call to "quicrq_subscribe_object_stream"
 * which will trigger the connection to the desired server and the opening of the object stream
 * through the protocol.
 * 
 * The subscribe function returns an opaque subscription context. When the stack wants to close
 * the subscription, it calls the consumer function with action = quicrq_media_close, after which the
 * application shall not reference the subscription context. If the application wants to discard
 * the subscription prior to receiving notice from the stack, it calls the unsubscribe function
 * with that subscription context. The subscription context shall not be used after that.
 */

#define quicrq_consumer_finished 1
#define quicrq_consumer_continue 0
#define quicrq_consumer_error -1

typedef enum {
    quicrq_media_datagram_ready = 0,
    quicrq_media_start_point,
    quicrq_media_final_object_id,
    quicrq_media_close
} quicrq_media_consumer_enum;

typedef struct st_quicrq_object_stream_consumer_properties_t {
    uint8_t flags;
} quicrq_object_stream_consumer_properties_t;

typedef int (*quicrq_object_stream_consumer_fn)(
    quicrq_media_consumer_enum action,
    void* object_consumer_ctx,
    uint64_t current_time,
    uint64_t group_id,
    uint64_t object_id,
    const uint8_t* data,
    size_t data_length,
    quicrq_object_stream_consumer_properties_t* properties);

typedef struct st_quicrq_object_stream_consumer_ctx quicrq_object_stream_consumer_ctx;

quicrq_object_stream_consumer_ctx* quicrq_subscribe_object_stream(quicrq_cnx_ctx_t* cnx_ctx,
    const uint8_t* url, size_t url_length, int use_datagrams, int in_order_required,
    quicrq_object_stream_consumer_fn media_object_consumer_fn, void* media_object_ctx);

void quicrq_unsubscribe_object_stream(quicrq_object_stream_consumer_ctx* subscribe_ctx);

int quicrq_cnx_post_media(quicrq_cnx_ctx_t* cnx_ctx, const uint8_t* url, size_t url_length,
    int use_datagrams);

typedef int (*quicrq_media_consumer_init_fn)(quicrq_stream_ctx_t* stream_ctx, const uint8_t* url, size_t url_length);
int quicrq_set_media_init_callback(quicrq_ctx_t* ctx, quicrq_media_consumer_init_fn media_init_fn);

quicrq_cnx_ctx_t* quicrq_first_connection(quicrq_ctx_t* qr_ctx);
int quicrq_cnx_has_stream(quicrq_cnx_ctx_t* cnx_ctx);
int quicrq_close_cnx(quicrq_cnx_ctx_t* cnx_ctx);
int quicrq_is_cnx_disconnected(quicrq_cnx_ctx_t* cnx_ctx);

int quicrq_callback(picoquic_cnx_t* cnx,
    uint64_t stream_id, uint8_t* bytes, size_t length,
    picoquic_call_back_event_t fin_or_event, void* callback_ctx, void* v_stream_ctx);

/* Handling of extra repeats
 *
 * The "extra repeat" process attempts to limit the extra latency caused by
 * packet losses and retransmissions. It is only applied for media streams
 * sent as datagrams.
 * 
 * The extra repeat is done in two cases:
 * - send an extra copy of a packet after an error correction, i.e., "on nack"
 * - send an extra copy of a packet if it was delayed at a previous hop,
 *   i.e., "after delayed"
 * if desired, the code will schedule a second transmission of the fragment
 * after an "extra delay".
 * 
 * The two modes of repeat can be controlled independently by a call
 * to "quicrq_set_extra_repeat". 
 * 
 * The function "quicrq_set_extra_repeat_delay" lets the application
 * specify that extra delay. The value "10,000 microseconds" is generally
 * adequate. If the value is set to 0, the extra repeat process is disabled.
 * (this is the default.)
 * 
 * When extra repeat is enabled, the application must call the function
 * `quicrq_handle_extra_repeat` at regular intervals. In a multi threaded
 * environment, this must be done inside the "picoquic" network thread,
 * for example when processing the network loop time check callback
 * `picoquic_packet_loop_time_check`. The function returns the time
 * at will the next extra copy should be scheduled, or UINT64_MAX if no
 * such copy is currently planned.
 */

void quicrq_set_extra_repeat(quicrq_ctx_t* qr, int on_nack, int after_delayed);
void quicrq_set_extra_repeat_delay(quicrq_ctx_t* qr, uint64_t delay_in_microseconds);
uint64_t quicrq_handle_extra_repeat(quicrq_ctx_t* qr, uint64_t current_time);

#ifdef __cplusplus
}
#endif

#endif /* QUICRQ_H */
