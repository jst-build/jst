## Unreleased

### Other changes

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
