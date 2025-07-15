# How to Build

This project can be built with `jst` or bootstrapped entirely from scratch.

## Building with `jst` or `just-mr`

If you have an installation of `jst` already available, `jst` can simply be
built by:

```sh
$ jst build ALL
$ jst install ALL -o ${DESTDIR}
```

... or with `just-mr`:

```sh
$ just-mr build ALL
$ just-mr install ALL -o ${DESTDIR}
```

This will build and install `jst` for Linux on the x86_64 architecture with a dynamic link
dependency on glibc.

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
$ jst install ALL -o ${DESTDIR} -D '{"DEBUG": {"ENABLE": true}}'
```

#### Example: Cross-compilation

First, make sure that the cross-compiler for the desired architecture is
installed and working properly. For example, to build `jst` for 64-bit ARM,
specify `arm64` as the target architecture and `gnu` (GCC) as the target
compiler family:

```sh
$ jst install ALL -o ${DESTDIR} \
    -D '{"TARGET_ARCH": "arm64", "TOOLCHAIN_CONFIG": {"FAMILY": "gnu"}}'
```

> Note that cross-compilation is only supported for `gnu` and `clang` compiler
> families.

### Build Targets

All targets intended for end-users are defined in the top-level [`TARGETS`](./TARGETS) file.
Most notable targets are:

- `ALL` (default target): builds the main binaries `jst` and `jst_backend`.
- `PACKAGE`: builds a complete installation, including auxiliary tools, man
  pages, and bash completion files.

  > Note that the generation of man pages requires the `pandoc` tool to be
  > installed.

## Bootstrapping `jst`

In case you have neither `jst` nor `just-mr` available, you need to bootstrap
`jst` first:

```sh
$ ./bin/bootstrap.py
```

By default, all dependencies will be fetched from the internet, built from
source, and bundled in the final binaries. These binaries can be found in a
temporary directory, reported at the end of the bootstrap process.

If you want to avoid fetching archives from the internet, you can pre-download
the relevant archives listed in [`repos.in.json`](./etc/repos.in.json) and store
them in a directory (`DISTDIR`) on the local disk. The path to this directory,
as well as the source and build directory, can be specified as optional
arguments:

```sh
$ ./bin/bootstrap.py ${SRCDIR} ${BUILDDIR} ${DISTDIR}
```

To avoid any form of fetching and bundling, you can build against dependencies
from the host system by using the `PACKAGE` option described below.

### Bootstrap Options

The bootstrap process can be parameterized via environment variables. The
following list of environment variables are supported:

|Variable|Description|Default Value|
|-|:-|:-:|
| DEBUG             | Bootstrap sequentially | *not set* |
| BOOTSTRAP_CONF    | The `jst` build options (JSON map) | { } |
| BOOTSTRAP_TARGET  | The target to bootstrap | ALL |
| BOOTSTRAP_MODULE  | The bootstrap target's module | "" (top-level module) |
| PACKAGE           | Build against system dependencies | *not set* |
| PKG_CONFIG_PATH   | Custom path for `pkg-config` | *not set* |
| LOCALBASE         | Path to local base for system dependencies | / |
| PKG_PATHS         | `pkg-config` paths in *LOCALBASE* (JSON list) | ["lib/pkgconfig", "share/pkgconfig"] |
| NON_LOCAL_DEPS    | Dependencies not taken from system (JSON list) | [ ] |
| ENV               | Additional environment vars (JSON map) | { } |
| SOURCE_DATE_EPOCH | Seconds since Unix epoch | *not set* |

#### Example: Bootstrap with custom build options

To bootstrap `jst` with custom [build options](#build-options), use the
`BOOTSTRAP_CONF` environment variable:

``` sh
$ BOOTSTRAP_CONF='{"DEBUG": {"ENABLE": true}}' ./bin/bootstrap.py
```

#### Example: Bootstrap custom targets

To bootstrap different `jst` [build targets](#build-targets), use the
`BOOTSTRAP_TARGET` environment variable:

``` sh
$ BOOTSTRAP_TARGET='PACKAGE' ./bin/bootstrap.py
```

#### Example: Bootstrap against system dependencies

To bootstrap against preinstalled system dependencies, specify the environment
variables `PACKAGE` and `LOCALBASE` (typically `/usr` or `/usr/local`)
accordingly:

``` sh
$ PACKAGE='YES' \
  LOCALBASE='/usr' \
  NON_LOCAL_DEPS='["google_apis", "bazel_remote_apis"]' \
    ./bin/bootstrap.py
```

> Note that system dependencies will be resolved using the `pkg-config` tool,
> which must be available in `PATH`.

If some dependencies should not be taken from the system, those can be
specified in the `NON_LOCAL_DEPS` variable, which has to contain a JSON list.
The full list of valid dependencies can be found in [`repos.in.json`](./etc/repos.in.json).
If the environment variable `PKG_CONFIG_PATH` is set, the bootstrap script
forwards it to the build so that `pkg-config` can pick up the correct files.
