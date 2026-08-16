Cross compiling and testing cross-compiled targets
==================================================

So far, we were always building for the platform on which we were
building. There are, however, good reasons to build on a different
one. For example, the other platform could be faster (common theme
when developing for embedded devices), cheaper, or simply available
in larger quantities. The standard solution for these kinds of
situations is cross compiling: the binary is completely built on
one platform, while being intended to run on a different one.

Cross compiling using the CC rules
----------------------------------

The `C` and `C++` rules that come with the `jst-build` repository
already have cross compiling built into the toolchain
definitions for the `"gnu"` and `"clang"` compiler families; as
different compilers expect different ways to be told about the target
architecture, cross compiling is not supported for the `"unknown"`
compiler family.

First, ensure that the required tools and libraries (which also have
to be available for the target architecture) for cross compiling
are installed. Depending on the distribution used, this can be
spread over several packages, often grouped by the architecture being
cross compiled to.

Once the required packages are installed, usage is quite straightforward.
Let's start a new project in a clean working directory.

``` sh
$ touch ROOT
```

We create a file `repos.template.json` specifying the one local repository.

``` {.json srcname="repos.template.json"}
{
  "repositories": {
    "": {
      "repository": {
        "type": "file",
        "path": "."
      },
      "bindings": {"rules": "toolchain"}
    }
  }
}
```

The actual `toolchain` is imported via `jst-import-git`.

``` sh
$ jst-import-git -C repos.template.json -b system --as toolchain https://github.com/jst-build/toolchains-cc > repos.json
```

Let's have `main.cpp` be the canonical _Hello World_ program.

``` {.cpp srcname="main.cpp"}
#include <iostream>

int main() {
  std::cout << "Hello world!\n";
  return 0;
}
```

Then, a `TARGETS` file describing a simple binary.

``` {.jsonnet srcname="TARGETS"}
{
  helloworld: {
    type: @'rules//CC:binary',
    name: ['helloworld'],
    srcs: ['main.cpp'],
  }
}
```

As mentioned in the introduction, we need to specify `TOOLCHAIN_CONFIG`,
`OS`, and `ARCH`. So the canonical building for host looks something like
the following.

``` sh
$ jst build -D '{"TOOLCHAIN_CONFIG": {"FAMILY": "gnu"}, "OS": "linux", "ARCH": "x86_64"}'
INFO: Found 4 repositories involved
INFO: Requested target '""//:helloworld' with config: {
        "ARCH": "x86_64",
        "OS": "linux",
        "TOOLCHAIN_CONFIG": {
          "FAMILY": "gnu"
        }
      }
INFO: Discovered 2 actions, 0 tree overlays, 1 trees, 0 blobs
INFO: Processed 2 actions, 0 cache hits.
INFO: Artifacts built, logical paths are:
        helloworld [0d5754a83c7c787b1c4dd717c8588ecef203fb72:16992:x]
$
```

To cross compile, we simply add `TARGET_ARCH`.

``` sh
$ jst build -D '{"TOOLCHAIN_CONFIG": {"FAMILY": "gnu"}, "OS": "linux", "ARCH": "x86_64", "TARGET_ARCH": "arm64"}'
INFO: Found 4 repositories involved
INFO: Requested target '""//:helloworld' with config: {
        "ARCH": "x86_64",
        "OS": "linux",
        "TARGET_ARCH": "arm64",
        "TOOLCHAIN_CONFIG": {
          "FAMILY": "gnu"
        }
      }
INFO: Discovered 2 actions, 0 tree overlays, 1 trees, 0 blobs
INFO: Processed 2 actions, 0 cache hits.
INFO: Artifacts built, logical paths are:
        helloworld [b45459ea3dd36c7531756a4de9aaefd6af30e417:9856:x]
$
```

To inspect the different command lines for native and cross compilation,
we can use `jst analyse`.

