# qClient

qClient is an example client. It implements the client API to make a connection
to the relay and to act as subscriber, publisher, or both. The client program
will read from `stdin` when in publisher mode to publish data. Alternatively,
the client can be configured to publish a timestamp using the option `--clock`.

Use `qclient -h` to view more options.

> [!NOTE]
> The **namespace** and **name** for both publish and subscribe can be any string
> value. The only requirement is to publish and subscribe to the same values.

### As chat subscriber

```
./qclient --sub_namespace chat --sub_name general
```

### As chat publisher

```
./qclient --pub_namespace chat --pub_name general
```

This will start the client as a publisher in `stdin`, and will prompt the user
to enter text. To exit the client from this mode, use `Ctrl+D` (Linux/MacOS)
or `Ctrl+Z` (Windows).

### As both chat subscriber and publisher

```
./qclient --sub_namespace chat --sub_name general --pub_namespace chat --pub_name general
```

### As clock publisher

```
./qclient --pub_namespace clock --pub_name second --clock
```

This will send a timestamp periodically on the user's behalf. To exit the client
in this mode, simply interrupt the program (`Ctrl+C`)

### As clock subscriber

```
./qclient --sub_namespace clock --sub_name second
```