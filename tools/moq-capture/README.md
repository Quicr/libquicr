<!--
SPDX-FileCopyrightText: Copyright (c) 2026 Cisco
SPDX-License-Identifier: CC-BY-4.0
-->

# MoQ Capture

MoQ Capture records objects from a MOQT track and replays previously recorded
objects through a relay.

Use `moq-capture -h` to view all options.

## Record a track

```sh
moq-capture --record --namespace test/track --name messages
```

## Replay a track

```sh
moq-capture --replay --namespace test/track --name messages
```

Recordings are stored in `moq_data` by default. Use `--data_dir` to select a
different directory and `--replay_speed_ms` to control the interval between
replayed objects.

## Dependencies

| Dependency | Purpose | Declared in |
| --- | --- | --- |
| [nlohmann/json](https://github.com/nlohmann/json) | Recording metadata | [`CMakeLists.txt`](CMakeLists.txt) |

> [!WARNING]
> Recordings contain application payloads. Store and share them according to
> the sensitivity of the captured data.
