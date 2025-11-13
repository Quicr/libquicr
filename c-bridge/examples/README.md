# QuicR C-Bridge Examples

This directory contains example applications demonstrating how to use the QuicR C-Bridge API.

## Building the Examples

The examples are built automatically when you build the main project:

```bash
cd libquicr
make c-bridge-only
```

The compiled examples will be located in `build/c-bridge/` directory.

## Examples Overview

### 1. Simple Publisher (`simple_publisher`)
Shows how to create a basic publisher that sends timestamped data every second.
**Usage**:
```bash
# Basic usage with clock mode
./build/c-bridge/simple_publisher --clock

# With announce flow
./build/c-bridge/simple_publisher --announce --clock

# Custom server and namespace
./build/c-bridge/simple_publisher --server 192.168.1.100 --port 33435 \
    --namespace my/namespace --track my_track --clock
```

**Options**:
- `-s, --server HOSTNAME`: Server hostname (default: 127.0.0.1)
- `-p, --port PORT`: Server port (default: 33435)
- `-n, --namespace NS`: Namespace to publish (default: example/publisher)
- `-t, --track TRACK`: Track name (default: video_stream)
- `-c, --clock`: Publish timestamp every second
- `-a, --announce`: Use announce flow instead of publish flow


---

### 2. Simple Subscriber (`simple_subscriber`)

Shows how to create a basic subscriber that receives data from publisher 
publishing to a specified namespace and track.

**Usage**:
```bash
# Basic subscription
./build/c-bridge/simple_subscriber --namespace example/publisher --track video_stream

# Subscribe to specific group range
./build/c-bridge/simple_subscriber --namespace example/publisher --track video_stream \
    --start-group 5 --end-group 10

# Subscribe with priority and range
./build/c-bridge/simple_subscriber --namespace example/publisher --track video_stream \
    --priority 4 --start-group 0 --end-group 100 --start-object 0 --end-object 50
```

**Options**:
- `-s, --server HOSTNAME`: Server hostname (default: 127.0.0.1)
- `-p, --port PORT`: Server port (default: 33435)
- `-n, --namespace NS`: Namespace to subscribe (default: example/)
- `-t, --track TRACK`: Track name (default: video_stream)
- `--start-group ID`: Starting group ID (default: 0)
- `--end-group ID`: Ending group ID (default: UINT64_MAX)
- `--start-object ID`: Starting object ID (default: 0)
- `--end-object ID`: Ending object ID (default: UINT64_MAX)
- `--priority LEVEL`: Priority level 0-4 (0=VERY_LOW, 1=LOW, 2=NORMAL, 3=HIGH, 4=VERY_HIGH, default: 3)
- `--group-order ORDER`: Group order (default: 0)

---

### 3. Chat Application (`chat`)

Real-time bidirectional chat application demonstrating simultaneous publishing and subscribing.


**Usage**:
```bash
# Start first user
./build/c-bridge/chat --username Alice --room general

# Start second user in another terminal
./build/c-bridge/chat --username Bob --room general

# With announce flow
./build/c-bridge/chat --username Alice --room general --announce
```

**Options**:
- `-s, --server HOSTNAME`: Server hostname (default: 127.0.0.1)
- `-p, --port PORT`: Server port (default: 33435)
- `-r, --room ROOM`: Chat room name (default: general)
- `-u, --username NAME`: Your username (default: user)
- `-a, --announce`: Use announce flow

**How It Works**:
1. Creates a namespace `chat/{room_name}`
2. Subscribes to track `messages` to receive messages from others
3. Publishes to track `messages` to send your messages
4. Messages are formatted as: `[HH:MM:SS] username: message`

---

### 4. Fetch Example (`fetch`)
Demonstrates fetching historical/cached objects from a specific range.

**Key Features**:
- Fetches objects by group and object ID range
- Useful for retrieving cached content
- Supports partial range fetching

**Usage**:
```bash
# Fetch objects from group 0-5, objects 0-50
./build/c-bridge/fetch --start-group 0 --end-group 5 \
    --start-object 0 --end-object 50

# Fetch from specific namespace/track
./build/c-bridge/fetch --namespace example/data --track archived \
    --start-group 10 --end-group 20 --start-object 0 --end-object 100
```

