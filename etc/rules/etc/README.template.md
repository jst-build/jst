# C/C++ rules for the `jst` build system

A collection of rules for building C/C++ libraries and binaries.

## How to use this Repository

To import `rules-cc` to your repository, add the following code to the *imports
section* of your `repos.in.json` and run `jst-lock` to generate the final
repository lock-file

~~~jsonc
"imports": [
  {
    "source": "git",
    "branch": "master",
    "url": "https://github.com/jst-build/rules-cc",
    "repos": [{"alias": "rules-cc"}]
  },
  // ...
],
~~~

## Consume and being consumed by CMake Libraries

For interoperability with CMake projects, see

- [consume CMake libraries](./doc/consume-cmake-libraries.md)
- [being consumed by CMake](./doc/being-consumed-by-cmake.md)

## Debug fission

The C/C++ rules have support for debug fission, which splits the debug symbols
of each compilation unit into separate artifacts, with several benefits in terms
of artifact caching, distribution, and build time.

For more details regarding this feature, see
[debug fission support](./doc/debug-fission.md).

## Rule Documentation

In this documentation, the standard configuration variables
`"AR"`, `"CC"`, `"CXX"`, `"CFLAGS"`, `"CXXFLAGS"`,`"LDFLAGS"`,
`"ADD_CFLAGS"`, `"ADD_CXXFLAGS"`, `"ADD_LDFLAGS"`, `"ENV"`,
`"BUILD_POSITION_INDEPENDENT"` are omitted.

