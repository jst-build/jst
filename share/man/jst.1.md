% JST(1) | General Commands Manual

NAME
====

jst - generic multi-repository build system

SYNOPSIS
========

**`jst`** **`version`**  
**`jst`** {**`setup`**|**`setup-env`**} \[*`OPTION`*\]... \[**`--all`**\] \[*`main-repo`*\]  
**`jst`** **`fetch`** \[*`OPTION`*\]... \[**`--all`**\] \[**`--backup-to-remote`**] \[**`-o`** *`fetch-dir`*\] \[*`main-repo`*\]  
**`jst`** **`update`** \[*`OPTION`*\]... \[*`repo`*\]...  
**`jst`** **`gc-repo`** \[*`OPTION`*\]... \[**`--drop-only`**\]  
**`jst`** {**`analyse`**|**`build`**} \[*`OPTION`*\]... \[*`target-reference`*\]  
**`jst`** **`install`** \[*`OPTION`*\]... **`-o`** *`OUTPUT_DIR`* \[*`target-reference`*\]  
**`jst`** **`rebuild`** \[*`OPTION`*\]... \[*`target-reference`*\]  
**`jst`** **`describe`** \[*`OPTION`*\]... \[*`target-reference`*\]  
**`jst`** **`install-cas`** \[*`OPTION`*\]... *`OBJECT_ID`*  
**`jst`** **`add-to-cas`** \[*`OPTION`*\]... *`PATH`*  
**`jst`** **`gc`** \[*`OPTION`*\]...  
**`jst`** **`eval`** \[*`OPTION`*\]...  
**`jst`** **`execute`** \[*`OPTION`*\]...  
**`jst`** **`serve`** *`SERVE_CONFIG_FILE`*  
**`jst`** **`backend`** \[*`OPTION`*\]... \[*`BACKEND_ARG`*\]...  

DESCRIPTION
===========

**`jst`** is a generic multi-repository build system; language-specific
knowledge is described in separate rule files. For every build action,
the relative location of the inputs is independent of their physical
location. This staging allows taking sources from different locations
(logical repositories), including bare Git repositories. Targets are
defined using JSON format, in proper files (by default, named
*`TARGETS`*). Targets are uniquely identified by their name, the
repository, and the module they belong to. A module is the relative path
from the repository target root directory to a subdirectory containing a
target file.

The module's name of the targets defined at the target root level is
the empty string. Specifying the correct repository, target root,
module, and target name allows that target to be processed independently of
the current working directory.

A target is named on the command line by a single *target-reference*, of
the same syntax that target files use:
*`[<repository>//][<module>][:[<target>]]`*. Two shortcuts exist that a
target file does not have: a bare name is the target of that name in the
module of the current working directory (so **`baz`** is the same as
**`:baz`**), and an empty target segment denotes the default target of the
module (**`:`**, **`./sub:`**, **`//tests:`**, *`repo`***`//:`**). Without
any argument, the default target of the current module is built, which is
the same as **`:`**.

A module given as **`//`***`module`* is relative to the target root, one
given as **`./`***`module`* is relative to the module of the current
working directory, and a module escaping the workspace is an error. A
reference of the form **`//`***`foo/bar`* without a target segment is the
target *`bar`* of the module *`foo/bar`*, not its default target; the
latter is **`//`***`foo/bar`***`:`**.

The default target of a module is its lexicographically-first target,
according to native byte order. So, a target named with an empty string
will always be the default target for that module.

If a target depends on other targets defined in other modules or
repositories, **`jst`** will recursively visit all and only the required
modules.

The main repository is the repository containing the target specified on
the command line. It is named by the *`<repository>`* segment of the
reference, which takes precedence over everything else; a reference
without that segment leaves the choice to the option **`--main`**, and
that in turn to the key *`"main"`* of the multi-repository configuration
file. If none of the three is given, the lexicographical first repository
from the multi-repository configuration file is used as main.

Note that the *`<repository>`* segment means different things on the
command line and in a target file, as the two resolve it against
different things. On the command line it is a repository of the
multi-repository configuration, named globally, just as **`--main`**
names one; that is what allows naming a repository the main repository
does not depend on. In a target file it is a name bound in the
*`"bindings"`* of the repository the file belongs to, which may well be
a different name for the same repository. So a reference can be moved
between a target file and the command line unchanged only if it names no
repository, or if the binding and the repository have the same name.

The *`workspace_root`* of the main repository is then defined as
follows. If the option **`--workspace-root`** is provided, then
*`workspace_root`* is set accordingly. If the option is not provided,
**`jst`** checks if it is specified within the multi-repository
configuration file. If it is, then it is set accordingly. If not, **`jst`**
starts looking for a marker in the current directory first, then in all
the parent directories until it finds one. The supported markers are

 - *`ROOT`* file (can be empty, content is ignored)
 - *`WORKSPACE`* (can be empty, content is ignored)
 - *`.git`* (can be either a file - empty or not, content is ignored -
   or the famous directory)

If it fails, **`jst`** errors out.

For non-main repositories, the *`workspace_root`* entry must be declared
in the multi-repository configuration file.

Afterwards, *`target_root`*, *`rule_root`*, and *`expression_root`*
directories for the main repository are set using the following
strategy. If the corresponding command-line option is specified, then it
is honored. Otherwise, it is read from the multi-repo configuration
file. If it is not specified there, the default value is used. The
default value of *`target_root`* is *`workspace_root`*, of *`rule_root`*
is *`target_root`*, and of *`expression_root`* is *`rule_root`*.

Finally, the file names where *targets*, *rules*, and *expressions* are
defined for the main repository. If the corresponding key is present in
the multi-repository configuration file, it is set accordingly. If the
user gives the corresponding command-line option, it overwrites what is
eventually read from the configuration file. If they are not explicitly
stated neither within the multi-repository configuration file nor on the
command line, the default values are used, which are *`TARGETS`*,
*`RULES`*, and *`EXPRESSIONS`*, respectively.

