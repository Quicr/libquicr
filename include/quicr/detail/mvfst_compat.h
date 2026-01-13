// SPDX-FileCopyrightText: Copyright (c) 2024 Cisco Systems
// SPDX-License-Identifier: BSD-2-Clause

/**
 * @file mvfst_compat.h
 * @brief Compatibility header for mvfst/folly integration with spdlog/fmt
 *
 * This header handles conflicts between:
 * - macOS math.h macros (isnan, isfinite, isinf, signbit)
 * - fmt/spdlog template functions with the same names
 * - libc++ <format> header which also uses these functions
 *
 * The issue: macOS math.h defines isnan/isfinite etc as complex macros that
 * expand to ternary expressions. These macros break:
 * 1. fmt's template function definitions
 * 2. libc++'s std::format which calls std::isnan(value)
 * 3. cmath's `using ::isnan` declarations (macros can't be imported)
 *
 * IMPORTANT: On macOS with USE_MVFST, this header should be manually included
 * as the FIRST header in any source file that uses both folly and fmt/spdlog.
 * This ensures <cmath> is included before folly includes <math.h>, which
 * properly sets up std::isnan etc. before the macros are defined.
 */

#pragma once

#if defined(USE_MVFST) && defined(__APPLE__)

// Include cmath FIRST, before any other headers
// This ensures that std::isnan, std::isfinite, etc. are properly declared
// as functions in namespace std before math.h macros potentially get defined
#include <cmath>

// Now undefine any macros that might have been defined by math.h
// (which could have been transitively included)
#ifdef isnan
#undef isnan
#endif
#ifdef isfinite
#undef isfinite
#endif
#ifdef isinf
#undef isinf
#endif
#ifdef signbit
#undef signbit
#endif
#ifdef fpclassify
#undef fpclassify
#endif
#ifdef isnormal
#undef isnormal
#endif

#endif // USE_MVFST && __APPLE__
