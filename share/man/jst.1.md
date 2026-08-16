% JST(1) | General Commands Manual

NAME
====

jst - multi-repository configuration tool and launcher for the build tool

SYNOPSIS
========

**`jst`** \[*`OPTION`*\]... **`version`**  
**`jst`** \[*`OPTION`*\]... {**`setup`**|**`setup-env`**} \[**`--all`**\] \[*`main-repo`*\]  
**`jst`** \[*`OPTION`*\]... **`fetch`** \[**`--all`**\] \[**`--backup-to-remote`**] \[**`-o`** *`fetch-dir`*\] \[*`main-repo`*\]  
**`jst`** \[*`OPTION`*\]... **`update`** \[*`repo`*\]...  
**`jst`** \[*`OPTION`*\]... **`gc-repo`** \[**`--drop-only`**\]  
**`jst`** \[*`OPTION`*\]... **`backend`** \[*`JST_BACKEND_ARG`*\]...  
**`jst`** \[*`OPTION`*\]... {**`version`**|**`describe`**|**`analyse`**|**`build`**|**`install`**|**`install-cas`**|**`add-to-cas`**|**`rebuild`**|**`gc`**|**`eval`**|**`serve`**|**`execute`**} \[*`JST_BACKEND_ARG`*\]...  

DESCRIPTION
===========

**`jst`** is a configuration tool for the multi-repository *jst-build*
build system. It can be used both standalone and as a launcher for
**`jst_backend`**(1).

The tool performs specific operations, based on the invoked subcommand,
on repositories described in a configuration file. All subcommands
operate at the level of *workspace roots* deduced from the given
repository descriptions. See **`jst-repo-config`**(5) for more
details on the input format.

OPTIONS
=======

General options
---------------

**`-h`**, **`--help`**  
Output a usage message and exit.

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

**`-D`**, **`--defines`** *`JSON`*  
Defines, via an in-line JSON object, an overlay configuration for
**`jst_backend`**(1); if used as a launcher for a subcommand known to support
**`--defines`**, this defines value is forwarded, otherwise it is
ignored. If **`-D`** is given several times, the **`-D`** options
overlay (in the sense of *`map_union`*) in the order they are given on
the command line.

**`--local-build-root`** *`PATH`*  
Root for local CAS, cache, and build directories. The path will be
created if it does not exist already. This option overwrites any values
set in the **`jstrc`**(5) file.  
Default: path *`".cache/jst"`* in user's home directory.

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

**`-L`**, **`--local-launcher`** *`JSON_ARRAY`*  
JSON array with the list of strings representing the launcher to prepend
actions' commands before being executed locally.  
Default: *`["env", "--"]`*.

**`--distdir`** *`PATH`*  
Directory to look for distfiles before fetching. If given, this will be
the first place distfiles are looked for. This option can be given
multiple times to specify a list of distribution directories that are
used for lookup in the order they appear on the command line.
Directories specified via this option will be appended to the ones set
in the **`jstrc`**(5) file.  
Default: the single file path *`".distfiles"`* in user's home directory.

**`--main`** *`NAME`*  
The repository to take the target from.

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

**`--plain-log`**  
Do not use ANSI escape sequences to highlight messages.

**`--log-append`**  
Append messages to log file instead of overwriting existing.

**`--no-fetch-ssl-verify`**  
Disable the default peer SSL certificate verification step when fetching
archives (for which we verify the hash anyway) from remote.

**`--fetch-cacert`** *`PATH`*  
Path to the CA certificate bundle containing one or more certificates to
be used to peer verify archive fetches from remote.

**`-r`**, **`--remote-execution-address`** *`NAME`*:*`PORT`*  
Address of a remote execution service. This is used as an intermediary fetch
location for archives, between local CAS (or distdirs) and the network.

**`--remote-instance-name`** *`NAME`*
Value to pass as `instance_name` in the remote execution API.  

**`-R`**, **`--remote-serve-address`** *`NAME`*:*`PORT`*  
Address of a **`jst_backend`** **`serve`** service. This is used as intermediary fetch
location for Git commits, between local CAS and the network.

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

**`--fetch-absent`**  
Try to make available all repositories, including those marked as absent.
This option cannot be set together with **`--compatible`**.

**`--compatible`**  
At increased computational effort, be compatible with the original remote build
execution protocol. If a remote execution service address is provided, this 
option can be used to match the artifacts expected by the remote endpoint.

**`--backend`** *`PATH`*  
Name of the backend binary in *`PATH`* or path to the backend binary.  
Default: *`"jst_backend"`*.

**`--just`** *`PATH`*  
Legacy option to specify the backend binary, same as **`--backend`**.

**`--rc`** *`PATH`*  
Path to the jstrc file to use. See **`jstrc`**(5) for more
details.  
Default: file path *`".jstrc"`* in the user's home directory.

**`--dump-rc`** *`PATH`*  
Dump the effective rc, i.e., the rc after overlaying all applicable auxiliary
files specified in the `"rc files"` field, to the specified file. In this
way, an rc can be made self-contained in preparation for committing it to
a repository.

**`--git`** *`PATH`*  
Path to the git binary in *`PATH`* or path to the git binary. Used in
the rare instances when shelling out to git is needed.  
Default: *`"git"`*.

**`--norc`**  
Option to prevent reading any **`jstrc`**(5) file.

