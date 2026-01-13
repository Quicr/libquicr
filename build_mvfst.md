# Building with MVFST

## Prerequisites

### macOS
```bash
brew install mvfst proxygen
```

### Linux
```bash
cd dependencies/mvfst
sudo ./build/fbcode_builder/getdeps.py install-system-deps --recursive mvfst
python3 ./build/fbcode_builder/getdeps.py build mvfst --install-prefix=/usr/local
```

## Build

```bash
mkdir -p build-mvfst && cd build-mvfst
cmake -DUSE_MVFST=ON ..
make -j$(nproc)
```

## Running Client with TLS Skip Verification

For testing with self-signed certificates:

```bash
./cmd/examples/qclient -r moq://localhost:1234 \
    --quic_stack mvfst \
    --tls_skip_verify \
    --pub_namespace "test" \
    --pub_name "track1"
```

Or for subscriber:

```bash
./cmd/examples/qclient -r moq://localhost:1234 \
    --quic_stack mvfst \
    --tls_skip_verify \
    --sub_namespace "test" \
    --sub_name "track1"
```