For non-main repositories (i.e., repositories defining targets required
by the target given on the command line), *`target_root`*,
*`rule_root`*, *`expression_root`*, and file names of targets, rules and
expressions, if different from the default values, must be declared in
the multi-repository configuration file.

The repositories to build from are described in a multi-repository
configuration; **`jst`** fetches them as needed and derives the workspace roots
from that description. See **`jst-repo-config`**(5) for the input format and
**`jst-repo-build-config`**(5) for the resulting configuration handed to the
build itself.

OPTIONS
=======

Options are given after the subcommand. Each subcommand accepts the option
groups listed below for it; any other option is reported as an error. For the
subcommands that build, all remaining arguments are the targets to act on.

General options
---------------

Accepted by every subcommand except **`version`**, which takes no options at
all.

**`-h`**, **`--help`**  
Output a usage message and exit.

**`--rc`** *`PATH`*  
Path to the jstrc file to use. See **`jstrc`**(5) for more
details.  
Default: file path *`".jstrc"`* in the user's home directory.

**`--norc`**  
Option to prevent reading any **`jstrc`**(5) file.

**`--dump-rc`** *`PATH`*  
Dump the effective rc, i.e., the rc after overlaying all applicable auxiliary
files specified in the `"rc files"` field, to the specified file. In this
way, an rc can be made self-contained in preparation for committing it to
a repository.

**`-f`**, **`--log-file`** *`PATH`*  
Path to local log file. **`jst`** will store the information printed on
stderr in the log file along with the thread id and timestamp when the
output has been generated.

**`--log-limit`** *`NUM`*  
Log limit (higher is more verbose) in interval \[0,6\] (Default: 3).

**`--restrict-stderr-log-limit`** *`NUM`*  
Restrict logging on console to the minimum of the specified **`--log-limit`**
and the value specified in this option. The default is to not additionally
restrict the log level at the console.

**`--plain-progress`**  
Use plain, non-interactive progress output: instead of the interactive
progress report, one line is printed for each action, at the time the action
is started, prefixed by the number of processed actions and the total number
of actions. Actions that were served from cache are counted, but not printed,
which leaves gaps in the sequence of reported counter values. This is the
default whenever standard error is not attached to a terminal or the
environment variable **`CI`** is set.

**`--plain-log`**  
Alias for **`--plain-progress`**, which additionally implies
**`--no-color`**; see **`--color`**.  

**`--color`**, **`--no-color`**  
Whether to use ANSI escape sequences to highlight messages. If neither is
given, the environment variables **`FORCE_COLOR`** (any non-empty value
enables colors) and **`NO_COLOR`** (any non-empty value disables colors) are
honored, in this order, before falling back to checking whether standard
error is attached to a terminal. If several of these flags are given,
including the **`--no-color`** implied by **`--plain-log`**, the last one on
the command line wins.

**`--log-append`**  
Append messages to log file instead of overwriting existing.

Backend options
---------------

Accepted by the subcommands carrying out a build, which are all but
**`version`** and **`gc-repo`**.

**`--backend`** *`PATH`*  
Name in *`PATH`* of, or path to, the binary carrying out the build. Which
binary that is, is an implementation detail; **`jst`** itself is the supported
way of calling it, in particular through the **`backend`** subcommand.  
Default: *`"jst_backend"`*.

Configuration options
---------------------

Accepted by the subcommands reading the multi-repository configuration and
setting up the repositories described in it, i.e., **`setup`**,
**`setup-env`**, **`fetch`**, **`update`**, **`describe`**, **`analyse`**,
**`build`**, **`install`**, and **`rebuild`**.

**`-C`**, **`--repository-config`** *`PATH`*  
Path to the multi-repository configuration file. See
**`jst-repo-config`**(5) for more details. If no configuration
file is specified, **`jst`** will look for one in the following
order:

 - *`$WORKSPACE_ROOT/repos.json`* (workspace of the **`jst`** invocation)
 - *`$WORKSPACE_ROOT/etc/repos.json`* (workspace of the **`jst`**
   invocation)
 - *`$HOME/.jst-repos.json`*
 - *`/etc/jst-repos.json`*

The default configuration lookup order can be adjusted in the jstrc
file. See **`jstrc`**(5) for more details.

**`--absent`** *`PATH`*  
Path to a file specifying which repositories are to be considered
absent, overriding the values set by the *`"pragma"`* entries in the
multi-repository configuration. The file has to contain a JSON array
of those repository names to be considered absent.

**`--checkout-locations`** *`PATH`*  
Specification file for checkout locations and additional mirrors.
This file contains a JSON object with several known keys:

 - the key *`"<version control>"`* of key *`"checkouts"`* specifies
   pairs of repository URLs as keys and absolute paths as values.
   Currently supported version control is Git, therefore
   the respective key is *`"git"`*. The paths contained for each repository
   URL point to existing locations on the filesystem containing the
   checkout of the respective repository.  
 - the key *`"local mirrors"`*, if given, is a JSON object mapping primary
   URLs to a list of local (non-public) mirrors. These mirrors are always
   tried first (in the given order) before any other URL is contacted.
 - the key *`"preferred hostnames"`*, if given, is a list of strings
   specifying known hostnames. When fetching from a non-local mirror, URLs
   with hostnames in the given list are preferred (in the order given)
   over URLs with other hostnames.
 - the key *`"extra inherit env"`*, if given, is a list of strings
   specifying additional variable names to be inherited from the
   environment (besides the ones specified in *`"inherit env"`*
   of the respective repository definition). This can be useful,
   if the local git mirrors use a different protocol (like `ssh`
   instead of `https`) and hence require different variables to
   pass the credentials.

This options overwrites any values set in the **`jstrc`**(5) file.  
Default: file path *`".jst-local.json"`* in user's home directory.

