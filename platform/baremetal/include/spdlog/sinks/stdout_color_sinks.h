/*
 * SPDX-FileCopyrightText: Copyright (c) 2024 Cisco Systems
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Bare-metal stub for spdlog stdout_color_sinks
 */

#ifndef QUICR_BAREMETAL_SPDLOG_STDOUT_COLOR_SINKS_H
#define QUICR_BAREMETAL_SPDLOG_STDOUT_COLOR_SINKS_H

#include "../spdlog.h"

namespace spdlog {
namespace sinks {

class stdout_color_sink_mt {};
class stdout_color_sink_st {};
class stderr_color_sink_mt {};
class stderr_color_sink_st {};

} // namespace sinks

/* Note: stderr_color_mt and stdout_color_mt are defined as functions
 * in spdlog.h that return std::shared_ptr<logger> */

} // namespace spdlog

#endif /* QUICR_BAREMETAL_SPDLOG_STDOUT_COLOR_SINKS_H */
