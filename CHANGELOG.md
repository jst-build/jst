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
- The option naming the repository *build* configuration, i.e., the file
  describing the roots to build from, is now `-B`,
  `--repository-build-config`, instead of `-C`, `--repository-config`. This
  removes the clash with `jst`'s own `-C`, which names the multi-repository
  configuration and keeps its meaning. The format is called
  jst-repo-build-config accordingly (see `jst-repo-build-config`(5),
  previously `jst_backend-repo-config`(5)). Affected are `jst_backend` and
  the bootstrap traversers; `jst`, `jst-lock` and `jst-import-git` are
  unchanged.

### Other changes

- `serve` and `execute` are now known `jst` subcommands.
- The single-node execution service (`jst execute`) now rejects batch requests
  whose total blob size exceeds the supported limit with `INVALID_ARGUMENT`, as
  foreseen by the remote build execution protocol, instead of answering them
  with a response of arbitrary size. That limit can be set with the new option
  `--max-batch-size`; it defaults to the maximum gRPC message length of 3 MiB,
  which is also the cap for larger values. The limit reported via the
  capabilities service, by default the supported one, can be set independently
  with the new option `--max-batch-size-reported`; a value of 0 reports that no
  limit is set.
- The `jstlang` language implementation, formerly vendored as a separate
  repository under `extern/justlang`, now lives in the main source tree at
  `src/buildtool/jstlang`. Its compiler binary is available as the export
  target `jstlangc`, which is deliberately not part of `INSTALL`.
- `jst-lock` now derives the default output file name from the input file
  name: an input of the form `<path>/<name>.in.json` results in output
  `<path>/<name>.json`. If the input name does not end in `.in.json`, the
  output file must be specified explicitly via `-o`.
- The hasher now uses OpenSSL's algorithm-agnostic `EVP_MD_CTX` digest API
  instead of the deprecated per-algorithm `SHA1_*`/`SHA256_*`/`SHA512_*`
  functions, fixing the build against OpenSSL 3.x while remaining compatible
  with BoringSSL.
- If the `libgit2` version built against was configured to handle SSH
  connections by executing an external OpenSSH command (like the `ssh` binary),
  `jst` no longer shells out to `git` for remotes reached over SSH, i.e.,
  `ssh://`, `git+ssh://`, `ssh+git://`, and scp-style `[user@]host:path`
  locations. For `libgit2` versions lacking that capability, shelling out
  remains the fallback to ensure that the user's SSH setup is properly honored,
  and also to support protocols `libgit2` cannot handle. Note that `ssh`
  inherits the full environment `jst` was called in, so the repository field
  `"inherit env"` has no effect for such remotes.
- The bundled `libgit2` has been upgraded to version `1.9.7` and is now built
  with `USE_SSH=exec`, making `libgit2` handle the SSH transport itself by
  executing the system's `ssh` binary. The main benefit is
  that the bundled `jst` no longer requires `git` to be installed on the host
  in order to fetch such repositories. The SSH setup of the user is honored as
  before, as it is the system's `ssh` that is executed; `GIT_SSH_COMMAND`,
  `GIT_SSH`, and `core.sshCommand` are taken into account as usual.
- The progress reporting has been redesigned, see the new concept
  documentation [Progress Reporting](doc/concepts/progress-reporting.md).
  On a terminal, the build now shows its running actions in columns that
  adapt to the terminal width, each with a spinner and its running time,
  above a bottom line with a progress bar, the total build time, and the
  action statistics. Spinner and progress bar use Unicode characters if the
  terminal can display them, and ASCII characters otherwise.
- Outside of a terminal, or with the new option `--plain-progress`, the
  build prints one line per executed action instead, prefixed by a counter.
  Environments with the `CI` environment variable set are treated as
  non-interactive as well. The option `--plain-log` is an alias for
  `--plain-progress --no-color`.
- The repository setup now reports its progress in the same way: on a
  terminal with the repositories being worked on, including the progress of
  downloads and the amounts imported, and otherwise with one line per
  repository to be set up. Repositories that already exist locally are only
  counted, so a setup that has nothing to do is silent. The final setup summary
  states the time taken and the amounts fetched and imported.
- The new options `--color` and `--no-color` control the highlighting of
  the output; if neither is given, the environment variables `FORCE_COLOR`
  and `NO_COLOR` are honored, in this order, before falling back to
  checking whether stderr is a terminal. Of these options and `--plain-log`,
  the one given last on the command line decides. In the configuration of
  `jst backend serve`, the logging key `"color"` is a flag accordingly.
- The build summary now states the time the build took and is printed
  after the list of artifacts.

### Fixes

- Batch transfers to and from a remote-execution service no longer fail if the
  service enforces a stricter limit on the total size of a batch request than
  the one it announces via its capabilities; the protocol explicitly allows a
  service to announce no limit at all, while still being subject to a message
  size limitation of its own. Rejected batch requests now lower the assumed
  limit for that remote-execution instance and are retried with smaller
  batches, falling back to the streaming API for blobs that do not fit a batch
  request.
- The `jstlang` lexer no longer reads past the end of the source buffer when
  the input ends in trailing whitespace or an unterminated comment.
- Merged fixes from upstream version `1.6.6`.
- The output-content check for actions (`OutputsCheck`) now also considers
  `output_symlinks` when collecting actual output paths, so actions producing
  plain (non-file, non-directory) symlinks are no longer incorrectly flagged
  as missing outputs.

## Release `1.6.1` (2025-09-03)

Bug fixes on top of `1.6.0`.

### Fixes

- The single-node execution service (`jst execute`) now supports RBE
  protocol version `2.2`. Starting with this version, platform properties are
  part of the `Action` protobuf message and not the `Command` protobuf message.
- The single-node execution service (`jst execute`) will now honor
  platform properties during action creation. Despite being a single-node
  service without execution image dispatch, platform properties can still be
  useful to enforce sharding of the action cache.
- Generic actions use the POSIX-mandated shell path `/bin/sh` by default. This
  solves the problem that `sh` cannot be found on some remote execution services
  that use an empty environment and offer no option to specify a launcher.
- Merged fixes from upstream version `1.6.3`.
- `jst` no longer crashes if the empty string is specified as the
  path for a `"file"` repository; instead it treats it as `"."`.

## Release `1.6.0` (2025-07-15)

Initial release, based on upstream version `1.6.1`.
