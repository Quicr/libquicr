#pragma once

#include <string>
#include <sstream>
#include "encode.h"
#include "quicr/name.h"
#include "message_buffer.h"


/*
 * Methods to perform quicr group/object gap checks on received and transmitted objects
 */
namespace quicr {
    /**
     * @brief constructs a string to log based on namespace group/object id gaps
     *
     * @param [in]      is_tx               True if TX, False if RX
     * @param [in]      name                Name to check
     * @param [in,out]  last_group_id       Previous group id from last call
     * @param [in, out] last_object_id      Previous object id from last call
     *
     * @returns string messages if there is a gap. Empty message indicates no gap
     */
    std::string gap_check(bool is_tx,
                          const Name& name,
                          uint64_t& last_group_id,
                          uint64_t& last_object_id) {

        uint64_t group_id = name.bits<uint64_t>(16, 32);
        uint64_t object_id = name.bits<uint64_t>(0, 16);

        std::ostringstream log_s;

        if (last_group_id != 0 && last_object_id != 0) {

            if (group_id - last_group_id > 1) {
                log_s << (is_tx ? "TX " : "RX ")
                      << "Group gap for name: " << name << " " << group_id
                      << " - " << last_group_id << " = "
                      << (group_id - last_group_id) - 1;

            } else if (group_id == last_group_id && object_id - last_object_id > 1) {
                log_s << (is_tx ? "TX " : "RX ")
                      << "Object gap for name: " << name << " "
                      << object_id << " - " << last_object_id
                      << " = " << (object_id - last_object_id) - 1;
            }
        }

        last_group_id = group_id;
        last_object_id = object_id;

        return log_s.str();
    }

} // namespace quicr