**`-j`**, **`--jobs`** *`NUM`*  
Number of jobs to run.  
Default: Number of cores.  

Authentication options
----------------------

Only TLS and mutual TLS (mTLS) are supported.
They mirror the **`jst_backend`**(1) options.

**`--tls-ca-cert`** *`PATH`*  
Path to a TLS CA certificate that is trusted to sign the server
certificate.

**`--tls-client-cert`** *`PATH`*  
Path to a TLS client certificate to enable mTLS. It must be passed in
conjunction with **`--tls-client-key`** and **`--tls-ca-cert`**.

**`--tls-client-key`** *`PATH`*  
Path to a TLS client key to enable mTLS. It must be passed in
conjunction with **`--tls-client-cert`** and **`--tls-ca-cert`**.

SUBCOMMANDS
===========

**`version`**
-------------

Print on stdout a JSON object providing version information for this
tool itself. The version information for jst is in the same format that
also **`jst_backend`** uses.

**`setup`**|**`setup-env`**
---------------------------

These subcommands fetch all required repositories and generate an
appropriate multi-repository **`jst_backend`** configuration file. The resulting
file is stored in CAS and its path is printed to stdout. See
**`jst_backend-repo-config`**(5) for more details on the resulting
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
working directory when **`jst_backend`** is invoked. In this way, working on a
checkout of that repository is possible, while having all of its
dependencies properly set up. In the case of **`setup`**, the workspace root
of the main repository is taken as-is into the output configuration
file.

fetch
-----

This subcommand prepares all archive-type and **`"git tree"`** workspace roots
for an offline build by fetching all their required source files from the
specified locations given in the input configuration file or ensuring the 
specified tree is present in the Git cache, respectively. Any subsequent
**`jst`** or **`jst_backend`** invocations containing fetched archive or 
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

update
------

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

gc-repo
-------

This subcommand rotates the generations of the repository cache.
Every root used is added to the youngest generation. Therefore upon
a call to **`gc-repo`** all roots are cleaned up that were not used
since the last **`gc-repo`**.

If **`--drop-only`** is given, only the old generations are cleaned up,
without rotation. In this way, storage can be reclaimed; this might be
necessary as no perfect sharing happens between the repository generations.

backend
--

This subcommand is used as the canonical way of specifying **`jst_backend`**
arguments and calling **`jst_backend`** via **`execvp`**(2). Any subsequent argument
is unconditionally forwarded to **`jst_backend`**. For *known* subcommands
(**`version`**, **`describe`**, **`analyse`**, **`build`**, **`install`**, 
**`install-cas`**, **`add-to-cas`**, **`rebuild`**, **`gc`**, **`eval`**,
**`serve`**, **`execute`**), the
**`jst setup`** step is performed first for those commands accepting a
configuration (**`describe`**, **`analyse`**, **`build`**, **`install`**,
**`rebuild`**) and the produced configuration is prefixed to the provided
arguments. The main repository for the **`setup`** step can be provided in
the configuration or on the command line. If no main repository is
provided, the lexicographical first repository from the configuration is
used.

All logging arguments given to **`jst`** are passed to **`jst_backend`** as early
arguments. If log files are provided, an unconditional
**`--log-append`** argument is passed as well, which ensures no log
messages will get overwritten.

The **`--local-launcher`** argument is passed to **`jst_backend`** as early
argument for those *known* subcommands that accept it (analyse, build,
install, rebuild, execute).

The **`--remote-execution-address`**, **`--remote-instance-name`**,
**`--compatible`**, and
**`--remote-serve-address`** arguments are passed to **`jst_backend`** as early
arguments for those *known* subcommands that accept them
(describe, analyse, build, install-cas, add-to-cas, install, rebuild).
They are *not* forwarded to **`serve`** and **`execute`**, which are servers
rather than remote-execution clients and take their remote-execution
settings from their own configuration or command line.

The *authentication options* given to **`jst`** are passed to **`jst_backend`** as
early arguments for those *known* subcommands that accept them, according to
**`jst_backend`**(1).

**`version`**|**`describe`**|**`analyse`**|**`build`**|**`install`**|**`install-cas`**|**`add-to-cas`**|**`rebuild`**|**`gc`**|**`eval`**|**`serve`**|**`execute`**
-------------------------------------------------------------------------------------------------------------------------------------------------------------------

This subcommand is the explicit way of specifying *known* **`jst_backend`**
subcommands and calling **`jst_backend`** via **`execvp`**(2). The same description
as for the **`backend`** subcommand applies.

**`gc-repo`**
-------------

Rotate the repository-root generations. In this way, all repository
roots not needed since the last call to **`gc-repo`** are purged
and the corresponding disk space reclaimed.


EXIT STATUS
===========

The exit status of **`jst`** is one of the following values:

 - 0: the command completed successfully
 - 64: setup succeeded, but exec failed
 - 65: any unspecified error occurred in jst
 - 66: unknown subcommand (internal implementation error of **`jst`**)
 - 67: error parsing the command-line arguments
 - 68: error parsing the configuration
 - 69: error during fetch
 - 70: error during update
 - 71: error during setup

Any other exit code that does not have bit 64 set is a status value from
**`jst_backend`**, if **`jst`** is used as a launcher. See **`jst_backend`**(1) for more
details.

See also
========

**`jstrc`**(5),
**`jst-repo-config`**(5),
**`jst_backend-repo-config`**(5),
**`jst_backend`**(1)