**`--distdir`** *`PATH`*  
Directory to look for distfiles before fetching. If given, this will be
the first place distfiles are looked for. This option can be given
multiple times to specify a list of distribution directories that are
used for lookup in the order they appear on the command line.
Directories specified via this option will be appended to the ones set
in the **`jstrc`**(5) file.  
Default: the single file path *`".distfiles"`* in user's home directory.

**`--main`** *`NAME`*  
The repository to take the target from. A target reference naming a
repository, as *`repo`***`//`***`module`***`:`***`target`*, takes
precedence over this option.

**`--git`** *`PATH`*  
Path to the git binary in *`PATH`* or path to the git binary. Used in
the rare instances when shelling out to git is needed. SSH remotes are
among those instances only if the `libgit2` **`jst`** is built against
does not support SSH by executing the system's `ssh` binary.  
Default: *`"git"`*.

**`--no-fetch-ssl-verify`**  
Disable the default peer SSL certificate verification step when fetching
archives (for which we verify the hash anyway) from remote.

**`--fetch-cacert`** *`PATH`*  
Path to the CA certificate bundle containing one or more certificates to
be used to peer verify archive fetches from remote.

**`--fetch-absent`**  
Try to make available all repositories, including those marked as absent.
This option cannot be set together with **`--compatible`**.

Build-root options
------------------

Accepted by every subcommand using the local build root, i.e., by all but
**`version`**, **`eval`**, and **`serve`**.

**`--local-build-root`** *`PATH`*  
Root for local CAS, cache, and build directories. The path will be
created if it does not exist already. This option overwrites any values
set in the **`jstrc`**(5) file.  
Default: path *`".cache/jst"`* in user's home directory.

Launcher options
----------------

Accepted by the subcommands running actions locally, i.e., by the
*configuration options* subcommands above and by **`execute`**.

**`-L`**, **`--local-launcher`** *`JSON_ARRAY`*  
JSON array with the list of strings representing the launcher to prepend
actions' commands before being executed locally.  
Default: *`["env", "--"]`*.

Overlay options
---------------

Accepted by the subcommands analysing targets, i.e., **`describe`**,
**`analyse`**, **`build`**, **`install`**, **`rebuild`**, and **`eval`**.

**`-D`**, **`--defines`** *`JSON`*  
Defines, via an in-line JSON object, an overlay configuration for
**`jst`**(1), to which it is forwarded. If **`-D`** is given several
times, the **`-D`** options overlay (in the sense of *`map_union`*) in the
order they are given on the command line, and are forwarded as a single
overlay configuration.

Parallelism options
-------------------

Accepted by the subcommands doing the work they size.

**`--parallel`** *`NUM`*  
Number of tasks to run in parallel, e.g., for importing to git, and, for the
subcommands *`analyse`*, *`build`*, *`describe`*, *`install`* and *`rebuild`*,
for analysing targets and building the action graph.  
Default: Number of cores.  

**`-J`**, **`--fetch-jobs`** *`NUM`*  
Number of fetches to perform concurrently, i.e., of archives and of git
repositories. Use it to limit the load on the network independently of the
remaining parallelism. Note that this only takes effect for the
subcommands whose work consists of fetching, i.e., *`fetch`* and
*`update`*; during a *`setup`*, the fetches share the parallelism of the
other work.  
Default: value of **`--parallel`**.  

**`-j`**, **`--jobs`**, **`--build-jobs`** *`NUM`*  
Number of jobs to run during the build phase. Default: value of

Remote-execution options
------------------------

Accepted by the subcommands acting as a client of a remote-execution
service, i.e., the *configuration options* subcommands above except
**`update`**, as well as **`install-cas`** and **`add-to-cas`**. The
*authentication options* below belong to this group as well.

**`-r`**, **`--remote-execution-address`** *`NAME`*:*`PORT`*  
Address of a remote execution service. This is used as an intermediary fetch
location for archives, between local CAS (or distdirs) and the network.

**`--remote-instance-name`** *`NAME`*
Value to pass as `instance_name` in the remote execution API.  

**`--compatible`**  
At increased computational effort, be compatible with the original remote build
execution protocol. If a remote execution service address is provided, this 
option can be used to match the artifacts expected by the remote endpoint.

**`--max-attempts`** *`NUM`*  
If a remote procedure call (rpc) returns `grpc::StatusCode::UNAVAILABLE`, that
rpc is retried at most *`NUM`* times. (Default: 1, i.e., no retry).

**`--initial-backoff-seconds`** *`NUM`*  
Before retrying the second time, the client will wait the given amount of
seconds plus a jitter, to better distribute the workload. (Default: 1).

**`--max-backoff-seconds`** *`NUM`*  
From the third attempt (included) on, the backoff time is doubled at
each attempt, until it exceeds the `max-backoff-seconds`
parameter. From that point, the waiting time is computed as
`max-backoff-seconds` plus a jitter. (Default: 60)

**`--remote-execution-property`** *`KEY`*:*`VAL`*  
Property for remote execution as key-value pair. Specifying this option
multiple times will accumulate pairs. If multiple pairs with the same
key are given, the latest wins.  
Supported by: analyse|build|install|rebuild|traverse.

**`--endpoint-configuration`** FILE  
File containing a description on how to dispatch to different
remote-execution endpoints based on the execution properties.
The format is a JSON list of pairs (lists of length two) of an object
of strings and a string. The first entry describes a condition (the
remote-execution properties have to agree on the domain of this
object), the second entry is a remote-execution address in the NAME:PORT
format as for the **`-r`** option. The first matching entry (if any) is taken;
if none matches, the default execution endpoint is taken (either
as specified by **`-r`**, or local execution if no endpoint is
specified).  
Supported by: analyse|build|install|rebuild|traverse.

Serve options
-------------

Accepted by the subcommands using a remote **`serve`** service, i.e.,
**`setup`**, **`setup-env`**, **`fetch`**, **`describe`**, **`analyse`**,
**`build`**, **`install`**, and **`rebuild`**.