``` sh
$ jst analyse -D '{"TOOLCHAIN_CONFIG": {"FAMILY": "gnu"}, "OS": "linux", "ARCH": "x86_64"}' --dump-actions -
$ jst analyse -D '{"TOOLCHAIN_CONFIG": {"FAMILY": "gnu"}, "OS": "linux", "ARCH": "x86_64", "TARGET_ARCH": "arm64"}' --dump-actions -
$ jst analyse -D '{"TOOLCHAIN_CONFIG": {"FAMILY": "clang"}, "OS": "linux", "ARCH": "x86_64"}' --dump-actions -
$ jst analyse -D '{"TOOLCHAIN_CONFIG": {"FAMILY": "clang"}, "OS": "linux", "ARCH": "x86_64", "TARGET_ARCH": "arm64"}' --dump-actions -
```


Testing in the presence of cross compiling
------------------------------------------

To understand testing in the presence of cross compiling, let's
walk through a simple example. We create a basic test in the subdirectory `test`

``` sh
$ mkdir -p ./test
```

by adding a file `test/TARGETS` containing

``` {.jsonnet srcname="test/TARGETS"}
{
  basic: {
    type: @'rules//shell/test:script',
    name: ['basic'],
    test: ['basic.sh'],
    deps: [@'//:helloworld'],
  },

  'basic.sh': {
    type: 'file_gen',
    name: 'basic.sh',
    data: './helloworld | grep -i world',
  },
}
```

Now, if we try to run the test by simply specifying the target architecture,
we find that the binary to be tested is still only built for host.

``` sh
$ jst analyse --dump-targets - -D '{"TOOLCHAIN_CONFIG": {"FAMILY": "gnu"}, "OS": "linux", "ARCH": "x86_64", "TARGET_ARCH": "arm64"}' test basic
INFO: Found 4 repositories involved
INFO: Requested target '""//test:basic' with config: {
        "TOOLCHAIN_CONFIG": {
          "FAMILY": "gnu"
        },
        "OS": "linux",
        "ARCH": "x86_64",
        "TARGET_ARCH": "arm64"
      }
INFO: Result of target ['""//test:basic',{"ARCH":"x86_64","OS":"linux","TARGET_ARCH":"arm64","TOOLCHAIN_CONFIG":{"FAMILY":"gnu"}}]: {
        "artifacts": {
          "pwd": {"data":{"id":"a3bcca44cc1b307cd7e560c0264f011f1c0069ca124aed6a75919e812725a763","path":"pwd"},"type":"ACTION"},
          "result": {"data":{"id":"a3bcca44cc1b307cd7e560c0264f011f1c0069ca124aed6a75919e812725a763","path":"result"},"type":"ACTION"},
          "stderr": {"data":{"id":"a3bcca44cc1b307cd7e560c0264f011f1c0069ca124aed6a75919e812725a763","path":"stderr"},"type":"ACTION"},
          "stdout": {"data":{"id":"a3bcca44cc1b307cd7e560c0264f011f1c0069ca124aed6a75919e812725a763","path":"stdout"},"type":"ACTION"},
          "time-start": {"data":{"id":"a3bcca44cc1b307cd7e560c0264f011f1c0069ca124aed6a75919e812725a763","path":"time-start"},"type":"ACTION"},
          "time-stop": {"data":{"id":"a3bcca44cc1b307cd7e560c0264f011f1c0069ca124aed6a75919e812725a763","path":"time-stop"},"type":"ACTION"}
        },
        "provides": {
          "lint": [
          ]
        },
        "runfiles": {
          "basic": {"data":{"id":"6337a73272400d3d4fc32cccd334c5d852f4577c04036acd8d2340fdab2284bb"},"type":"TREE"}
        }
      }
INFO: List of analysed targets:
{
  "@": {
    "": {
      "": {
        "helloworld": [{"ADD_CFLAGS":null,"ADD_CXXFLAGS":null,"ADD_LDFLAGS":null,"ARCH":"x86_64","BUILD_POSITION_INDEPENDENT":null,"CC":null,"CFLAGS":null,"CXX":null,"CXXFLAGS":null,"DEBUG":null,"DWP":null,"ENV":null,"FORCE_DISABLE_INCLUDE_SCAN":null,"HOST_ARCH":null,"LDFLAGS":null,"LINT":null,"TARGET_ARCH":"x86_64","TOOLCHAIN_CONFIG":{"FAMILY":"gnu"}}]
      },
      "test": {
        "basic": [{"ADD_CFLAGS":null,"ADD_CXXFLAGS":null,"ADD_LDFLAGS":null,"ARCH":"x86_64","ARCH_DISPATCH":null,"BUILD_POSITION_INDEPENDENT":null,"CC":null,"CFLAGS":null,"CXX":null,"CXXFLAGS":null,"DEBUG":null,"DWP":null,"ENV":null,"FORCE_DISABLE_INCLUDE_SCAN":null,"HOST_ARCH":null,"LDFLAGS":null,"LINT":null,"RUNS_PER_TEST":null,"TARGET_ARCH":"arm64","TEST_ENV":null,"TEST_SUMMARY_EXECUTION_PROPERTIES":null,"TIMEOUT_SCALE":null,"TOOLCHAIN_CONFIG":{"FAMILY":"gnu"}}],
        "basic.sh": [{}]
      }
    },
    "toolchain": {
      "CC": {
        "defaults": [{"ARCH":"x86_64","DEBUG":null,"FORCE_DISABLE_INCLUDE_SCAN":null,"HOST_ARCH":null,"TARGET_ARCH":"x86_64","TOOLCHAIN_CONFIG":{"FAMILY":"gnu"}}],
        "gcc": [{"ARCH":"x86_64","DEBUG":null,"FORCE_DISABLE_INCLUDE_SCAN":null,"HOST_ARCH":null,"TARGET_ARCH":"x86_64","TOOLCHAIN_CONFIG":{"FAMILY":"gnu"}}]
      },
      "shell": {
        "defaults": [{"ARCH":"x86_64","HOST_ARCH":null,"TARGET_ARCH":"arm64","TOOLCHAIN_CONFIG":{"FAMILY":"gnu"}}]
      }
    }
  }
}
INFO: Target tainted ["test"].
$
```

