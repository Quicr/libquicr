# QuicR C Bridge

The QuicR C Bridge provides a C API that wraps the C++ QuicR library functionality, 
allowing C applications to use QuicR for media over QUIC transport.

## Architecture

```
C Application
     |
     v
C Bridge API (quicr_bridge.h)
     |
     v
C++ Bridge Implementation (quicr_bridge.cpp)
     |
     v
QuicR C++ Library
```

## Building

### Using CMake

To build the C Bridge:

```bash
# Build everything including C Bridge
cmake -DQUICR_BUILD_C_BRIDGE=ON -B build .
cmake --build build

# Build only C Bridge
make c-bridge-only
```

### Using Makefile

```bash
# Build everything including C Bridge
make

# Build only C Bridge
make c-bridge-only

# Build C Bridge with existing build directory
make c-bridge
```

## API Overview

### Core Types

- **`qbridge_client_t`** - Client connection handle
- **`qbridge_publish_track_handler_t`** - Publisher track handler
- **`qbridge_subscribe_track_handler_t`** - Subscriber track handler
- **`qbridge_fetch_track_handler_t`** - Fetch track handler


## Examples

The `examples/` directory contains complete working examples:

- **`simple_publisher.c`** - Basic publisher that sends test data
- **`simple_subscriber.c`** - Basic subscriber that receives objects
- **`chat.c`** - Interactive chat application demonstrating bidirectional messaging
- **`fetch.c`** - Example of fetching objects on demand
- **`file_transfer.c`** - File transfer application demonstrating data streaming

See [examples/README.md](examples/README.md) for details. for detailed instructions.



## License

SPDX-FileCopyrightText: Copyright (c) 2024 Cisco Systems  
SPDX-License-Identifier: BSD-2-Clause
