## Unreleased

### Breaking changes

- All default paths inherited from upstream *justbuild* have been renamed to
  their `jst` equivalents, with no fallback to the old locations: the local
  build root is now `$HOME/.cache/jst`, the checkout-locations file
  `~/.jst-local.json`, and the repository configuration is looked up as
  `~/.jst-repos.json` and `/etc/jst-repos.json`.
- The rc-file fallback to `~/.just-mrrc` has been dropped; only `~/.jstrc` is
  read now. The rc-file keys for the backend have been renamed and the legacy
  spellings dropped: `"just files"` is now `"jst files"`, and `"just args"` is
  now `"jst args"`.

### Other changes

- `serve` and `execute` are now known `jst` subcommands.
- `jst-lock` now derives the default output file name from the input file
  name: an input of the form `<path>/<name>.in.json` results in output
  `<path>/<name>.json`. If the input name does not end in `.in.json`, the
  output file must be specified explicitly via `-o`.
- The hasher now uses OpenSSL's algorithm-agnostic `EVP_MD_CTX` digest API
  instead of the deprecated per-algorithm `SHA1_*`/`SHA256_*`/`SHA512_*`
  functions, fixing the build against OpenSSL 3.x while remaining compatible
  with BoringSSL.

### Fixes

- The `justlang` lexer no longer reads past the end of the source buffer when
  the input ends in trailing whitespace or an unterminated comment.
- Merged fixes from upstream version `1.6.6`
- The output-content check for actions (`OutputsCheck`) now also considers
  `output_symlinks` when collecting actual output paths, so actions producing
  plain (non-file, non-directory) symlinks are no longer incorrectly flagged
  as missing outputs.

## Release `1.6.1` (2025-09-03)

Bug fixes on top of `1.6.0`.

### Fixes

- The single-node execution service (`jst backend execute`) now supports RBE
  protocol version `2.2`. Starting with this version, platform properties are
  part of the `Action` protobuf message and not the `Command` protobuf message.
- The single-node execution service (`jst backend execute`) will now honor
  platform properties during action creation. Despite being a single-node
  service without execution image dispatch, platform properties can still be
  useful to enforce sharding of the action cache.
- Generic actions use the POSIX-mandated shell path `/bin/sh` by default. This
  solves the issue that `sh` cannot be found on some remote execution services
  that use an empty environment and without the option to specify a launcher.
- Merged fixes from upstream version `1.6.3`
- `jst` no longer crashes if the empty string is specified as
  path for a `"file"` repository; instead it treats it as `"."`.

## Release `1.6.0` (2025-07-15)

First initial release based on upstream version `1.6.1`.
