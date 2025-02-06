# Jst Build System

`jst` is a generic build system supporting multi-repository
builds. The language-specific information to translate high-level
concepts (libraries, binaries) into individual compile actions is
taken from user-defined rules described by functional expressions.

What sets `jst` apart from other build systems:

- Written in C++ (mostly)
- Built-in integration of Git, using its object database
- Support for multi-language builds, via [user-defined rules](doc/concepts/rules.md)
- Support for true [multi-repository builds](doc/concepts/multi-repo.md)
- Decoupling files from their local path ("[staging](doc/concepts/overview.md#staging)")
- Action graph pruning, via [target-level caching](doc/concepts/target-cache.md)
- Building without the sources, via [absent roots](doc/concepts/service-target-cache.md#delegation-absent-roots-in-jst_backend-repository-specification)
- Reduced network traffic, via [blob splitting](doc/concepts/blob-splitting.md)
- Built-in single-node remote execution server (`jst backend execute`)

## Getting started

In an empty directory, create a file named `TARGETS` with the following content:

``` jsonnet
{
  // Target 'helloworld' based on built-in rule 'generic'
  helloworld: {
    type: 'generic',
    cmds: ['echo Hello World > helloworld.txt'],
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
Hello World
```

## Tutorial

- [Basics](examples/basics-tutorial/README.md)
- [C/C++](examples/cpp-tutorial/README.md)
- Rust (*coming soon*)
- Cangjie (*coming soon*)

## Justlang documentation

- [Standard Library](doc/justlang/stdlib.md)

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
- [Execution properties](doc/concepts/execution-properties.md)
- [Computed roots](doc/concepts/computed-roots.md)

## Rule writing documentation

- [Rule Expression Language](doc/concepts/expressions.md)
- [User-Defined Rules](doc/concepts/rules.md)
- [Documentation Strings](doc/concepts/doc-strings.md)
- [Anonymous Targets](doc/concepts/anonymous-targets.md)
