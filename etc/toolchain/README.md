# System toolchain for the `jst` build system

A toolchain definition using the tools from the local system for building.

## How to use this Repository

To import `toolchain` to your repository, add the following code to the *imports
section* of your `repos.in.json` and run `jst-lock` to generate the final
repository lock-file

```jsonc
"imports": [
  {
    "source": "git",
    "branch": "system",
    "url": "https://github.com/jst-build/toolchains-cc",
    "repos": [{"alias": "toolchain"}]
  },
  // ...
],
```

## General Configuration

The toolchain is configured using the following configuration variables:

|Variable|Description|Default Value|
|-|-|-:|
| `DEBUG` | Debug build options | `null` |
| `ARCH` | Unqualified architecture | `"x86_64"` |
| `HOST_ARCH` | Build host's architecture | *derived from ARCH* |
| `TARGET_ARCH` | Build target's architecture | *derived from ARCH* |
| `TOOLCHAIN_CONFIG["FAMILY"]` | Compiler family | `"generic"` |
| `TOOLCHAIN_CONFIG["VENDOR"]` | Target triplet vendor | `""` |
| `TOOLCHAIN_CONFIG["SYSTEM"]` | Target triplet system | `"linux-gnu"` |
| `TOOLCHAIN_CONFIG["CFLAGS"]` | Extra C compile flags | `[]` |
| `TOOLCHAIN_CONFIG["CXXFLAGS"]` | Extra C++ compile flags | `[]` |
| `TOOLCHAIN_CONFIG["LDFLAGS"]` | Extra linker flags | `[]` |
| `TOOLCHAIN_CONFIG["PATH"]` | Toolchain system paths | `["/bin", "/usr/bin"]` |
| `TOOLCHAIN_CONFIG["GRPC_PLUGIN"]` | gRPC C++ plugin path | `"/usr/bin/grpc_cpp_plugin"` |
| `TOOLCHAIN_CONFIG["PKG_CONFIG_PATH"]` | Additional pkg-config paths | `[]` |
| `TOOLCHAIN_CONFIG["FORCE_CC"]` | Force C compiler name | `null` |
| `TOOLCHAIN_CONFIG["FORCE_CXX"]` | Force C++ compiler name | `null` |
| `TOOLCHAIN_CONFIG["FORCE_AR"]` | Force archiver name | `null` |
| `TOOLCHAIN_CONFIG["FORCE_DWP"]` | Force DWARF pkg utility | `null` |

> Please note that `DEBUG` is a map and anything logically false (e.g., `null`,
> empty map) disables debug mode. For full list of valid options, please see the
> [C/C++ rule documentation on debug fission](https://github.com/jst-build/rules-cc/blob/master/doc/debug-fission.md).

The system toolchain supports the following compiler families:

- `"gnu"`: GCC compiler from system
- `"clang"`: Clang compiler from system
- `"generic"`: Generic compiler interface (`cc`/`c++`)

Please note that the family `"generic"` does not support cross-compilation. For
the other families, the cross-compilation target triplet is computed from the
variables: `<TARGET_ARCH>-[<VENDOR>-]<SYSTEM>`.


**Example:** Building with GCC on Void Linux x86_64 with musl libc:

``` json
{
  "ARCH": "x86_64",
  "TOOLCHAIN_CONFIG": {
    "FAMILY": "gnu",
    "SYSTEM": "linux-musl",
    "FORCE_AR": "ar"
  }
}
```

**Example:** Cross-compiling for arm64 with Clang on RedHat Linux x86_64:

``` json
{
  "ARCH": "x86_64",
  "TARGET_ARCH": "arm64",
  "TOOLCHAIN_CONFIG": {
    "FAMILY": "clang",
    "VENDOR": "redhat",
    "SYSTEM": "linux-gnu"
  }
}
```
