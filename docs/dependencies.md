# Dependency management

## Caching dependency sources

By default CPM clones each dependency into the build tree under `build/_deps`, so removing the
build directory with `make cclean` (or `rm -rf build`) discards the sources and the next configure
downloads them again.

Set `CPM_SOURCE_CACHE` to a directory outside the build tree to retain and share dependency
sources:

```bash
export CPM_SOURCE_CACHE=$HOME/.cache/CPM
```

It can also be passed for a single configure:

```bash
cmake -B build -DCPM_SOURCE_CACHE=$HOME/.cache/CPM
```

Entries are keyed by package name and version, so separate build directories, crypto backend
selections, and branch switches can share the same sources. The cache also holds the bootstrapped
CPM release.

## Third-party licence notices

[CPMLicenses.cmake](https://github.com/cpm-cmake/CPMLicenses.cmake) can collect licence and
copyright files from CPM dependencies distributed with libquicr:

```bash
cmake -B build -DQUICR_GENERATE_THIRD_PARTY_LICENSES=ON
cmake --build build --target third-party-licenses
```

Alternatively, run `make licenses`. Both commands write
`build/THIRD_PARTY_LICENSES.txt`.

The target intentionally includes only `timeq`, `picotls`, `picoquic`, and conditional `fmt`;
test, benchmark, example, and build-tool dependencies are excluded.

CPMLicenses uses the first dependency-root file beginning with `LICENSE`, `LICENCE`, `COPYING`, or
`Copyright`. Review its warnings and output for dependencies with missing or ambiguous licence
files. The generated notice complements rather than replaces the product SBOM and licence-policy
checks.
