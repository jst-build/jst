# Jst Build System

`jst` is a generic build system supporting multi-repository and multi-language
builds. The language-specific information to translate high-level
concepts (libraries, binaries) into individual compile actions is
taken from user-defined rules described by functional expressions.

Designated targets are taken entirely from cache, if the repositories
transitively involved have not changed. So, by making good use of
the multi-repository structure, the action graph can be kept small.
Remote build execution is supported and the remote-building of
cachable targets can be fully delegated to a service (provided by
the tool itself); when doing so, it is not necessary to have the
dependencies locally (neither as source nor as binary).

## Why jst?

What sets `jst` apart from other build systems:

- Built-in integration of Git, using its object database
- Support for multi-language builds via [user-defined rules](doc/concepts/rules.md)
- Support for conflict-free [multi-repository builds](doc/concepts/multi-repo.md)
- Decoupling files from their local path: ["staging"](doc/concepts/overview.md#staging)
- Action graph pruning via [target-level caching](doc/concepts/target-cache.md)
- Building without the sources via [absent roots](doc/concepts/service-target-cache.md#delegation-absent-roots-in-jst_backend-repository-specification)
- Reduced network traffic via [blob splitting](doc/concepts/blob-splitting.md)
- Faster communication with [directories as first-class citizens](doc/specification/remote-protocol.md)
- Built-in single-node remote execution server: `jst backend execute`

## Getting started

In an empty directory, create a file named `TARGETS` with the following content:

``` jsonnet
{
  // Target 'helloworld' based on built-in rule 'generic'
  helloworld: {
    type: 'generic',
    cmds: ['echo Hello World! > helloworld.txt'],
    outs: ['helloworld.txt'],
  },
}
```

Build the `helloworld` target and install its outputs to directory `out`:

``` sh
$ touch ROOT    # create workspace root
$ jst install helloworld -o out
```

Print the output file `helloworld.txt`:

``` sh
$ cat out/helloworld.txt
Hello World!
```

## Tutorial

- [Basics](examples/basics-tutorial/README.md)
- [C/C++](examples/cpp-tutorial/README.md)
- Java (*coming soon*)
- Rust (*coming soon*)
- Cangjie (*coming soon*)

## Justlang documentation

- [Getting Started](doc/justlang/getting-started.md)
- [Standard Library](doc/justlang/stdlib.md)
- [Troubleshooting](doc/justlang/troubleshoot.md)

## General documentation

- [Overview](doc/concepts/overview.md)
- [Build Configurations](doc/concepts/configuration.md)
- [Multi-Repository Builds](doc/concepts/multi-repo.md)
- [Built-in Rules](doc/concepts/built-in-rules.md)
- [Cache Pragma and Testing](doc/concepts/cache-pragma.md)
- [Target-Level Caching](doc/concepts/target-cache.md)
- [Target-Level Caching as a Service](doc/concepts/service-target-cache.md)
- [Garbage Collection](doc/concepts/garbage.md)
- [Symbolic links](doc/concepts/symlinks.md)
- [Tree overlays](doc/concepts/tree-overlay.md)
- [Execution properties](doc/concepts/execution-properties.md)
- [Computed roots](doc/concepts/computed-roots.md)
- [Profiling and Invocation Logging](doc/concepts/profiling.md)
