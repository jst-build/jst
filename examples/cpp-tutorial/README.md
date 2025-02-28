# Jst Tutorial: Build a C++ Project

## Introduction

This tutorial will guide you through the necessary steps for setting up and
building a C++ project.

### What you need

For completing this tutorial, you will need:

- `jst` and `jst-lock` installed in your `PATH`
- a working internet connection (for fetching rules, toolchain, externals, etc.)
- optionally, the *Justlang* IDE extension for Visual Studio Code users

> Note: without an internet connection, you need to configure local mirrors
> for the required external repositories, explained in section [Company setup](#company-setup)
> below.

### Tutorial structure

The tutorial files are located in the `examples` directory of the `jst`
repository and have the following structure:

``` plaintext
examples
└── cpp-tutorial
    ├── stage1/
    ├── stage2/
    ├── stage3/
    └── stage4/
```

Each subdirectory is a separate project, representing a different stage in this
tutorial. First, you will learn how to set up a minimal C++ project with only a
single binary. In the second stage, you will add a library dependency to the
binary and provide project-wide settings that are applied to both of these
targets. In the third stage, you will add binary tests and shell tests to your
project. Finally, in the last stage, you will learn how to import external
projects, declare the public targets as *export targets*, and define convenient
*install targets*.

### The repos.in.json and TARGETS file

The `repos.in.json` file contains the *multi-repository configuration*. In
`jst`, most projects are multi-repository projects, because the *rules* and
*toolchains* are typically imported repositories. The general structure of
the `repo.in.json` file is defined as:

``` jsonc
{
  "main": "",           // name of the main repo to build
  "imports": [],        // list of repo imports (order matters)
  "repositories": {},   // map of repo name to repo description
}
```

The `TARGETS` file defines the build description. It contains an object that
maps a *target name* to a *target description*. Each target supports the
mandatory field `type`, which defines the rule to apply. Different rules may
support different fields. Please consider the following example:

``` jsonnet
{
  example: {
    type: @'rules//CC:binary',
    name: [/* binary name */],
    srcs: [/* C++ sources */],
  },
}
```

The target with name `example` uses the rule [`//CC:binary`](https://github.com/just-buildsystem/rules-cc#rule-cc-binary) from repository
`rules`, which is used to build executable binaries from C++ sources. This rule
supports the additional fields `name`, `srcs`, and many more.

## Stage 1: Describe and build single binary

Let's start by describing and building a single C++ binary. Please have a look
at the tutorial files for stage1.

``` plaintext
examples
└── cpp-tutorial
    └── stage1
        ├── ROOT
        ├── repos.in.json
        └── src
            ├── helloworld.cpp
            └── TARGETS
```

Step into the workspace root of stage 1:

``` sh
$ cd cpp-tutorial/stage1
```

### Project setup and toolchain import

To setup the project, we need to do two things:

1. Specify the *main repository* and the location of targets and sources
2. Import a [toolchain for C/C++](https://gitee.com/justbuild/toolchains-cc), which includes a [rule set for C/C++](https://github.com/just-buildsystem/rules-cc)

Let's start to setup the project by creating the `repos.in.json` file, next to
the `ROOT` file, marking the workspace root.

``` jsonc
{
  "main": "stage1",
  "imports": [/*...*/],
  "repositories": {
    "stage1": {
      "repository": {"type": "file", "path": "."},
      "bindings": {"rules": "toolchain"}
    }
  }
}
```

Here, we specify the *main repository* to be `"stage1"`. This repository is
defined as a *file repository* located at *path* `"."`. We furthermore specify a
*binding* from the name `"rules"` to the repository `"toolchain"`.

The `"toolchain"` repository itself is just an *alias* for the
`"system_toolchain"` repository that is imported from the *master branch* of the
Git repository [toolchains-cc](https://gitee.com/justbuild/toolchains-cc).

``` jsonc
{ // ...
  "imports": [{
    "source": "git",
    "branch": "master",
    "url": "https://gitee.com/justbuild/toolchains-cc.git",
    "repos": [{"repo": "system_toolchain", "alias": "toolchain"}]
  }],
  // ...
}
```

This repository already includes a suitable [rule set for C/C++](https://github.com/just-buildsystem/rules-cc),
so it can be directly used as rules for repository `"stage1"`.

Now, the last missing piece is the `repos.json` file. It serves as a
*lock-file*, pinpointing to the exact revisions of all imported repositories.
This file is required for building. It can be generated from the `repos.in.json`
file by running `jst-lock`:

``` sh
$ jst-lock  # generates repos.json lock-file (from repos.in.json)
```

Afterward, the `repos.json` lock-file will appear in the workspace root of
stage 1.

> Note: this lock-file is not just mandatory for building, but also required if
> someone wants to import your project, and therefore should be committed!

### Build and execute the binary

With the project and toolchain successfully set up, we can now start to describe
the binary target in the `src/TARGETS` file.

``` jsonnet
{
  helloworld: {
    type: @'rules//CC:binary',
    name: ['helloworld'],
    srcs: ['helloworld.cpp'],
  },
}
```

The target `helloworld` uses the rule [`//CC:binary`](https://github.com/just-buildsystem/rules-cc#rule-cc-binary) from `rules` and
specifies the binary `name` and the `srcs` to use.

Use subcommand `build` to build the binary:

``` sh
$ jst build src helloworld
```

The first argument is `src`, which specifies the module path of the target,
relative to the workspace root. The second argument specifies the target name to
build: `helloworld`.

`jst` will produce an output similar to this:

``` plaintext
INFO: Found 4 repositories to set up
INFO: Requested target 'stage1//src:helloworld' with config: {}
INFO: Discovered 2 actions, 1 trees, 0 blobs
INFO: Processed 2 actions, 0 cache hits.
INFO: Artifacts built, logical paths are:
        helloworld [6b4830b02b80974739f58659f44e464ee69abc08:16928:x]
```

By default, the subcommand `build` will not pollute the local source tree with
any produced outputs. If you want to copy the produced outputs to your local
file system, use the subcommand `install` and specify the output path via the
option `-o`:

``` sh
$ jst install src helloworld -o out
```

> Note: you should see only cache hits, because all actions are identical.

Let's run the installed binary:

```
$ out/helloworld
```

This should produce the command line output "`Hello World!`".

Now try to change the `helloworld.cpp` source file and rebuild. Meaningful
changes will produce a binary with a different hash. Reverting your changes
should restore the original binary's hash, as everything is taken from cache.

### Debug builds and cross-compilation

The build can be parameterized by configuration variables. Those can
be set by providing a JSON object via the following `jst` options:

- `-c,--config` for reading from a JSON file
- `-D,--define` for specifying inline JSON on the command line

The full list of variables supported by the imported toolchain can be obtained
from the [General Configuration](https://gitee.com/justbuild/toolchains-cc#general-configuration) section of the toolchain repository.

One of the supported variables is `DEBUG`, which is not set by default. Setting
it to `true` will result in a debug build.

```sh
$ jst build src helloworld -D'{"DEBUG":true}' -v
```

The `-v` flag shows that the compile command now contains debug flags
`[..., "-O0", "-g", ...]`.

Similarly, the variables `TARGET_ARCH` and toolchain `FAMILY` must be specified
for cross-compilation, according to the [system toolchain documentation](https://gitee.com/justbuild/toolchains-cc#system-toolchain).

```sh
$ jst build src helloworld -D'{"TARGET_ARCH":"arm64","TOOLCHAIN_CONFIG":{"FAMILY":"gnu"}}' -v
```

Again, the `-v` flags shows that the executed compile and linker commands are
now using the cross-compiler `["aarch64-linux-gnu-g++", ...]`.

### Summary: Stage 1

In this tutorial stage, you learned how to set up a C++ project with a toolchain
and how to build a simple C++ binary. Furthermore, you have understood that the
build can be parameterized by specifying supported configuration variables.

## Stage 2: Library dependencies and project settings

This stage extends the previous one by adding a library target as a dependency
to the binary target and provide project-wide settings (e.g., compile flags)
that are applied to all targets. This stage is structured as follows:

``` plaintext
examples
└── cpp-tutorial
    └── stage2
        ├── ROOT
        ├── repos.in.json
        ├── etc
        │   └── settings
        │       └── CC/TARGETS
        └── src
            ├── greet.cpp
            ├── greet.hpp
            ├── helloworld.cpp
            └── TARGETS
```

Please step into the workspace root of stage 2:

``` sh
$ cd cpp-tutorial/stage2
```

### Add library dependencies

Let's add a new target to the project: the library `libgreet`, which is a
dependency of the `helloworld` binary. Please have a look at the target
description in file `src/TARGETS`:

``` jsonnet
{
  helloworld: {
    type: @'rules//CC:binary',
    name: ['helloworld'],
    srcs: ['helloworld.cpp'],
    'private-deps': ['libgreet'],   // same as @'//src:libgreet'
  },

  libgreet: {
    type: @'rules//CC:library',
    name: ['greet'],    // produces libgreet.a
    hdrs: ['greet.hpp'],
    srcs: ['greet.cpp'],
    stage: ['greet'],   // prefix used for public headers
  },
}
```

The target `libgreet` uses the rule [`//CC:library`](https://github.com/just-buildsystem/rules-cc#rule-cc-library)
from binding `rules` to produce the static library `libgreet.a`. To link the
`helloworld` binary with this library, you only need to add the library target
to the private dependencies specified in the field `private-deps`.

Build the `helloworld` binary again.

``` sh
$ jst build src helloworld
```

You can see that build succeeds with now four processed actions in total.

Of course you can also build the `libgreet` target separately.

``` sh
$ jst build src libgreet -s
```

> Note: the flag `-s` will also print *runfiles*, e.g., public headers.

You should see that only two actions were processed, all served from cache, as
they were already implicitly executed when building `helloworld`.

``` plaintext
INFO: Processed 2 actions, 2 cache hits.
```

### Define project-wide settings

To define project-wide settings (e.g., compile flags) that are applied to all
targets, we need add two new repositories to the `repos.in.json` file:

``` jsonc
{ // ...
  "repositories": {
    "etc/settings": {
      "repository": {"type": "file", "path": "etc/settings"}
    },
    "settings": {
      "repository": "toolchain",
      "target_root": "etc/settings",
      "rule_root": "toolchain",
      "bindings": {"base": "toolchain"}
    },
    "stage2": {
      "repository": {"type": "file", "path": "."},
      "bindings": {"rules": "settings"}
    }
  }
}
```

Those two repositories are:

1. `"etc/settings"` is a *file repository* pointing to *path* `etc/settings`,
    which contains the actual settings in `TARGETS` files.
2. `"settings"` is a repository based on the previously imported `"toolchain"`
    repository. Additionally, its `target_root` is replaced to pickup the
    `TARGETS` files from the `"etc/settings"` *file repository*. However, with
    the new `target_root`, we cannot access the toolchain's targets anymore,
    which are needed for inheriting the toolchain settings. Therefore, we define
    the binding `"base"` as a way to reference targets from the toolchain.

Finally, the *main repository* `"stage2"` can now use the new `"settings"`
repository as a binding for rules.

Now run `jst-lock` to apply these changes to the `repos.json` lock-file.

The actual C/C++ settings are defined in target `defaults` of submodule `CC` in
the settings target root: `etc/settings/CC/TARGETS`.

``` jsonnet
// Project flags, common for C and C++
local common_flags = ['-Wall', '-Werror', '-pedantic'];

{
  defaults: {
    type: @'//CC:defaults',
    base: [@'base//CC:defaults'],
    ADD_CFLAGS: ['-std=c11'] + common_flags,
    ADD_CXXFLAGS: ['-std=c++14'] + common_flags,
  },
}
```

This target uses the rule [`//CC:defaults`](https://github.com/just-buildsystem/rules-cc#rule-cc-defaults)
to inherit the toolchain settings from `base//CC:defaults` and extend it by
project-wide compile flags using the fields `ADD_CFLAGS` and `ADD_CXXFLAGS`.

Now build the `helloworld` target again with the verbose flag `-v`.

``` sh
$ jst build src helloworld -v
```

The produced output reveals that the compile flags `[..., "-std=c++14", "-Wall",
"-Werror", "-pedantic" ,...]` have been correctly added to all C++ compiler
command lines.

Now try modifying those flags yourself and see if they are
correctly propagated throughout your build.

### Summary: Stage 2

In the second stage of this tutorial, you learned how to model dependencies
between C++ targets and how to configure project-wide settings that apply
equally to all C/C++ targets within the project.

## Stage 3: Add test targets

Testing is a crucial part of every software development project. Similar to
compiling and linking, also tests should never needlessly rerun if they are not
affected by a change. In this stage, you will learn how to add binary tests and
shell tests to your project. The structure of this stage is as follows:

``` plaintext
examples
└── cpp-tutorial
    └── stage3
        ├── ROOT
        ├── repos.in.json
        ├── etc
        │   └── settings
        │       ├── CC/TARGETS
        │       └── shell/TARGETS
        ├── src/
        └── test
            ├── TARGETS
            └── test_libgreet.cpp
```

Before we begin, please step into the workspace root for stage 3.

``` sh
$ cd cpp-tutorial/stage3
```

### Test concept

Tests can be added by writing specific test targets. Like most other targets,
also test targets are essentially *build targets*, i.e., they run a build
action to produce an artifact. In this case, the artifact that is being built is
a test report. Consequently, the subcommand `build` is used to run tests.

#### Failing tests

Test targets are allowed to fail, i.e., their failure will not abort the build
process. Instead, failing test targets will produce a warning message and their
reports will be separately listed as *failed artifacts* at the end of the build
process.

#### Tainting

All test targets are implicitly *tainted* with the string `"test"`. The way
*tainting* works is that all targets that depend on a test target must be
tainted with the string `"test"` as well (and possibly even more strings). The
target analysis will fail if this constraint is violated. It is up to the users
to define with what strings their targets are tainted with.

### Supported test rules

The [C/C++ rule set](https://github.com/just-buildsystem/rules-cc) supports two
types of test rules:

1. *Binary test* rule [`//CC/test:test`](https://github.com/just-buildsystem/rules-cc#rule-cctest-test) for testing native libraries
2. *Shell test* rule [`//shell/test:script`](https://github.com/just-buildsystem/rules-cc#rule-shelltest-script) for running shell tests

The test report produced with these rules will contain the artifacts: `result`,
`stderr`, `stdout`, `time-start`, and `time-stop`.

### Add binary tests

Let's create a binary test for the `libgreet` library in `test/TARGETS`.

``` jsonnet
{ // ...
  test_libgreet: {
    type: @'rules//CC/test:test',
    name: ['libgreet'],
    srcs: ['test_libgreet.cpp'],
    'private-deps': [
      @'//src:libgreet',
    ],
  },
  // ...
}
```

The test binary target is defined like any other typical binary target. The
major difference is its output. Instead of producing a binary, it produces a
test report from running the binary test.

Now build the test report and print the `stdout` artifact.

``` sh
$ jst build test test_libgreet -P stdout
```

From the output you should see that the test was run successfully.

> Note: you need to create a module in `etc/settings/CC/test/TARGETS` to run
> the toolchain's test launchers.

Now try to change the `test_libgreet.cpp` source file to produce a test failure
and trigger a rebuild. You should see a warning about *failed artifacts* that
were produced: the test report of a failed test.

### Add shell tests

Let's create the shell test for the `helloworld` binary in `test/TARGETS`.

``` jsonnet
{ // ...
  test_helloworld: {
    type: @'rules//shell/test:script',
    name: ['helloworld'],
    test: ['test_helloworld.sh'],
    deps: [
      @'//src:helloworld',
    ],
  },
  // ...
}
```

Shell tests contain no sources but a shell script, specified via field `test`.
The shebang of this script will be ignored. Instead, it will be run with the
default shell configured in `etc/settings/shell/TARGETS` (typically `/bin/sh`).
For building the report, the test has access to the artifacts produced by the
targets listed as dependencies in field `deps`, the binary target
`//src:helloworld`.

Now build the test report and print the `stdout` artifact.

``` sh
$ jst build test test_helloworld -P stdout
```

From the output you should see that the test was run successfully.

> Note: you need to create a module in `etc/settings/shell/test/TARGETS` to run
> the toolchain's test launchers.

### Define a test suite

To not run every test individually, the rule [`//test:suite`](https://github.com/just-buildsystem/rules-cc/tree/master#rule-test-suite)
can be used to combine multiple tests in a single target, the test suite.

Let's create the test suite target `ALL`:

``` jsonnet
{
  ALL: {
    type: @'rules//test:suite',
    deps: [
      @':test_libgreet',
      @':test_helloworld',
    ],
    stage: ['test'],
  },
  // ...
}
```

It collects the test reports from `test_libgreet` and `test_helloworld` and
stages them to the output directory `test`.

``` sh
$ jst build test ALL
```

Failing tests will be reported as a separate warning message by `jst`. If no
failing artifacts were reported, then all tests ran successfully.

Also with the test suite, you can still directly print the `stdout` artifact of
a test report by specifying its path, e.g., via `-P test/libgreet/stdout`.

### Summary: Stage 3

In this tutorial stage, you learned about the general test concept used by
`jst`. Furthermore, it was shown how to use the available C/C++ test rules to
define binary tests, shell tests, and test suites.

## Stage 4: External, export, and install targets

In this stage, we want to demonstrate how external projects can be imported. As
an example, we will import [`fmtlib`](https://github.com/fmtlib/fmt) and [`googletest`](https://github.com/google/googletest).
Furthermore, we will show how your project's public targets can be declared as
*export targets*, a requirement if others want to import your project. Finally,
we present *install targets* that conveniently include the required artifacts of
transient dependencies. The structure of this stage is as follows:

``` plaintext
examples
└── cpp-tutorial
    └── stage4
        ├── ROOT
        ├── TARGETS
        ├── gtest.TARGETS
        ├── repos.in.json
        ├── etc/
        ├── src/
        └── test/
```

Please step into the workspace root for stage 4.

``` sh
$ cd cpp-tutorial/stage4
```

When importing targets from external projects, there are two different scenarios
to consider:

1. Importing from an existing `jst` project
2. Importing from a non-`jst` project

### Import fmtlib from a jst project

Importing from an existing `jst` project is straight-forward. All you need to do
is to define a new import in the `repos.in.json`:

``` jsonc
{ // ...
  "imports": [
    {/* toolchain */},
    {
      "source": "git",
      "branch": "v1.4.2",
      "url": "https://github.com/just-buildsystem/justbuild.git",
      "repos": [{
        "repo": "fmt",
        "alias": "fmtlib",
        "map": {"rules": "toolchain"}
      }]
    }
  ],
  // ...
}
```

In this example, we import the repository [`fmt`](https://github.com/just-buildsystem/justbuild/blob/v1.4.2/etc/repos.json#L215) from the Justbuild tag `v1.4.2`.
The local alias name for this repository is `fmtlib` and we additionally
specify a `map` to replace their `rules` by our `toolchain`.

> Note: we deliberately not specify `settings` as rules, because we do not
> want to force the local project settings on the imported project.

Finally, we can add the new `fmtlib` repository to the bindings of the `stage3`
main repository and run `jst-lock`.

Now the `libgreet` target can access the top-level target `fmt-lib` from
`fmtlib` via `fmtlib//:fmt-lib`.

``` jsonnet
{ // ...
  libgreet: {
    type: @'rules//CC:library',
    arguments_config: ['BUILD_SHARED', 'USE_FMTLIB'],
    name: ['greet'],    // produces libgreet.[a|so]
    shared:
      if jst.env('BUILD_SHARED') then ['yes'],
    'private-cflags':
      if jst.env('USE_FMTLIB') then ['-DUSE_FMTLIB'],
    'private-deps':
      if jst.env('USE_FMTLIB') then [@'fmtlib//:fmt-lib'],
    hdrs: ['greet.hpp'],
    srcs: ['greet.cpp'],
    stage: ['greet'],   // prefix used for public headers
  }
}
```

The target `libgreet` was modified to honor the configuration variable
`USE_FMTLIB` (see `arguments_config`). If set, additional `private-cflags` and
`private-deps` are specified. Try building `helloworld` with this variable:

``` sh
$ jst build src helloworld -D'{"USE_FMTLIB":true}' -v
```

You can see that `libfmt.a` was built and added to the final command for linking
`helloworld`.

### Import gtest from a CMake project

Importing from a non-`jst` project requires a little more work:

1. Define a new import to fetch the externals sources
2. Provide a target description for building external code

Let's start by defining a new import for fetching the external sources:

``` jsonc
{ // ...
  "imports": [
    {/* toolchain */},
    {/* fmtlib */},
    {
      "source": "git",
      "branch": "v1.14.0",
      "url": "https://github.com/google/googletest.git",
      "as plain": true,
      "repos": [{"alias": "gtest_sources"}]
    }
  ],
  // ...
}
```

In the above example, we imported the sources from the [googletest](https://github.com/google/googletest.git) repository
version `v1.14.0`. Due to it not being a `jst` repository (missing lock-file
`repos.json`), we have to import it `as plain`. The local alias name for this
repository is `gtest_sources`.

To provide the target description `gtest.TARGETS` we reuse the target root from
the main repository `stage4`. In combination with `gtest_sources`, we can now
create the new repository called `gtest`.

``` jsonc
{ // ...
  "repositories": {
    // ...
    "gtest": {
      "repository": "gtest_sources",
      "target_root": "stage4",
      "target_file_name": "gtest.TARGETS",
      "bindings": {"rules": "toolchain"}
    }
  }
}
```

Similar to `fmtlib`, we also define a binding from `rules` to our `toolchain`.
Finally, we can add the new `gtest` repository to the bindings of the `stage4`
main repository and run `jst-lock`.

The last missing piece is providing the target description in file
`gtest.TARGETS`.

``` jsonnet
{
  gtest_main: {
    type: @'rules//CC/foreign/cmake:library',
    arguments_config: ['DEBUG'],
    name: ['gtest_main'],
    project: [jst.tree('.')],
    defines: [
      'BUILD_GMOCK=OFF',
      'gtest_force_shared_crt=OFF',
      'CMAKE_BUILD_TYPE=' + (if jst.env('DEBUG')
                             then 'Debug' else 'Release'),
    ],
    out_libs: ['libgtest_main.a', 'libgtest.a'],
    out_hdr_dirs: ['gtest'],
    'pkg-config': ['gtest.pc'],
  },
}
```

Due to googletest being a CMake project, the rule [`//CC/foreign/cmake:library`](https://github.com/just-buildsystem/rules-cc/tree/master#rule-ccforeigncmake-library) is used.
As a project directory, we specify the entire local tree `"."`. Depending on
the `DEBUG` variable, slightly different CMake defines are used. The output
artifacts collected for this target are: the libraries `libgreet_main.a` and
`libgreet.a` (order matters), the header directory `gtest`, and the optional
pkg-config file `gtest.pc`. Please be aware that CMake must be installed for
building this target.

Now add the `gtest//:gtest_main` to the `test_libgreet` target and run the test:

``` sh
$ jst build test test_libgreet -P stdout
```

From the `stdout` artifact of the produced test report, you can see that `gtest`
was used to run the test.

### Define public export targets

To make you project ready for being imported by other `jst` projects, it is
recommended to define *export targets*. Export targets evaluate the public
targets of your project and make them eligible for [target-level caching](../../doc/concepts/target-cache.md),
a technique to cut off entire subgraphs of the action graphs, which is required
to lower the action graph size for large-scale projects.

Please see the top-level `TARGETS` file.

``` jsonnet
{
  helloworld: {
    type: 'export',
    doc: ['Exported helloworld binary'],
    flexible_config: config_vars,
    config_doc: config_doc,
    target: @'//src:helloworld',
  },

  libgreet: {
    type: 'export',
    doc: ['Exported libgreet library'],
    flexible_config: config_vars,
    config_doc: config_doc,
    target: @'//src:libgreet',
  },
  // ...
}
```

Export targets use the [built-in rule `export`](../../doc/concepts/built-in-rules.md#export)
to restrict the configuration variables to those specified in the field
`flexible_config` before evaluating the target specified in the field `target`.
Both export targets support the same configuration variables and documentation.
Hence, those were defined in common local variables `config_vars` and
`config_doc`. Finally, both export targets evaluate their respective targets
`helloworld` and `libgreet` in submodule `src`.

> Note: If an export target is declared, it should be used to replace all
> references to the evaluated `target`, also in internal dependencies. The
> evaluated target should never be used directly.

You can use the subcommand `describe` to access the variable documentation of
export targets:

``` sh
$ jst describe helloworld
```

Building is still the same, with the difference that unsupported variables will
not be honored when computing the action graph.

### Provide convenience install targets

For development, obtaining only the target's main artifacts when building is
fine. However, for deployment, you probably want to define *install targets*
that also consider required artifacts (e.g., shared libraries, public headers)
of their transient dependencies.

Please see the top-level `TARGETS` file.

``` jsonnet
{ // ...
  APPS: {
    type: @'rules//CC:install-with-deps',
    targets: ['helloworld'],
  },

  LIBS: {
    type: @'rules//CC:install-with-deps',
    targets: ['libgreet'],
  },

  TESTS: {
    type: 'install',
    tainted: ['test'],
    deps: [
      @'//test:ALL',
    ],
  },
  // ...
}
```

The install targets `APPS` and `LIBS` use the rule [`//CC:install-with-deps`](https://github.com/just-buildsystem/rules-cc#rule-cc-install-with-deps)
to install the export targets listed in the field `targets` including their
required transient dependencies.

For instance, building target `APPS` (`helloworld`) with `BUILD_SHARED` will
install the binary `helloworld` and the shared library `libgreet.so`, because it
is required for running the binary.

``` sh
$ jst build APPS -D'{"BUILD_SHARED":true}'
```

Similarly, building target `LIBS` (`libgreet`) with `USE_FMTLIB` will
install the static library `libgreet.a`, its public headers, and its dependency
`libfmt.a`, because those are required for including and linking the library.

``` sh
$ jst build LIBS -D'{"USE_FMTLIB":true}'
```

Finally, the top-level target `TESTS` uses the [built-in rule `install`](../../doc/concepts/built-in-rules.md#install)
to obtain the test reports from test suites in submodules (`//test:ALL`). Please
note that it must be `tainted` with string `"test"`, which all test targets are
implicitly tainted with (see section [Tainting](#tainting)).

``` sh
$ jst build TESTS
```

You can still directly access the test reports by specifying their path, e.g.,
`-P test/helloworld/stdout`.

### Summary: Stage 4

In the last stage of this tutorial, you learned about how to import targets from
other `jst` and non-`jst` projects alike. Furthermore, you learned how to
prepare your project for being imported by others, by declaring your project's
public targets as *export targets*. And finally, you now know how to define
*install targets*, which conveniently install main artifacts of a target
alongside their required artifacts from transient dependencies.

## Company setup

In a company setup you may not have clean internet available. In that case, you
must provide the required repositories (for rules, toolchain, etc.) via local
mirrors and set up a redirection.

`jst` fetches repositories from `https://` and `ftp://` directly, while for
`ssh://` it shells out to `git`. Furthermore, `jst-lock` always shells out to
`git`. Consequently, redirections have to be configured for both scenarios.

To configure redirections for repositories directly fetched by `jst`, provide
the file `~/.just-local.json` with content analogous to:

``` json
{
  "local mirrors": {
    "https://gitee.com/justbuild/toolchains-cc.git": [
      "https://local-mirror1/justbuild/toolchains-cc.git",
      "ssh://git@local-mirror2/justbuild/toolchains-cc.git"
    ]
  }
}
```

> Note: for more details, please see section [Additional mirrors in just local](../../doc/concepts/alternative-mirrors.md#additional-mirrors-in-the-just-local-specification).

To configure redirections for repositories fetched by shelling out to `git`,
provide a Git config, e.g., `~/.gitconfig` with content analogous to:

``` plaintext
[url "https://local-mirror1/justbuild/toolchains-cc.git"]
    insteadOf = https://gitee.com/justbuild/toolchains-cc.git
```
