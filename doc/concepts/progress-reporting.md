# Progress Reporting

While setting up repositories and while building, `jst` reports what it
is currently doing. How it does so depends on whether anybody is watching
the output live or not.

## Interactive and non-interactive output

The output is considered *interactive* if stderr is attached to a terminal
and the environment variable `CI` is not set. The option `--plain-progress`
forces non-interactive output, even on a terminal.

- **Interactive** output is a live block at the bottom of the terminal,
  which is redrawn in place. It shows what is being worked on right now,
  each item with a spinner and the time spent on it so far, and a bottom
  line with the overall progress. Once done, the block is removed and only
  the final summary remains.
- **Non-interactive** output prints one line per item at the time work on
  it starts, prefixed by a counter. Nothing is ever overwritten or cut, so
  that the output can be followed in a CI log or a pipe.

Log files (`-f`, `--log-file`) never contain the live block of the
interactive output, and never contain colors; every message written to them
is prefixed by a timestamp, regardless of the console mode.

### Colors

The options `--color` and `--no-color` decide whether the output is
highlighted with ANSI escape sequences. If neither is given, a non-empty
`FORCE_COLOR` enables colors, a non-empty
[`NO_COLOR`](https://no-color.org) disables them, and otherwise colors are
used if stderr is a terminal.

The option `--plain-log` combines `--plain-progress` and `--no-color`. Of
all these options, the one given last on the command line decides whether
colors are used.

The overall status (counters, the bottom-line prefix) is shown in green,
the names of the items being worked on in blue, and durations are dimmed.

### Durations

Durations are shown with a single decimal place below one minute (`3.1s`),
in minutes and seconds below one hour (`2m03s`), and in hours and minutes
beyond (`1h07m`). Layouts reserve 6 characters for a duration, which fits
anything up to `23h59m`; longer durations are not cut, but exceed their
column.

### Progress bars

The bottom line, and in the setup also the operation column, holds a
progress bar. If the terminal can display Unicode (detected as for the
spinner), it is drawn as a heavy line for the part done, a half heavy line
for half a column, and a light line for the rest, e.g.,
`━━━━━━━━╸─────────`; with colors, the part done is green and the rest
dimmed. Otherwise it falls back to ASCII, `[======>    ]`, where the
brackets count towards its width. Either way the bar occupies the width in
columns given by the layout.

## Layout principles

All progress output is arranged in columns, separated by two spaces.

In interactive mode, every column has a minimum and a maximum width, and
the layout adapts to the width of the terminal: if the terminal is too
narrow for all columns at their maximum width, one designated column
shrinks first, down to its minimum, and only then the other one. Content
exceeding its column is cut as described per column below. If the width of
the terminal cannot be determined, 80 columns are assumed. A terminal
narrower than the sum of all minimum widths (50 columns) is accepted; the
lines then simply exceed it.

In non-interactive mode, columns have a fixed width, but content is never
cut: overlong content just shifts the following column. The counter is not
padded and is expected to fit up to `[1000/9999]` (11 characters).

## Build progress

The build progress is laid out for action labels (the `"label"` of an
action, see [rules](rules.md)) of up to 40 characters. The columns holding
the label are sized accordingly: 43 characters in interactive mode (40
plus 3 for the spinner and the spaces around it), and 52 characters in
non-interactive mode (40 plus 12 for the counter of up to 11 characters and
the space following it).

### Interactive

| Column | Content | Width | Alignment | Exceeding content |
|-|-|-|-|-|
| Label | 1 space, spinner, 1 space, action label (up to 40) | 23 to 43 | left | cut on the right, ending with `...` |
| Duration | time the action is running | 6 | right | never cut |
| Origin | quoted target name and action number | 17 or more | left | module shortened on the left, then the whole origin cut on the left |

The Origin column shrinks first. At most eight running actions are shown;
if more are running, a line `... and N more` follows in the Label column,
indented by 3 spaces.

An origin reads `'<repository>//<module>:<target>'#<action>`, of which
target and action number matter most, and the module least. It is therefore
shortened in two steps: first the module is cut on the left, keeping the
repository; only if even the origin without a module does not fit, the
whole origin is cut on the left. Origins of a different shape, e.g. of
anonymous targets, are always cut on the left:

```
'jst//src/buildtool/multithreading:task_system'#0     fits
'jst//...uildtool/multithreading:task_system'#0       module shortened
'jst//...:task_system'#0                              module dropped
'...ask_system'#0                                     cut on the left
```

The bottom line uses the same columns: the Label column holds a progress
bar of the actions done (executed or cached) out of all actions, prefixed
by `    Building `; the Duration column the time the build is running; the
Origin column the statistics `N/M done, C cached, P processing.`, of which
`processing` and then `cached` are dropped if they do not fit, while
`done` is always shown.

At a terminal width of 100:

```
 ⠼ Patching file options.h                     0.4s  'absl//:absl/base/options.h'#0
 ⠼ Compiling task_system.cpp                   0.3s  'jst//...uildtool/multithreading:task_system'#0
 ⠼ Compiling builtin_functions.cpp           11m21s  'jst//src/buildtool/jstlang/ast:builtins'#0
 ⠼ Compiling git_context.cpp                  1m33s  'jst//src/buildtool/file_system:git_context'#0
 ⠼ Compiling expression_ptr.cpp                0.3s  'jst//.../build_engine/expression:expression'#2
 ⠼ Compiling a_really_long_file_name_for...    0.2s  'jst//test:long'#0
   ... and 16 more
    Building ━━╸───────────────────────────  11m22s  34/382 done, 0 cached, 24 processing.
```

The same state at a terminal width of 60:

```
 ⠼ Patching file options.h           0.4s  '.../options.h'#0
 ⠼ Compiling task_system.cpp         0.3s  '...ask_system'#0
 ⠼ Compiling builtin_functions...  11m21s  '...t:builtins'#0
 ⠼ Compiling git_context.cpp        1m33s  '...it_context'#0
 ⠼ Compiling expression_ptr.cpp      0.3s  '...expression'#2
 ⠼ Compiling a_really_long_fil...    0.2s  '.../test:long'#0
   ... and 16 more
    Building ━╸──────────────────  11m22s  34/382 done.
```

### Non-interactive

| Column | Content | Width | Alignment |
|-|-|-|-|
| Label | counter (up to 11), 1 space, action label (up to 40) | 52 | left |
| Origin | quoted target name and action number | unlimited | left |

The counter reports the position of the action among all actions of the
build. Only actions that are actually executed are printed; actions served
from cache are counted but not printed, which leaves gaps in the
numbering:

```
[1/3558] Patching file options.h                      'absl//:absl/base/options.h'#0
[2/3558] Compiling uncompr.c                          'zlib//:zlib_unexported'#13
[5/3558] Compiling trees.c                            'zlib//:zlib_unexported'#12
[9/3558] Compiling os.cc                              'fmt//:fmt_unexported'#1
[10/3558] Compiling sha1-armv8-win.S                  'ssl//:crypto_asm'#67
[12/3558] Compiling chacha20_poly1305_x86_64-linux.S  'ssl//:crypto_asm'#114
```

### Summary

Once the build is finished, the artifacts are listed, followed by a
summary of the form

```
INFO: Processed 1703 actions in 3m17s (1497 cache hits).
```

## Setup progress

Only repositories that actually have to be set up are reported.
Repositories that already exist locally, e.g., from a previous setup, count
as *pre-existing*: they are counted in the overall progress, but not shown
individually. If all repositories pre-exist, the setup is silent.

### Interactive

| Column | Content | Width | Alignment | Exceeding content |
|-|-|-|-|-|
| Repository | 1 space, spinner, 1 space, repository name | 20 to 30 | left | cut on the right |
| Duration | time the repository is being worked on | 6 | right | never cut |
| Operation | operation, progress bar, amount | 20 or more | left | amount dropped as a whole, then cut on the right |

The Operation column shrinks first. It holds the kind of work being done
(`fetching`, `unpacking`, `importing`, or `computing`), followed by a
10-character progress bar whenever the total of a transfer is known (the
size of a download, or the number of objects of a git fetch), followed by
the amount transferred so far, e.g., `332.4 KiB` or, if the total size is
known, `174.9 KiB/2.3 MiB`. Imports are only known as a whole, so they
are shown with their amount, but without a progress bar.

At most eight repositories are shown; if more are being worked on, a line
`... and N more` follows in the Repository column, indented by 3 spaces.

The bottom line holds a progress bar of the repositories set up, including
the pre-existing ones, prefixed by `    Setting up `; the time the setup is
running; and the statistics `N/M done, fetched X/Y+`. Here, `Y+` is the
total size announced by the downloads so far; as sizes are only discovered
while fetching, it is a lower bound. If the statistics do not fit, `/Y+` is
dropped first, and then the fetched amount, while `done` is always shown.

At a terminal width of 80:

```
 ⠼ re2/config                     0.5s  fetching ━━━━━───── 332.4 KiB
 ⠼ absl                           1.2s  fetching ╸───────── 174.9 KiB/2.3 MiB
 ⠼ ssl                            4.9s  fetching ━━━━━━━━╸─ 63.6 MiB/70.7 MiB
 ⠼ curl                           2.1s  unpacking
 ⠼ google_apis                    4.2s  importing 57.7 MiB
 ⠼ libgit2                        0.1s  fetching
   ... and 3 more
    Setting up ━━━━━━━────────    5.3s  34/68 done, fetched 90.7 MiB/130.6 MiB+
```

The same state at a terminal width of 60:

```
 ⠼ re2/config                     0.5s  fetching ━━━━━─────
 ⠼ absl                           1.2s  fetching ╸─────────
 ⠼ ssl                            4.9s  fetching ━━━━━━━━╸─
 ⠼ curl                           2.1s  unpacking
 ⠼ google_apis                    4.2s  importing 57.7 MiB
 ⠼ libgit2                        0.1s  fetching
   ... and 3 more
    Setting up ━━━━━━━────────    5.3s  34/68 done
```

### Non-interactive

A single column: the counter, 1 space, and `Setting up <repository>`.
The counter reports the position of the repository among all repositories;
pre-existing repositories are counted but not printed, which leaves gaps in
the numbering:

```
[1/68] Setting up zlib
[2/68] Setting up zlib/targets
[3/68] Setting up grpc_toolchain/targets
[4/68] Setting up json
[5/68] Setting up json/targets
[7/68] Setting up bazel_remote_apis
[8/68] Setting up absl
```

### Summary

If any repository had to be set up, the setup ends with a summary stating
the number of repositories set up, the time it took, the number of
pre-existing repositories, and the amounts fetched from the network and
imported to the local storage, if any:

```
INFO: Processed 35 repositories in 17.2s (33 pre-existed), fetched 140.5 MiB, imported 643.4 MiB.
```