**`-R`**, **`--remote-serve-address`** *`NAME`*:*`PORT`*  
Address of a **`jst serve`** service. This is used as intermediary fetch
location for Git commits, between local CAS and the network.

Authentication options
----------------------

Only TLS and mutual TLS (mTLS) are supported.
They mirror the **`jst`**(1) options.

**`--tls-ca-cert`** *`PATH`*  
Path to a TLS CA certificate that is trusted to sign the server
certificate.

**`--tls-client-cert`** *`PATH`*  
Path to a TLS client certificate to enable mTLS. It must be passed in
conjunction with **`--tls-client-key`** and **`--tls-ca-cert`**.

**`--tls-client-key`** *`PATH`*  
Path to a TLS client key to enable mTLS. It must be passed in
conjunction with **`--tls-client-cert`** and **`--tls-ca-cert`**.

Build configuration options
---------------------------

**`--action-timeout`** *`NUM`*  
Action timeout in seconds. (Default: 300). The timeout is honored only
for the remote build.  
Supported by: analyse|build|install|rebuild|traverse.

**`-c`**, **`--config`** *`PATH`*  
Path to configuration file.  
Supported by: analyse|build|describe|install|rebuild.

**`-B`**, **`--repository-build-config`** *`PATH`*  
Path to the repository build configuration, describing the roots of the
repositories to build from. See **`jst-repo-build-config`**(5) for more
details.  
Supported by: analyse|build|describe|install|rebuild|traverse.

**`--request-action-input`** *`ACTION`*  
Modify the request to be, instead of the analysis result of the
requested target, the input stage of the specified action as artifacts,
with empty runfiles and a provides map providing the remaining
information about the action, in particular as *`"cmd"`* the arguments
vector and *`"env"`* the environment.

An action can be specified in the following ways

 - an action identifier prefixed by the *`%`* character
 - a number prefixed by the *`#`* character (note that it requires
   quoting on most shells). This specifies the action with that index of
   the actions associated directly with that target; the indices start
   from 0 onwards, and negative indices count from the end of the array
   of actions.
 - an action identifier or number without prefix, provided the action
   identifier does not start with either *`%`* or *`#`* and the number
   does not happen to be a valid action identifier.

Supported by: analyse|build|describe|install|rebuild.

**`--expression-file-name`** *`TEXT`*  
Name of the expressions file.  
Supported by: analyse|build|describe|install|rebuild.

**`--expression-root`** *`PATH`*  
Path of the expression files' root directory. Default: Same as

**`--rule-root`**.  
Supported by: analyse|build|describe|install|rebuild.

**`--rule-file-name`** *`TEXT`*  
Name of the rules file.  
Supported by: analyse|build|describe|install|rebuild.

**`--rule-root`** *`PATH`*  
Path of the rule files' root directory. Default: Same as

**`--target-root`**  
Supported by: analyse|build|describe|install|rebuild.

**`--target-file-name`** *`TEXT`*  
Name of the targets file.  
Supported by: analyse|build|describe|install|rebuild.

**`--target-root`** *`PATH`*  
Path of the target files' root directory. Default: Same as

**`--workspace-root`**  
Supported by: analyse|build|describe|install|rebuild.

**`-w`**, **`--workspace-root`** *`PATH`*  
Path of the workspace's root directory.  
Supported by: analyse|build|describe|install|rebuild|traverse.

General output options
----------------------

**`--dump-artifacts-to-build`** *`PATH`*  
File path for writing the artifacts to build to. Output format is JSON
map with staging path as key, and intentional artifact description as
value.  
Supported by: analyse|build|install|rebuild.

