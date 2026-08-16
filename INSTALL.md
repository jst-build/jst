# How to Build

This project can be built with `jst` or bootstrapped entirely from scratch.

## Building with `jst`

If you already have an installation of `jst` available, `jst` can simply be
built by installing the required [system dependencies](./doc/BUILD_DEPS.md) and
running:

```sh
$ jst build
$ jst install -o ${DESTDIR}
```

> Note that system dependencies will be resolved using the `pkg-config` tool,
> which must be available in `PATH`.

This will build and install `jst` for Linux on the x86_64 architecture with a
dynamic link dependency on glibc.

### Build Options

Build options are provided via configuration variables.
The following table describes the most important supported configuration
variables. The full list can be obtained via `jst describe`.

|Variable|Supported Values|Default Value for `jst`|
|-|:-:|:-:|
| OS | linux | linux |
| ARCH | x86, x86_64, arm, arm64 | x86_64 |
| HOST_ARCH | x86, x86_64, arm, arm64 | *derived from ARCH* |
| TARGET_ARCH | x86, x86_64, arm, arm64 | *derived from ARCH* |
| DEBUG | map, anything logically false | null |
| TOOLCHAIN_CONFIG["FAMILY"] | gnu, clang, unknown | unknown |
| TOOLCHAIN_CONFIG["BUILD_STATIC"] | true, false | false |

> Note that you can choose a different stack size for resulting binaries by
> adding `"-Wl,-z,stack-size=<size-in-bytes>"` to variable `"FINAL_LDFLAGS"`
> (which has to be a list of strings).

#### Example: Debug build

Configuration variables are specified via JSON objects, encoded as string
arguments using the command line option `-D`. The following example specifies
the configuration variable `DEBUG` to obtain a debug build:

```sh
$ jst install -o ${DESTDIR} -D '{"DEBUG": {"ENABLE": true}}'
```

#### Example: Bundled build

When building with bundled dependencies, all dependencies will be fetched from
the internet, built from source, and bundled into the final binaries. This
requires using a different repository configuration, `bundled.json`, which is
specified via the command line option `-C`. The following example initiates a
bundled build:

```sh
$ jst -C etc/bundled.json install -o ${DESTDIR}
```

#### Example: Cross-compilation

First, make sure that the cross-compiler for the desired architecture is
installed and working properly. For example, to build `jst` for 64-bit ARM,
specify `arm64` as the target architecture and `gnu` (GCC) as the target
compiler family:

```sh
$ jst -C etc/bundled.json install -o ${DESTDIR} \
    -D '{"TARGET_ARCH": "arm64", "TOOLCHAIN_CONFIG": {"FAMILY": "gnu"}}'
```

> Note that cross-compilation is only supported for `gnu` and `clang` compiler
> families.

## Bootstrapping `jst`

In case you do not have `jst`, you need to bootstrap it first:

```sh
$ ./bin/bootstrap.py
```

By default, all dependencies will be taken from the system. To avoid any need
for system dependencies, you can request to fetch and build all dependencies
from source by using the `BUNDLED` option described below.

### Bootstrap Options

The bootstrap process can be parameterized via environment variables. The
following environment variables are supported:

|Variable|Description|Default Value|
|-|:-|:-:|
| DEBUG             | Bootstrap sequentially | *not set* |
| BOOTSTRAP_CONF    | The `jst` build options (JSON map) | { } |
| BOOTSTRAP_TARGET  | The target to bootstrap | ALL |
| BOOTSTRAP_MODULE  | The bootstrap target's module | "" (top-level module) |
| BUNDLED           | Build with bundled dependencies | *not set* |
| PKG_CONFIG_PATH   | Custom path for `pkg-config` | *not set* |
| LOCALBASE         | Path to local base for system dependencies | / |
| PKG_PATHS         | `pkg-config` paths in *LOCALBASE* (JSON list) | ["lib/pkgconfig", "share/pkgconfig"] |
| SYSTEM_DEPS       | Dependencies taken from system, for bundled builds (JSON list) | [ ] |
| ENV               | Additional environment vars (JSON map) | { } |
| SOURCE_DATE_EPOCH | Seconds since Unix epoch | *not set* |

#### Example: Bootstrap with custom build options

To bootstrap `jst` with custom [build options](#build-options), use the
`BOOTSTRAP_CONF` environment variable:

``` sh
$ BOOTSTRAP_CONF='{"BUILD_MANPAGES": true}' ./bin/bootstrap.py
```

#### Example: Bootstrap with bundled dependencies

To bootstrap with bundled dependencies built from source, specify the
environment variable `BUNDLED`:

``` sh
$ BUNDLED='YES' \
  SYSTEM_DEPS='["curl", "libarchive"]' \
    ./bin/bootstrap.py
```

If some dependencies should not be bundled but taken from the system, those can
be specified in the `SYSTEM_DEPS` variable, which has to contain a JSON list.
The full list of valid dependencies can be found in
[`bundled.in.json`](./etc/bundled.in.json). If the environment variable
`PKG_CONFIG_PATH` is set, the bootstrap script forwards it to the build so that
`pkg-config` can pick up the correct files.

If you want to avoid fetching archives from the internet, you can pre-download
the relevant archives listed in [`bundled.json`](./etc/bundled.json) and store
them in a directory (`DISTDIR`) on the local disk. The path to this directory,
as well as the source and build directory, can be specified as optional
arguments:

```sh
$ BUNDLED=YES ./bin/bootstrap.py ${SRCDIR} ${BUILDDIR} ${DISTDIR}
```
