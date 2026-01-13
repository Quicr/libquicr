# QClient Pub/Sub with MVFST

This guide shows how to use qclient with **mvfst** as the QUIC stack with **raw QUIC** or **WebTransport** transport options.

---

## Building with MVFST Support

### Prerequisites

mvfst and its dependencies (folly, fizz, proxygen) must be installed system-wide before building libquicr with mvfst support.

#### macOS (Homebrew)

```bash
# Install mvfst and all dependencies
brew install mvfst

# Optional: Install proxygen for WebTransport support
brew install proxygen
```

#### Linux (using getdeps.py)

```bash
# Clone mvfst if not already present
cd dependencies/mvfst

# Install system dependencies (Ubuntu/Debian)
sudo ./build/fbcode_builder/getdeps.py install-system-deps --recursive mvfst

# Build and install mvfst and dependencies
python3 ./build/fbcode_builder/getdeps.py build mvfst --install-prefix=/usr/local

# Optional: Build proxygen for WebTransport support
cd ../proxygen
python3 ./build/fbcode_builder/getdeps.py build proxygen --install-prefix=/usr/local
```

### Build libquicr with mvfst

Once mvfst is installed, build libquicr with the `USE_MVFST` option:

```bash
# Create build directory
mkdir -p build-mvfst && cd build-mvfst

# Configure with mvfst support
cmake -DUSE_MVFST=ON ..

# Build
make -j$(nproc)

# The qclient and qserver binaries will be in:
# build-mvfst/cmd/examples/
```

### Verify mvfst support

```bash
./cmd/examples/qclient --help
# Should show --quic_stack option with mvfst as a valid choice
```

---

## Running the Examples

### 1. Start the Server (qserver)

```bash
# Start the relay/server (uses picoquic by default)
./qserver -b 127.0.0.1 -p 1234 -c ./server-cert.pem -k ./server-key.pem
```

### 2. Publisher Examples

**Raw QUIC with mvfst:**
```bash
./qclient -r moq://localhost:1234 \
    --transport quic \
    --quic_stack mvfst \
    --pub_namespace "my/namespace" \
    --pub_name "my_track"
```

**WebTransport with mvfst:**
```bash
./qclient -r https://localhost:1234 \
    --transport webtransport \
    --quic_stack mvfst \
    --pub_namespace "my/namespace" \
    --pub_name "my_track"
```

**Publisher with clock mode (auto-publish timestamps):**
```bash
./qclient -r moq://localhost:1234 \
    --transport quic \
    --quic_stack mvfst \
    --pub_namespace "chat/room1" \
    --pub_name "messages" \
    --clock
```

### 3. Subscriber Examples

**Raw QUIC with mvfst:**
```bash
./qclient -r moq://localhost:1234 \
    --transport quic \
    --quic_stack mvfst \
    --sub_namespace "my/namespace" \
    --sub_name "my_track"
```

**WebTransport with mvfst:**
```bash
./qclient -r https://localhost:1234 \
    --transport webtransport \
    --quic_stack mvfst \
    --sub_namespace "my/namespace" \
    --sub_name "my_track"
```

**Subscribe with joining fetch (get last N groups):**
```bash
./qclient -r moq://localhost:1234 \
    --transport quic \
    --quic_stack mvfst \
    --sub_namespace "chat/room1" \
    --sub_name "messages" \
    --joining_fetch 5
```

### 4. Fetch Examples

**Fetch specific range:**
```bash
./qclient -r moq://localhost:1234 \
    --transport quic \
    --quic_stack mvfst \
    --fetch_namespace "my/namespace" \
    --fetch_name "my_track" \
    --start_group 0 \
    --end_group 10 \
    --start_object 0 \
    --end_object 0
```

---

## Full Option Reference

| Option | Description |
|--------|-------------|
| `-r, --url` | Relay URL (default: `moq://localhost:1234`) |
| `-t, --transport` | Transport type: `quic` or `webtransport` |
| `--quic_stack` | QUIC stack: `picoquic` or `mvfst` |
| `--pub_namespace` | Publisher track namespace |
| `--pub_name` | Publisher track name |
| `--sub_namespace` | Subscriber track namespace |
| `--sub_name` | Subscriber track name |
| `--clock` | Auto-publish clock timestamps (publisher) |
| `--use_announce` | Use announce flow instead of publish |
| `--joining_fetch N` | Subscribe with joining fetch (last N groups) |
| `--start_point 0/1` | 0=from beginning, 1=from latest |
| `-d, --debug` | Enable debug logging |
| `-q, --qlog PATH` | Enable qlog output to path |
| `-s, --ssl_keylog` | Enable SSL keylog for debugging |

---

## URL Schemes

- **Raw QUIC**: Use `moq://` or `moqt://` URL scheme
- **WebTransport**: Use `https://` URL scheme

The client automatically converts URL schemes based on the `--transport` option:
- `--transport quic` + `https://` URL -> converts to `moq://`
- `--transport webtransport` + `moq://` URL -> converts to `https://`

---

## Example: Chat Demo (Pub + Sub)

**Terminal 1 - Publisher:**
```bash
./qclient -r moq://localhost:1234 \
    --quic_stack mvfst \
    --pub_namespace "chat/room1" \
    --pub_name "alice"
# Type messages and press Enter to send
```

**Terminal 2 - Subscriber:**
```bash
./qclient -r moq://localhost:1234 \
    --quic_stack mvfst \
    --sub_namespace "chat/room1" \
    --sub_name "alice"
# Messages will appear as received
```

---

## MVFST-Specific Notes

When using `--quic_stack mvfst`, the client will use Facebook's mvfst QUIC implementation instead of picoquic.

### Dependencies

mvfst requires the following libraries:
- **folly** - Facebook's C++ library
- **fizz** - Facebook's TLS 1.3 implementation
- **proxygen** (optional) - For full WebTransport support

### WebTransport Support

- If **proxygen** is installed, full WebTransport support is enabled
- Without proxygen, basic QUIC close/drain is used for WebTransport connections

### Troubleshooting

**CMake can't find mvfst/folly/fizz:**
```bash
# Ensure the packages are installed and pkg-config can find them
pkg-config --modversion libfolly
pkg-config --modversion mvfst

# If using a custom install prefix, set CMAKE_PREFIX_PATH:
cmake -DUSE_MVFST=ON -DCMAKE_PREFIX_PATH=/usr/local ..
```

**Build errors with fmt/spdlog conflicts:**
The build system handles fmt conflicts between spdlog and folly automatically via `mvfst_compat.h`.

---

## Quick Reference: Build Commands

```bash
# Standard build (picoquic only)
mkdir build && cd build
cmake ..
make -j$(nproc)

# Build with mvfst support
mkdir build-mvfst && cd build-mvfst
cmake -DUSE_MVFST=ON ..
make -j$(nproc)

# Build with mvfst and debug symbols
cmake -DUSE_MVFST=ON -DCMAKE_BUILD_TYPE=Debug ..
make -j$(nproc)

# Build with mvfst and custom install prefix
cmake -DUSE_MVFST=ON -DCMAKE_PREFIX_PATH=/opt/mvfst ..
make -j$(nproc)
```