The reason is that a test actually has to run the created binary
and that requires a build environment of the target architecture.
So, if not being told how to obtain such an environment, they carry
out the test in the best manner they can, i.e., by transitioning
everything to host. So, in order to properly test the cross-compiled
binary, we need to do two things.

- We need to set up remote execution on the correct architecture,
  either by buying the appropriate hardware or by running an emulator.
- We need to tell *jst-build* how to reach that endpoint.

To continue the example, let's say we set up an `arm64` machine,
e.g., a Raspberry Pi, in the local network. On that machine, we can
simply run a single-node execution service using `jst execute`;
note that the `jst_backend` binary used there has to be an `arm64` binary,
e.g., obtained by cross compiling.

The next step is to tell *jst-build* how to reach that machine;
as we only want to use it for certain actions, we can't simply
set it as the (default) remote-execution endpoint (specified by the
`-r` option). Instead, we create a file `dispatch.json`.

``` {.jsonc srcname="dispatch.json"}
[[{"runner": "arm64-worker"}, "10.204.62.67:9999"]]
```

This file contains a list of pairs (lists of length 2), with the
first entry a map of remote-execution properties and the second entry
a remote-execution endpoint. For each action, if the remote-execution
properties of that action match the first component of a pair, the
second component of the first matching pair is taken as remote-execution
endpoint instead of the default endpoint specified on the command
line. Here "matching" means that all values in the domain of the
map have the same value in the remote-execution properties of the
action (in particular, `{}` matches everything); so more specific
dispatches have to be specified earlier in the list. In our case,
we have a single endpoint in our private network that we should
use whenever the property `"runner"` has value `"arm64-worker"`.
The IP/port specification might differ in your setup. The path to
this file is passed by the `--endpoint-configuration` option.

