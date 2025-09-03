## Release `1.6.1` (UNRELEASED)

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

## Release `1.6.0` (2025-07-15)

First initial release based on upstream version `1.6.1`.
