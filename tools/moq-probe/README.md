<!--
SPDX-FileCopyrightText: Copyright (c) 2026 Cisco
SPDX-License-Identifier: CC-BY-4.0
-->

# MoQ Probe

MoQ Probe is an interactive client for testing and debugging libquicr. It
connects to a relay and can act as a subscriber, publisher, fetcher, or a
combination of these roles. In publisher mode, the client can read objects
from `stdin` or publish timestamps with the `--clock` option.

Use `moq-probe -h` to view all options.

> [!NOTE]
> The **namespace** and **name** for both publish and subscribe can be any string
> value. The only requirement is to publish and subscribe to the same values.

### As chat subscriber

```
moq-probe --sub_namespace chat --sub_name general
```

### As chat publisher

```
moq-probe --pub_namespace chat --pub_name general
```

This will start the client as a publisher in `stdin`, and will prompt the user
to enter text. To exit the client from this mode, use `Ctrl+D` (Linux/MacOS)
or `Ctrl+Z` (Windows).

### As both chat subscriber and publisher

```
moq-probe --sub_namespace chat --sub_name general --pub_namespace chat --pub_name general
```

### As clock publisher

```
moq-probe --pub_namespace clock --pub_name second --clock
```

This will send a timestamp periodically on the user's behalf. To exit the client
in this mode, simply interrupt the program (`Ctrl+C`)

### As clock subscriber

```
moq-probe --sub_namespace clock --sub_name second
```