Next, we have to tell the test rule that we actually do have a
runner for the `arm64` architecture. The rule expects this in the
`ARCH_DISPATCH` configuration variable that you might have seen in
the list of analysed targets earlier. It is expected to be a map
from architectures to remote-execution properties ensuring the action
will be run on that architecture. If the rule finds a non-empty
entry for the specified target architecture, it assumes a runner is
available and will run the test action with those properties; for
the compilation steps, cross compiling is still used. In our case,
we set `ARCH_DISPATCH` to `{"arm64": {"runner": "arm64-worker"}}`.

Finally, we have to provide the credentials needed for mutual
authentication with the remote-execution endpoint by setting `--tls-*`
options appropriately. `jst_backend` assumes that the same credentials
can be used for all remote-execution endpoints involved. In our example
we're building locally (where the build process starts the actions
itself) which does not require any credentials; nevertheless, it
still accepts any credentials provided.

``` sh
$ jst build -D '{"TOOLCHAIN_CONFIG": {"FAMILY": "gnu"}, "OS": "linux", "ARCH": "x86_64", "TARGET_ARCH": "arm64", "ARCH_DISPATCH": {"arm64": {"runner": "arm64-worker"}}}' --endpoint-configuration dispatch.json --tls-ca-cert ca.crt --tls-client-cert client.crt --tls-client-key client.key test basic
INFO: Found 4 repositories involved
INFO: Requested target '""//test:basic' with config: {
        "ARCH": "x86_64",
        "ARCH_DISPATCH": {
          "arm64": {
            "runner":"arm64-worker"
          }
        },
        "OS": "linux",
        "TARGET_ARCH": "arm64",
        "TOOLCHAIN_CONFIG": {
          "FAMILY":"gnu"
        }
      }
INFO: Target tainted ["test"].
INFO: Discovered 3 actions, 0 tree overlays, 3 trees, 3 blobs
INFO: Processed 3 actions, 3 cache hits.
INFO: Artifacts built, logical paths are:
        pwd [da762809b16a6b476c6d45f67695769bfd6fdb01:160:f]
        result [7ef22e9a431ad0272713b71fdc8794016c8ef12f:5:f]
        stderr [e69de29bb2d1d6434b8b29ae775ad8c2e48c5391:0:f]
        stdout [cd0875583aabe89ee197ea133980a9085d08e497:13:f]
        time-start [32fdb8269cd3a5a699eeeafd1be565308167c110:11:f]
        time-stop [32fdb8269cd3a5a699eeeafd1be565308167c110:11:f]
      (1 runfiles omitted.)
INFO: Target tainted ["test"].
$
```

The resulting command line might look complicated, but the
authentication-related options, as well as the dispatch-related
options (including setting `ARGUMENTS_DISPATCH` via `-D`) can simply
be set in the `"jst args"` entry of the `.jstrc` file.

When inspecting the result, we can use `jst install-cas` as usual,
without any special arguments. Whenever dispatching an action to
a non-default endpoint, `jst_backend` will take care of syncing back the
artifacts to the default CAS.

Testing a matrix of configurations
----------------------------------

This example also shows that a single test can be run in a variety
of configurations.

- Different compilers can be used.
- We can build and test for the host architecture, or cross-compile
  for a different architecture and run the tests there.

Often, all possible combinations need to be tested, especially
for projects that are meant to be portable. Doing this by hand
or manually maintaining all combinations as a list of CI tasks is
cumbersome and error-prone. Therefore, there is a rule
`@'rules//test:matrix'` that can take care of handling all
configuration combinations in a single build. The fields are the same
as those of a test suite.

``` {.jsonnet srcname="test/TARGETS"}
...
matrix: {
  type: @'rules//test:matrix',
  deps: ['basic']
}
...
```

If run without special configuration, it also behaves like a test suite.

``` shell
$ jst build test matrix
INFO: Found 4 repositories involved
INFO: Requested target '""//test:matrix' with config: {}
INFO: Target tainted ["test"].
INFO: Discovered 3 actions, 0 tree overlays, 3 trees, 3 blobs
INFO: Processed 3 actions, 0 cache hits.
INFO: Artifacts built, logical paths are:
        basic [4a75cd3ff7b5b6552c03d74580c1d336f3f38a4e:208:t]
INFO: Target tainted ["test"].
```