**`--dump-artifacts`** *`PATH`*  
Dump artifacts generated by the given target. Using *`-`* as PATH, it is
interpreted as stdout. Note that, passing *`.`*/*`-`* will instead
create a file named *`-`* in the current directory. Output format is
JSON map with staging path as key, and object id description (hash,
type, size) as value. Each artifact is guaranteed to be *`KNOWN`* in
CAS. Therefore, this option cannot be used with **`analyse`**.  
Supported by: build|install|rebuild|traverse.

**`--profile`** *`PATH`*  
Write a profile to the specified path. See **`jst-profile`**(5) for
details on the format.  
Supported by: analyse|build|install|rebuild|describe.

**`--dump-graph`** *`PATH`*  
File path for writing the action graph description to. See
**`jst-graph-file`**(5) for more details.  
Supported by: analyse|build|install|rebuild.

**`--dump-plain-graph`** *`PATH`*  
File path for writing the action graph description to, however without
the additional `"origins"` key. See **`jst-graph-file`**(5) for more details.  
Supported by: analyse|build|install|rebuild.

**`--expression-log-limit`** *`NUM`*  
In error messages, truncate the entries in the enumeration of the active
environment, as well as the expression to be evaluated, to the specified
number of characters (default: 320).  
Supported by: analyse|build|install.

**`--serve-errors-log`** *`PATH`*  
Path to local file in which **`jst`** will write, in machine
readable form, the references to all errors that occurred on the
serve side. More precisely, the value will be a JSON array with one
element per failure, where the element is a pair (array of length
2) consisting of the configured target (serialized, as usual, as a
pair of qualified target name an configuration) and a string with
the hex representation of the blob identifier of the log; the log
itself is guaranteed to be available on the remote-execution side.  
Supported by: analyse|build|install.

**`-P`**, **`--print-to-stdout`** *`LOGICAL_PATH`*  
After building, print the specified artifact to stdout.  
Supported by: build|install|rebuild|traverse.

**`-p`**, **`--print-unique-artifact`**  
After building, print the unique artifact to stdout, if any. If
the option **`-P`** is given or the number of artifacts is not
precisely one, this option has no effect.  
Supported by: build|install|rebuild|traverse.

**`-s`**, **`--show-runfiles`**  
Do not omit runfiles in build report.  
Supported by: build|install|rebuild|traverse.

**`--target-cache-write-strategy`** *`STRATEGY`*  
Strategy for creating target-level cache entries. Supported values are

 - *`sync`* Synchronize the artifacts of the export targets and write
   target-level cache entries. This is the default behaviour.
 - *`split`* Synchronize the artifacts of the export targets, using
   blob splitting if the remote-execution endpoint supports it,
   and write target-level cache entries. As opposed to the default
   strategy, additional entries (the chunks) are created in the CAS,
   but subsequent syncs of similar blobs might need less traffic.
 - *`disable`* Do not write any target-level cache entries. As
   no artifacts have to be synced, this can be useful for one-off
   builds of a project or when the connection to the remote-execution
   endpoint is behind a very slow network.

Supported by: build|install|rebuild.

Output dir and path
-------------------

**`-o`**, **`--output-dir`** *`PATH`*  
Path of the directory where outputs will be copied. If the output path
does not exist, it will create all the necessary folders and subfolders.
If the artifacts have been already staged, they will be overwritten.  
Required by: install|traverse.

**`-o`**, **`--output-path`** *`PATH`*  
Install path for the artifact. Refer to **`install-cas`** section for more
details.  
Supported by: install-cas.

**`--archive`**  
Instead of installing the requested tree, install an archive with the
content of the tree. It is a user error to specify **`--archive`** and
not request a tree.  
Supported by: install-cas.

**`--raw-tree`**  
When installing a tree to stdout, i.e., when no option **`-o`** is given,
dump the raw tree rather than a pretty-printed version. This option is
ignored if **`--archive`** is given.  
Supported by: install-cas.

**`-P`**, **`--sub-object-path`** *`PATH`*  
Instead of the specified tree object take the object at the specified
logical path inside.  
Supported by: install-cas.

**`--remember`**  
Ensure that all installed artifacts are available in local CAS as well,
even when using remote execution.  
Supported by: install|traverse|install-cas.

**`analyse`** specific options
------------------------------

**`--dump-actions`** *`PATH`*  
Dump actions to file. *`-`* is treated as stdout. Output is a list of
action descriptions, in JSON format, for the given target.

**`--dump-anonymous`** *`PATH`*  
Dump anonymous targets to file. *`-`* is treated as stdout. Output is a
JSON map, for all transitive targets, with two entries: *`nodes`* and
*`rule_maps`*. The former contains maps between node id and the node
description. *`rule_maps`* states the maps between the *`mode_type`* and
the rule to use in order to make a target out of the node.

**`--dump-blobs`** *`PATH`*  
Dump blobs to file. *`-`* is treated as stdout. The term *`blob`*
identifies a collection of strings that the execution back end should be
aware of before traversing the action graph. A blob, will be referred to
as a *`KNOWN`* artifact in the action graph.

**`--dump-nodes`** *`PATH`*  
Dump nodes of only the given target to file. *`-`* is treated as stdout.
Output is a JSON map between node id and its description.

**`--dump-vars`** *`PATH`*  
Dump configuration variables to file. *`-`* is treated as stdout. The
output is a JSON list of those variable names (in lexicographic order)
at which the configuration influenced the analysis of this target. This
might contain variables unset in the configuration if the fact that they
were unset (and hence treated as the default *`null`*) was relevant for
the analysis of that target.

**`--dump-targets`** *`PATH`*  
Dump all transitive targets to file for the given target. *`-`* is
treated as stdout. Output is a JSON map of all targets encoded as tree
by their entity name:

``` jsonc
{
  // anonymous targets
  "#": {
    "<rule_map_id>": {
      // all configs this target is configured with
      "<node_id>": ["<serialized config1>", ...]
    }
  },
  // "normal" targets
  "@": {
    "<repo>": {
      "<module>": {
        // all configs this target is configured with
        "<target>": ["<serialized config1>", ...]
      }
    }
  }
}
```

**`--dump-export-targets`** *`PATH`*  
Dump all transitive targets to file for the given target that are export
targets. *`-`* is treated as stdout. The output format is the same as
for **`--dump-targets`**.

**`--dump-targets-graph`** *`PATH`*  
Dump the graph of configured targets to a file (even if it is called
*`-`*). In this graph, only non-source targets are reported. The graph
is represented as a JSON object. The keys are the nodes of the graph,
and for each node, the value is a JSON object containing the different
kind of dependencies (each represented as a list of nodes).

 - *`"declared"`* are the dependencies coming from the target fields in
   the definition of the target
 - *`"implicit"`* are the dependencies implicit from the rule definition
 - *`"anonymous"`* are the dependencies on anonymous targets implicitly
   referenced during the evaluation of that rule

While the node names are strings (so that they can be keys in a JSON
object), they can themselves be decoded as JSON and in this way
precisely name the configured target. More precisely, the JSON decoding
of a node name is a list of length two, with the first entry being the
target name (as *`["@", repo, module, target]`* or _`["#", rule_map_id,
node_id]`_) and the second entry the effective configuration.

**`--dump-trees`** *`PATH`*  
Dump trees and all subtrees of the given target to file. *`-`* is
treated as stdout. Output is a JSON map between tree ids and the
corresponding artifact map, which maps the path to the artifact
description.

**`--dump-provides`** *`PATH`*  
Dump the provides map of the given target to file. *`-`* is treated
as stdout. The output is a JSON object mapping the providers to their
values, serialized as JSON; in particular, artifacts are replaced
by a JSON object with their intensional description. Therefore, the
dumped JSON is not uniquely readable, but requires an out-of-band
understanding where artifacts are to be expected.

**`--dump-result`** *`PATH`*  
Dump the result of the analysis for the requested target to
file. *`-`* is treated as stdout. The output is a JSON object with the
keys *`"artifacts"`*, *`"provides"`*, and *`"runfiles"`*.

**`rebuild`** specific options
------------------------------

**`--vs`** *`NAME`*:*`PORT`*|*`"local"`*  
Cache endpoint to compare against (use *`"local"`* for local cache).

**`--dump-flaky`** *`PATH`*  
Dump flaky actions to file.

**`add-to-cas`** specific options
---------------------------------

**`--follow-symlinks`**  
Resolve the positional argument to not be a symbolic link by following
symbolic links. The default is to add the link itself, i.e., the string
obtained by **`readlink`**(2), as blob.

**`--resolve-special`** *`TEXT`*  
When adding a directory to CAS, resolve any special filesystem entries based on
the strategy denoted by the string value specified. Special entries are all
those which are neither file, executables, or directories. If option is missing
or a non-supported value is provided, the default behavior is used, in which
only non-upwards symlinks are accepted and stored unresolved.  
Currently accepted values:
 - *`"ignore"`*: all special entries are ignored
 - *`"tree-upwards"`*: accept only symlinks with a target path pointing inside
   the location directory and resolve the ones that are upwards
 - *`"tree-all"`*: accept only symlinks with a target path pointing inside the
   location directory and resolve all of them
 - *`"all"`*: unconditionally accept and resolve all symlinks

**`traverse`** specific options
-------------------------------

**`-a`**, **`--artifacts`** *`TEXT`*  
JSON maps between relative path where to copy the artifact and its
description (as JSON object as well).

**`-g`**, **`--graph-file`** *`TEXT`* *`[[REQUIRED]]`*  
Path of the file containing the description of the actions. See
**`jst-graph-file`**(5) for more details.

**`--git-cas`** *`TEXT`*  
Path to a Git repository, containing blobs of potentially missing
*`KNOWN`* artifacts.

**`describe`** specific options
-------------------------------

**`--json`**  
Omit pretty-printing and describe rule in JSON format. Takes precedence over
**`--brief`**.

**`--brief`**  
Omit documentation and describe rule by listing the names of its fields and
configuration variables only.

**`--rule`**  
Module and target arguments refer to a rule instead of a target.

**`--no-pager`**  
Do not pipe the description through a pager. Paging happens only if standard
output is attached to a terminal; see the **`JST_PAGER`** and **`PAGER`**
environment variables.

**`execute`** specific options
------------------------------

**`-p`**, **`--port`** *`INT`*  
Execution service will listen to this port. If unset, the service will
listen to the first available one.

**`--info-file`** *`TEXT`*  
Write the used port, interface, and pid to this file in JSON format. If
the file exists, it will be overwritten.

**`-i`**, **`--interface`** *`TEXT`*  
Interface to use. If unset, the loopback device is used.

**`--pid-file`** *`TEXT`*  
Write pid to this file in plain txt. If the file exists, it will be
overwritten.

**`--max-batch-size`** *`UINT`*  
Set the maximum total size, in bytes, of the blobs accepted in a single batch
request; larger requests are rejected with `INVALID_ARGUMENT`, as foreseen
by the remote build execution protocol. Clients are expected to split up
rejected requests, or to transfer the respective blobs via the streaming
API. Values larger than the maximum gRPC message length are capped to it.
If unset, defaults to the maximum gRPC message length, 3 MiB.

**`--max-batch-size-reported`** *`UINT`*  
Set the maximum total size, in bytes, of the blobs in a single batch request as
reported by the capabilities service. Values larger than the maximum gRPC
message length are capped to it. A value of 0 reports that no limit is set,
which the remote build execution protocol explicitly allows; clients then
have to assume a limit of their own. If unset, the value of
**`--max-batch-size`** is reported.

**`--tls-server-cert`** *`TEXT`*  
Path to the TLS server certificate.

**`--tls-server-key`** *`TEXT`*  
Path to the TLS server key.

**`--log-operations-threshold`** *`INT`*  
Once the number of operations stored exceeds twice *`2^n`*, where *`n`*
is given by the option **`--log-operations-threshold`**, at most *`2^n`*
operations will be removed, in a FIFO scheme. If unset, defaults to
14. Must be in the range \[0,63\].

**`gc`** specific options
-------------------------

**`--no-rotate`**  
Do not rotate garbage-collection generations. Instead, only carry
out clean up tasks that do not affect what is stored in the cache.
Incompatible with `--all`.

**`--all`**
Do not rotate garbage-collection generations and do not split large
files. Instead, remove all cache generations at once. Incompatible with
`--no-rotate`.

SUBCOMMANDS
===========

The subcommands operating on targets, i.e., **`analyse`**, **`build`**,
**`install`**, **`rebuild`** and **`describe`**, perform the **`setup`** step
described below first, and the resulting configuration is used for the build.
The main repository for that step can be given by the repository segment of
the target reference, in the multi-repository configuration, or with
**`--main`**; if none is given, the lexicographical first repository of the
configuration is used.

**`version`**
-------------

Print on stdout a JSON object providing version information about the
version of the tool used. This JSON object will contain at least the
following keys.

 - *`"version"`* The version, as a list of numbers of length at least 3,
   following the usual convention that version numbers are compared
   lexicographically.
 - *`"suffix"`* The version suffix as a string. Generally, suffixes
   starting with a + symbol are positive offsets to the version, while
   suffixes starting with a *`~`* symbol are negative offsets.
 - *`"SOURCE_DATE_EPOCH"`* Either a number or *`null`*. If it is a
   number, it is the time, in seconds since the epoch, of the last
   commit that went into this binary. It is *`null`* if that time is not
   known (e.g., in development builds).

**`setup`**|**`setup-env`**
---------------------------

These subcommands fetch all required repositories and generate an
appropriate multi-repository **`jst`** configuration file. The resulting
file is stored in CAS and its path is printed to stdout. See
**`jst-repo-build-config`**(5) for more details on the resulting
configuration file format.

If a main repository is provided in the input configuration or on
command line, only it and its dependencies are considered in the
generation of the resulting multi-repository configuration file. If no
main repository is provided, the lexicographical first repository from
the configuration is used. To perform the setup for all repositories
from the input configuration file, use the **`--all`** flag.

The behavior of the two subcommands differs only with respect to the
main repository. In the case of **`setup-env`**, the workspace root of the
main repository is left out, such that it can be deduced from the
working directory when **`jst`** is invoked. In this way, working on a
checkout of that repository is possible, while having all of its
dependencies properly set up. In the case of **`setup`**, the workspace root
of the main repository is taken as-is into the output configuration
file.

**`fetch`**
-----------

This subcommand prepares all archive-type and **`"git tree"`** workspace roots
for an offline build by fetching all their required source files from the
specified locations given in the input configuration file or ensuring the 
specified tree is present in the Git cache, respectively. Any subsequent
**`jst`** or **`jst`** invocations containing fetched archive or 
**`"git tree"`** workspace roots will thus need no further network connections.

If a main repository is provided in the input configuration or on
command line, only it and its dependencies are considered for fetching.
If no main repository is provided, the lexicographical first repository
from the configuration is used. To perform the fetch for all
repositories from the input configuration file, use the **`--all`**
flag.

By default the first distribution directory is used as the
output directory for writing the fetched archives on disk. To
define an output directory that is independent of the given distribution
directories, use the **`-o`** option.

Additionally, and only in *native mode*, the **`--backup-to-remote`** option can
be used in combination with the **`--remote-execution-address`** argument to
synchronize the locally fetched archives, as well as the **`"git tree"`** 
workspace roots, with a remote endpoint.

**`update`**
------------

This subcommand updates the specified repositories (possibly none) and
prints the resulting updated configuration file to stdout.

Currently, **`jst`** can only update Git repositories and it will fail
if a different repository type is given. The tool also fails if any of
the given repository names are not found in the configuration file.

For Git repositories, the subcommand will replace the value for the
*`"commit"`* field with the commit hash (as a string) found in the
remote repository in the specified branch. The output configuration file
will otherwise remain the same at the JSON level with the input
configuration file.

**`gc-repo`**
-------------

This subcommand rotates the generations of the repository cache.
Every root used is added to the youngest generation. Therefore upon
a call to **`gc-repo`** all roots are cleaned up that were not used
since the last **`gc-repo`**.

If **`--drop-only`** is given, only the old generations are cleaned up,
without rotation. In this way, storage can be reclaimed; this might be
necessary as no perfect sharing happens between the repository generations.

**`analyse`**|**`build`**|**`install`**
---------------------------------------

The subcommands **`analyse`**, **`build`**, and **`install`** are
strictly related. In fact, from left to right, one is a subset of the
other. **`build`** performs work on top of **`analyse`**, and
**`install`** on top of **`build`**. When a user issues **`build`**, the
**`analyse`** is called underneath. In particular, there is no need to
run these three subcommands sequentially.

### **`analyse`**

analyse reads the target graph from *`TARGETS`* files for the given
target, computes the action graph (required by e.g., **`build`**, **`install`**,
**`traverse`**), and reports the artifacts, provides, and runfiles of the
analysed target.

In short, the **`analyse`** subcommand identifies all the steps required
to **`build`** a given target without actually performing those steps.

This subcommand, issued with proper flags, can dump in JSON format
artifacts, action graph, nodes, actions, (transitive) targets (both
named and anonymous), and trees.

### **`build`**

This subcommand performs the actions contained in the action graph
computed through the **`analyse`** phase.

If building locally, the building process is performed in temporary
separate directories to allow for staging according to the logical path
described in the *`TARGETS`* file. Since artifacts are only stored in
the CAS, the user has to use either the **`install`** or **`install-cas`**
subcommand to get them.

**`jst`** allows for both local (i.e., on the same machine where **`jst`** is
used) and remote compilation (i.e., by sending requests over a TCP
connection, e.g., to a different machine, cluster or cloud
infrastructure). In case of a remote compilation, artifacts are compiled
remotely and stored in the remote CAS. **`install`** and **`install-cas`**
subcommands can be used to locally fetch and stage the desired
artifacts.

### **`install`**

The **`install`** subcommand determines which (if any) actions need to be
(re)done and issues the command to (re)run them. Then, it installs the
artifacts (stored in the local or remote CAS) of the processed target
under the given *`OUTPUT_DIR`* (set by option **`-o`**) honoring the
logical path (aka, staging). If the output path does not exist, it will
create all the necessary folders and subfolders. If files are already
present, they will be overwritten.

**`rebuild`**
-------------

This subcommand inspects if builds are fully reproducible or not (e.g.,
time stamps are used). It simply rebuilds and compares artifacts to the
cached build reporting actions with different output. To do so in a
meaningful way, it requires that previous build is already in the cache
(local or remote).

**`describe`**
--------------

The **`describe`** subcommand allows for describing the rule generating a
target. The rule is resolved in precisely the same way as during the
analysis. The doc-strings (if any) from the rule definition (if
user-defined) are reported, together with a summary of the declared
fields and their types. The multi-repository configuration is honored in
the same way as during **`analyse`** and **`build`**; in particular, the rule
definition can also reside in a git-tree root.

**`install-cas`**
-----------------

**`install-cas`** fetches artifacts from CAS (Content Addressable Storage) by
means of their *`OBJECT_ID`* (object identifier). The canonical format
of an object identifier is *`[<hash>:<size>:<type>]`*; however, when
parsing an object identifier, **`install-cas`** uses the following default
rules, to make usage simpler.

 - The square brackets are optional.
 - If the size is missing (e.g., because the argument contains no
   colon), or cannot be parsed as a number, this is not an error, and
   the value 0 is assumed. While this is almost never the correct size,
   many CAS implementations, including the local CAS of **`jst`** itself,
   ignore the size for lookups.
 - From the type, only the first letter (*`f`* for non-executable file,
   *`x`* for executable file, and *`t`* for tree) is significant; the
   rest is ignored. If the type is missing (e.g., because the argument
   contains less than two colons), or its first letter is not one of the
   valid ones, *`f`* is assumed.

Depending on whether the output path is set or not, the behavior is
different.

### Output path is omitted

If the output path is omitted, it prints the artifact content to stdout
and if the artifact is a tree, it will print a human readable
description.

### Output path is set

1. Output path does not exist

   The artifact will be staged to that path. If artifact is a file, the
   installed one will have the name of the output path. If the artifact
   is a tree, it will create a directory named like the output path,
   and will stage all the entries (subtrees included) under that
   directory.

2. Output path exists and it is a directory

   If the artifact is a tree, a directory named with the hash of tree
   itself is created under the output path, and all the entries and
   subtrees are installed inside the hash-named directory.

   If the artifact is a file, it is installed under the output path and
   named according to the hash of the artifact itself.

3. Output path exists and it is a file

   If the artifact is a file, it will replace the existing file. If the
   artifact is a tree, it will cause an error.

**`add-to-cas`**
----------------

**`add-to-cas`** adds a file or directory to the local CAS and
reports the hash (without size or type information) on stdout. If a
remote endpoint is given, the object is also uploaded there. A main
use case of this command is to simplify the setup of `"git tree"`
repositories, where it can also avoid checking out a repository of
a foreign version-control system twice.

**`traverse`**
--------------

It allows for the building and staging of requested artifacts from a
well-defined *`GRAPH_FILE`*. See **`jst-graph-file`**(5) for more
details.

This subcommand is not offered by **`jst`** itself; it is reached through
the **`backend`** subcommand, as **`jst backend traverse`**.

**`gc`**
--------

The **`gc`** subcommand triggers garbage collection of the local cache.
More precisely, it rotates the cache and CAS generations. During a
build, upon cache hit, everything related to that cache hit is uplinked
to the youngest generation; therefore, upon a call to **`gc`** everything
not referenced since the last call to **`gc`** is purged and the
corresponding disk space reclaimed.

Additionally, and before doing generation rotation,
 - left-over temporary directories (e.g., from interrupted `jst`
   invocations) are removed, and
 - large files are split and only the chunks and the information
   how to assemble the file from the chunks are kept; in this way
   disk space is saved without losing information.

As the non-rotating tasks can be useful in their own right, the
`--no-rotate` option can be used to request only the clean-up tasks
that do not lose information.

If it is necessary to remove the entire cache, the `--all` option can
be used to skip generation rotation and splitting of large files. In
this scenario, all cache generations get removed starting from the
oldest generation.

`--no-rotate` and `--all` are incompatible options.

**`eval`**
----------

The **`eval`** subcommand evaluates Jstlang code from file (use `-`
for evaluating code from stdin). Runtime data can injected via options
**`--config`** and **`--defines`**. To stop the evaluation after
preprocessing and print only the generated low-level JSON code, use the
option **`--ir`**. By default, no special restrictions on the file type
are applied during preprocessing. Restrictions can be enabled by
specifying **`--targets`**, **`--rules`**, or **`--expressions`** (all
of them implying **`--ir`**).

**`execute`**
-------------

This subcommand starts a single node remote execution service, honoring
the **`jst`** native remote protocol.

If the flag **`--compatible`** is provided, the execution service will
honor the original remote build execution protocol.

**`serve`**
-----------

This subcommand starts a service that provides target dependencies needed for a
remote execution build. It expects as its only and mandatory argument the path
to a configuration file, following the format described in
**`jst-serve-config`**(5).

**`backend`**
-------------

This subcommand is the canonical way of calling the build tool backend
directly via **`execvp`**(2), and the only supported one: which binary carries
out the build is an implementation detail. Every argument after **`backend`** is
forwarded to it unchanged, and none of the operations **`jst`** performs itself
are carried out: in particular no repository setup takes place, and none of the
options of **`jst`** are forwarded. A build called this way therefore needs a repository
build configuration of its own, for instance one generated by **`jst setup`**
and given via **`-B`**.

The options of **`jst`** itself are given between **`backend`** and the backend
subcommand; from the backend subcommand on, every argument is forwarded
unchanged. The option of interest there is **`--backend`**, naming the binary
to call, as every other option can be given to the backend directly.

ENVIRONMENT
===========

**`JST_PAGER`**, **`PAGER`**  
The pager to pipe long output through, if standard output is attached to a
terminal; **`JST_PAGER`** takes precedence. If neither is set, **`less`** is
used, provided it can be found in **`PATH`**. Setting either of them to the
empty string disables paging, as does **`--no-pager`**. Unless **`LESS`** is
already set, **`less`** is invoked such that it quits if the output fits on a
single screen and passes colors through.

**`NO_COLOR`**, **`FORCE_COLOR`**  
Control the use of ANSI escape sequences to highlight messages; see
**`--color`**.

EXIT STATUS
===========

The exit status of **`jst`** is one of the following values:

 - 0: the command completed successfully
 - 1: the command failed due to a failing build action
 - 2: the command successfully parsed all the needed files (e.g.,
   *`TARGETS`*), successfully compiled the eventually required objects,
   but the generation of some artifacts failed (e.g., a test failed).
 - 8: the command failed due to an error during analysis (e.g., missing or
   malformed *`TARGETS`* files, assertion errors during analysis, cyclic
   dependencies)
 - 16: the command failed due to some problems related to the build environment
   (e.g., local build root inaccessible, credentials for remote endpoint not
   accessible)
 - 32: the tool was invoked in a syntactically malformed way (e.g., failure
   parsing the command line)
 - 64: setup succeeded, but exec of the build backend failed
 - 65: any unspecified error occurred during setup
 - 66: unknown subcommand (internal implementation error)
 - 67: error parsing the command-line arguments
 - 68: error parsing the configuration
 - 69: error during fetch
 - 70: error during update
 - 71: error during setup

See also
========

**`jstrc`**(5),
**`jst-repo-config`**(5),
**`jst-repo-build-config`**(5),
**`jst-serve-config`**(5),
**`jst-graph-file`**(5),
**`jst-profile`**(5),
**`jst-lock`**(1),
**`jst-import-git`**(1),
**`jst-deduplicate-repos`**(1)