**Options**:
- `-s, --server HOSTNAME`: Server hostname (default: 127.0.0.1)
- `-p, --port PORT`: Server port (default: 33435)
- `-n, --namespace NS`: Namespace (default: example/fetch)
- `-t, --track TRACK`: Track name (default: data)
- `--start-group ID`: Starting group ID (default: 0)
- `--end-group ID`: Ending group ID (default: 10)
- `--start-object ID`: Starting object ID (default: 0)
- `--end-object ID`: Ending object ID (default: 100)

---

### 5. File Transfer (`file_transfer`)

**Purpose**: Demonstrates reliable file transfer by splitting files into chunks and transmitting them as objects.

**Key Features**:
- Send mode: Reads file and publishes in 1KB chunks
- Receive mode: Subscribes and reconstructs file
- **Automatic metadata exchange**: Sender transmits file size and chunk count
- **End-of-transfer marker**: Explicit completion signal with verification
- **Timeout detection**: Receiver auto-exits if transfer stalls (10 second timeout)
- **No manual chunk counting needed**: Receiver automatically gets correct count
- Progress tracking with accurate percentages
- Handles large files efficiently
- Supports both publish and announce flows

**Sending a File**:
```bash
# Send a file (recommended: use announce flow)
./build/c-bridge/file_transfer --send myfile.txt --announce

# Send without announce
./build/c-bridge/file_transfer --send myfile.txt

# Send to specific namespace
./build/c-bridge/file_transfer --send myfile.pdf \
    --namespace files/documents --track myfile --announce
```

**Receiving a File**:
```bash
# Receive a file (chunks parameter is now optional)
./build/c-bridge/file_transfer --receive output.txt

# Legacy mode: specify expected chunks for early progress display
./build/c-bridge/file_transfer --receive output.txt --chunks 150

# Receive from specific namespace
./build/c-bridge/file_transfer --receive output.pdf \
    --namespace files/documents --track myfile
```

**Options**:
- `-s, --server HOSTNAME`: Server hostname (default: 127.0.0.1)
- `-p, --port PORT`: Server port (default: 33435)
- `-n, --namespace NS`: Namespace (default: example/file)
- `-t, --track TRACK`: Track name (default: transfer)
- `--send FILE`: Send the specified file
- `--receive FILE`: Receive and save to specified path
- `--chunks NUM`: Expected number of chunks (optional, for early progress display)
- `-a, --announce`: Use announce flow (recommended for automatic discovery)

---

## Running Multiple Examples Together

### Scenario 1: Publisher and Subscriber with Announce Flow
```bash
# Terminal 1: Start publisher with announce flow
./build/c-bridge/simple_publisher --announce --clock

# Terminal 2: Start subscriber (discovers namespace via announce)
./build/c-bridge/simple_subscriber --namespace example/publisher
```

### Scenario 2: Chat Room
```bash
# Terminal 1: Alice joins
./build/c-bridge/chat --username Alice --room general

# Terminal 2: Bob joins
./build/c-bridge/chat --username Bob --room general

# Terminal 3: Charlie joins
./build/c-bridge/chat --username Charlie --room general
```

### Scenario 3: File Transfer with Announce
```bash
# Terminal 1: Receiver waits for file (no need to specify chunk count)
./build/c-bridge/file_transfer --receive received.pdf

# Terminal 2: Sender sends with announce flow
./build/c-bridge/file_transfer --send document.pdf --announce
```


### Scenario 5: Publish, Fetch, and Subscribe

Note: The example for subscribing to a later group is not fully 
implemented in libquicr , especiallly Filters Absolute Start and 
Absolute Range filters.

```bash
# Terminal 1: Start publisher to create cached content
./build/c-bridge/simple_publisher --namespace example/fetch \
    --track data --clock --announce

# Terminal 2: Fetch historical data (groups 0-5)
./build/c-bridge/fetch --namespace example/fetch --track data \
    --start-group 0 --end-group 5 --start-object 0 --end-object 100

# Terminal 3: Subscribe to live data (groups 6+)
./build/c-bridge/simple_subscriber --namespace example/fetch \
    --track data --start-group 6
```

---

## License

SPDX-FileCopyrightText: Copyright (c) 2024 Cisco Systems
SPDX-License-Identifier: BSD-2-Clause