But we can instruct it via the configuration variable `TEST_MATRIX` to run
its `"deps"` in a product of configurations, with `TEST_MATRIX` cleared there.

``` shell
$ jst describe test matrix
INFO: Found 4 repositories involved
['""//test:matrix',{}] is defined by user-defined rule 'toolchain//test:matrix'.

 | Given a group of tests, build them in a variety of configurations.
 | 
 | The configuration variable TEST_MATRIX is expected to be a map with
 | each value being a map itself. Sequentially for each key, all possible
 | values of the associated map are tried and staged to the appropriate
 | key. Thus, the tests in "deps" are built in an exponential number of
 | configurations.
 | 
 | If TEST_MATRIX is unset, {} will be assumed, i.e., all the "deps" will
 | be built precisely once, in the current configuration. In this way,
 | the "matrix" rule can be used instead of the "suite" rule to allow
 | user-defined configuration matrices, dispatching over parameters for
 | dependencies (e.g., the toolchain).
 String fields
 - "stage"
   | The logical location this test suite is to be placed.
   | Individual entries will be joined with "/".
 Target fields
 - "deps"
   | The targets that suite is composed of.
 Variables taken from the configuration
 - "TEST_MATRIX"
   | Map describing the dimensions of the matrix to run the tests for.
   | 
   | Keys are config variables. The value for each key has to be a map
   | mapping the stage name to the corresponding value for that config
   | variable.
 Result
 - Artifacts
   | The disjoint union of the runfiles of the "deps" targets,
   | evaluated and staged as requested by TEST_MATRIX and finally
   | staged to the location given by "stage".
 - Runfiles
   | Same as artifacts.
```

Typically, one sets the matrix of desired configurations in a `"configure"`
target next to the matrix target which then acts as the main entry point.

``` {.jsonnet srcname="test/TARGETS"}
...
ALL: {
  type: 'configure',
  tainted: ['test'],
  target: 'matrix',
  config: {
    TEST_MATRIX: {
      TARGET_ARCH: {x86: 'x86_64', arm: 'arm64'},
      TOOLCHAIN_CONFIG: {gcc: {FAMILY: 'gnu'}, clang: {FAMILY: 'clang'}},
    }
  }
}
...
```

Each key in `"TEST_MATRIX"` is a configuration variable where
different values should be taken into account. For each such variable
we specify in another map the values that should be tried, keyed
by the path where the tests should be staged; obviously, if we run
the same test in many configurations we need to stage the various
test results to different locations.

Now, in a single invocation, we can run our test in all four relevant
configurations. Still, credentials and dispatch information need
to be provided.

``` shell
$ jst build -D '{"OS": "linux", "ARCH": "x86_64", "ARCH_DISPATCH": {"arm64": {"runner": "arm64-worker"}}}' --endpoint-configuration dispatch.json --tls-ca-cert ca.crt --tls-client-cert client.crt --tls-client-key client.key test ALL
INFO: Found 4 repositories involved
INFO: Requested target '""//test:ALL' with config: {
        "ARCH": "x86_64",
        "ARCH_DISPATCH": {
          "arm64": {
            "runner": "arm64-worker"
          }
        },
        "OS": "linux"
      }
INFO: Target tainted ["test"].
INFO: Discovered 12 actions, 0 tree overlays, 9 trees, 3 blobs
INFO: Processed 12 actions, 6 cache hits.
INFO: Artifacts built, logical paths are:
        clang/arm/basic [6503faa5935250fe50ebc2f00dbdd9dbf592fd3a:208:t]
        clang/x86/basic [90ead7c3b83f6902c3b0b032d63d734e0ea55a0b:208:t]
        gcc/arm/basic [24c3f2de7e920ad294edba63da596d810a2c350a:208:t]
        gcc/x86/basic [4a75cd3ff7b5b6552c03d74580c1d336f3f38a4e:208:t]
INFO: Target tainted ["test"].
```
