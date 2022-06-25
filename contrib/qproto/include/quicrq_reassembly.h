#ifndef QUICRQ_REASSEMBLY_H
#define QUICRQ_REASSEMBLY_H

#include "quicrq.h"
#include "picoquic_utils.h"
#include "picosplay.h"


#ifdef __cplusplus
extern "C" {
#endif

 /* Handling of object reassembly
  * Manage a list of objects being reassembled. The list is organized as a splay,
  * indexed by the object id and object offset. When a new fragment is received
  * the code will check whether the object is already present, and then whether the
  * fragment for that object has already arrived.
  */

typedef struct st_quicrq_reassembly_context_t {
    picosplay_tree_t object_tree;
    uint64_t next_object_id;
    uint64_t final_object_id;
    int is_finished : 1;
} quicrq_reassembly_context_t;

typedef enum {
    quicrq_reassembly_object_in_sequence,
    quicrq_reassembly_object_peek,
    quicrq_reassembly_object_repair
} quicrq_reassembly_object_mode_enum;

typedef int (*quicrq_reassembly_object_ready_fn)(
    void* media_ctx,
    uint64_t current_time,
    uint64_t object_id,
    const uint8_t* data,
    size_t data_length,
    quicrq_reassembly_object_mode_enum object_mode);

/* Submit a received packet for reassembly.
 * For each reassembled object, the function will call ()
 */
int quicrq_reassembly_input(
    quicrq_reassembly_context_t* reassembly_ctx,
    uint64_t current_time,
    const uint8_t* data,
    uint64_t object_id,
    uint64_t offset,
    int is_last_fragment,
    size_t data_length,
    quicrq_reassembly_object_ready_fn ready_fn,
    void * app_media_ctx);

int quicrq_reassembly_learn_start_point(quicrq_reassembly_context_t* reassembly_ctx, uint64_t start_object_id, uint64_t current_time,
    quicrq_reassembly_object_ready_fn ready_fn, void* app_media_ctx);

/* Obtain the final object ID */
int quicrq_reassembly_learn_final_object_id(
    quicrq_reassembly_context_t* reassembly_ctx,
    uint64_t final_object_id);

/* Find the object number of the last reassembled object */
uint64_t quicrq_reassembly_object_id_last(quicrq_reassembly_context_t* reassembly_ctx);

/* Initialize the reassembly context, supposedly zero on input.
 */
void quicrq_reassembly_init(quicrq_reassembly_context_t* reassembly_ctx);

/* Free the reassembly context
 */
void quicrq_reassembly_release(quicrq_reassembly_context_t* reassembly_ctx);

#ifdef __cplusplus
}
#endif

#endif /* QUICRQ_REASSEMBLY_H */