# Jst Tutorial: Basic Targets using Built-in Rules

## Introduction

This tutorial will guide you through the necessary steps for setting up a
minimalistic project with targets that only use [built-in rules](../../doc/concepts/built-in-rules.md).

### What you need

For completing this tutorial, you will need:

- `jst` installed in your `PATH`
- optionally, the *Justlang* IDE extension for Visual Studio Code users

### Tutorial structure

The tutorial files are located in the `examples` directory of the `jst`
repository and have the following structure:

``` plaintext
examples
└── basics-tutorial
    ├── ROOT
    ├── TARGETS
    └── name.txt
```

This tutorial is separated into three stages, each focusing on one specific
built-in rule: [`generic`](../../doc/concepts/built-in-rules.md#generic), [`file_gen`](../../doc/concepts/built-in-rules.md#file_gen), and [`install`](../../doc/concepts/built-in-rules.md#install).

### The TARGETS file

The `TARGETS` file defines the build description. It contains an object that
maps a *target name* to a *target description*. Each target supports the
mandatory field `type`, which defines the rule to apply. Different rules may
support different fields. Please consider the following example:

``` jsonnet
{
  example: {
    type: 'generic',
    cmds: [/* command lines */],
    outs: [/* output files */],
  },
}
```

The target with name `example` uses the built-in rule [`generic`](../../doc/concepts/built-in-rules.md#generic), which
supports the additional fields `cmds`, `outs`, and more.

## Stage 1: Generic targets

Generic targets use the built-in rule [`generic`](../../doc/concepts/built-in-rules.md#generic), which is similar to
rules in a classical `Makefile` or Bazel's `genrule`. A generic target runs a
set of shell commands to produce one or more output artifacts (files or
directories). Optionally, its execution may depend on artifacts from other
targets, use a custom environment, or specify a different working directory.

Please step into the workspace root of this tutorial:

``` sh
$ cd basics-tutorial
```

### Target without dependencies

Let's begin by defining a minimal `generic` target in the `TARGETS` file, next
to the `ROOT` file that marks the workspace root.

``` jsonnet
{
  plain_greeter: {
    type: 'generic',
    cmds: ['echo "Hello World" > out.txt'],
    outs: ['out.txt'],
  },
  // ...
}
```

The target `plain_greeter` has no dependencies and produces a single output file
`out.txt` from a static command line.

Build the output of this greeter:

``` sh
$ jst build plain_greeter
```

By default, the subcommand `build` will not pollute the local source tree with
any produced outputs. If you want to copy the produced outputs to your local
file system, use the subcommand `install` and specify the output path via the
option `-o`:

``` sh
$ jst install plain_greeter -o out
```

Let's investigate the installed output file:

``` sh
$ cat out/out.txt
```

This should print the string "`Hello World`".

If you just want to see the generated output without installing it, you can also
use the `-P` option after the subcommand `build`:

``` sh
$ jst build plain_greeter -P out.txt
```

### Target with source file dependencies

Next, we would like to create a `generic` target that has a file dependency.

``` jsonnet
{ // ...
  file_greeter: {
    type: 'generic',
    deps: ['name.txt'],
    cmds: [
      'echo -n "Hello " > out.txt',
      'cat name.txt >> out.txt',
    ],
    outs: ['out.txt'],
  },
  // ...
}
```

The target `file_greeter` has a dependency on the target `name.txt`.
As the `TARGETS` file does not contain any target named `name.txt`, it is
implicitly treated as a [source file target](../../doc/concepts/overview.md#files).
The content of that file is used to produce the string for the output file.

Build this greeter and see its output:

``` sh
$ jst build file_greeter -P out.txt
```

This should print the string "`Hello Galaxy`".

### Target with a dependency on another target

Finally, let's create a `generic` target that takes the output of another target
as input.

``` jsonnet
{ // ...
  to_upper: {
    type: 'generic',
    deps: ['file_greeter'],
    cmds: ['cat out.txt | tr "a-z" "A-Z" > OUT.txt'],
    outs: ['OUT.txt'],
  },
  // ...
}
```

The target `to_upper` reads the output file `out.txt` from the `file_greeter`
target, converts its content to uppercase, and writes the result to the output
file `OUT.txt`.

Build this target and see its output:

``` sh
$ jst build to_upper -P OUT.txt
```

This should print the string "`HELLO GALAXY`".

> Note: when using remote execution for building, the intermediate file
> `out.txt` is not even transferred to the client and only exists in the
> remote's content-addressable storage (CAS), unless explicitly requested.

## Stage 2: File-gen targets

Producing a text file can also be achieved by using the built-in rule [`file_gen`](../../doc/concepts/built-in-rules.md#file_gen).

``` jsonnet
{ // ...
  generated: {
    type: 'file_gen',
    arguments_config: ['GREETEE'],
    name: 'out.txt',
    data: 'Hello ' + jst.env('GREETEE', default='Universe') + '\n',
  },
  // ...
}
```

The target `generated` produces the file `out.txt` with its content computed
from the expression in field `data`. The expression concatenates the string
`'Hello '` with the value from the configuration variable `GREETEE`, which
defaults to the string `'Universe'` if not set.

> Note: configuration variables are read using the function [`jst.env()`](../../extern/justlang/doc/stdlib.md#jstenvname-defaultnull).

Build the `file_gen` target without variables:

``` sh
$ jst build generated -P out.txt
```

This should print the string "`Hello Universe`".

Configuration variables can be set by providing a JSON object either via
`-c,--config` for reading from file, or via `-D,--define` for reading inline
JSON from the command line.

Build the `file_gen` target with variable `GREETEE` set to `'Everybody'`:

``` sh
$ jst build generated -P out.txt -D'{"GREETEE":"Everybody"}'
```

This should print the string "`Hello Everybody`".

## Stage 3: Install targets

Last, we would like to demonstrate the built-in rule [`install`](../../doc/concepts/built-in-rules.md#install),
which allows the flexible restaging (renaming/restructuring) of artifacts.

For example, what if we want to combine the outputs of the above greeters in a
single target? It would lead to a conflict, as all of them produce an output
file with the same name `out.txt`. In such scenarios, the following `install`
target can be used to restage the outputs to different names.

``` jsonnet
{ // ...
  ALL: {
    type: 'install',
    files: {
      'out/plain.txt': 'plain_greeter',
      'out/file.txt': 'file_greeter',
      'out/gen.txt': 'generated',
    },
  },
}
```

Build this target to produce and restage all output files:

``` sh
$ jst build ALL
```

The output produced should look like this:

``` plaintext
INFO: Requested target '""//:ALL' with config: {}
INFO: Discovered 2 actions, 0 tree overlays, 0 trees, 1 blobs
INFO: Processed 2 actions, 2 cache hits.
INFO: Artifacts built, logical paths are:
        out/file.txt [34c97eeca89eb286aed798efd885da6ea77e9a96:13:f]
        out/gen.txt [a17dcb5259599be90a546576d571d2afcb66e37b:15:f]
        out/plain.txt [557db03de997c86a4a028e1ebd3a1ceb225be238:12:f]
```

You can see that these artifacts were produced from processing two actions (the
`generic` targets) and locally computing one blob (the `file_gen` target). The
`install` target itself is not visible, as it is only a local recomputation of
artifact names.

Of course you can still use the `-P` option to directly print an artifact's
content:

``` sh
$ jst build ALL -P out/gen.txt
```

This should print the string "`Hello Universe`".

