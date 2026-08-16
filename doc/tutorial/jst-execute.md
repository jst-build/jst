Single-node remote execution service: `jst execute`
===================================================

`jst execute` starts a single-node remote build execution service in
the environment in which the command has been issued. Being able to easily
create a remote build execution service can improve the development
experience wherever the build environment (and the cache) can or should be
shared among developers. For example (and certainly not limited to):

- when developers build on the same machine. It will allow multiple
  users to build in the same environment and share the cache, thus
  avoiding duplicated work.

- to quickly set up a testing environment that can be used by other
  developers.

For the sake of completeness, these are the files used to compile the
examples:

```
latex-hello-world/
+--hello.tex
+--repos.json
+--TARGETS
```

They read as follows.

File `repos.json`:

``` {.json srcname="repos.json"}
{
  "main": "tutorial",
  "repositories": {
    "latex-rules": {
      "repository": {
        "type": "git",
        "branch": "master",
        "commit": "ffa07d6f3b536f1a4b111c3bf5850484bb9bf3dc",
        "repository": "https://github.com/just-buildsystem/rules-typesetting"
      }
    },
    "tutorial": {
      "repository": {
        "type": "file",
        "path": "."
      },
      "bindings": {"latex-rules": "latex-rules"}
    }
  }
}
```

File `TARGETS`:

``` {.jsonnet srcname="TARGETS"}
{
  tutorial: {
    type: @'latex-rules//latex:latexmk',
    main: ['hello'],
    srcs: ['hello.tex'],
  },
}
```

File `hello.tex`:

``` {.tex srcname="hello.tex"}
\documentclass[a4paper]{article}

\author{jst-build developers}
\date{}
\title {jst execute}

\begin{document}
\maketitle

Hello from \LaTeX!
\end{document}
```

Simple usage of `jst execute` in the same environment
-----------------------------------------------------

In this first example, we simply call `jst execute`, and the
environment of the caller is made available. We therefore recommend having
a dedicated, unprivileged `build` user to run the execution service. In the
following, we will use `%` to indicate the prompt of the `build` user, and
`$` for a *normal* user.

To enable such a single-node execution service, it is sufficient to
type in one shell (as the `build` user):

``` sh
% jst execute -p <N>
```

Here `<N>` is a port number that is expected to be available.
By default, the native `git`-based protocol will be used, but it
is also possible to use the original protocol with `sha256` hashes
by providing the `--compatible` option.

``` sh
% jst execute --compatible -p <N>
```

This is particularly useful when providing the remote-execution service
to a different build tool.

To use it, as a *normal* user, type in a different shell:

``` sh
$ jst [...] -r localhost:<N>
```

Let's run these commands to understand the output.

``` sh
% jst execute -p 8080
INFO: execution service started: {"interface":"127.0.0.1","pid":4911,"port":8080}
```

Once the execution service is started, it logs three essential pieces of
information:

- which interface is used (in this case, the default one, which is the
  loopback device)
- the PID (which will change on every run)
- the port in use

To make use of the execution service, run from a different shell:

``` sh
$ jst [...] -r localhost:8080
```

### Use a random port

If we don't need (or know) a fixed port number, we can simply omit the
`-p` option. In this case, `jst execute` will listen on a random
free port.

``` sh
% jst execute
INFO: execution service started: {"interface":"127.0.0.1","pid":7217,"port":33841}
```

The port number can be different each time we invoke the
above command.

Finally, to connect to the remote endpoint, type:

``` sh
$ jst [...] -r localhost:33841
```

### Info file

Copying and pasting port numbers and PIDs can be error-prone, or
unworkable if we manage several execution service instances. Therefore,
the invocation of `jst execute` can be decorated with the option
`--info-file <PATH>`, which stores the interface, PID, and port bound to
the running instance in `<PATH>`, in JSON format. The user can then easily
parse this file to extract the required information.

For example:

``` sh
% jst execute --info-file /tmp/foo.json
INFO: execution service started: {"interface":"127.0.0.1","pid":7680,"port":44115}
```

``` sh
$ cat /tmp/foo.json
{"interface":"127.0.0.1","pid":7680,"port":44115}
```

Please note that the info file will *not* be deleted automatically
when the user terminates the service. The user is responsible for
removing it from the file system.

### Enable mTLS

It is worth mentioning that mTLS must be enabled when the execution
service starts; it cannot be activated (or deactivated) while the
instance is running.

``` sh
% jst execute [...] --tls-ca-cert <path_to_CA_cert> --tls-server-cert <path_to_server_cert> --tls-server-key <path_to_server_key>
```

When a client connects, it must pass the same CA certificate, together
with its own certificate and private key, signed by that certificate
authority.

``` sh
$ jst [...] --tls-ca-cert <path_to_CA_cert> --tls-client-cert <path_to_client_cert> --tls-client-key <path_to_client_key>
```

#### How to generate self-signed certificates

This section does not claim to be an exhaustive guide to the
generation and management of certificates, which is well beyond the
scope of this tutorial. We only want to provide a minimal reference to
let users start using mTLS and gain the benefits of mutual
authentication.

##### Certification Authority certificate

As a first step, we need a Certification Authority certificate (`ca.crt`):

``` sh
% openssl req -x509 -sha256 -nodes -days 365 -newkey rsa:4096 -keyout ca.key -out ca.crt
```

##### Server certificate and key

If the clients will connect using the loopback device, i.e., the users
are logged in on the same machine where `jst execute` will run, the
*server certificates* can be generated with the following commands:

``` sh
% openssl req -new -nodes -newkey rsa:4096 -keyout server.key -out server.csr -subj "/CN=localhost"
% openssl x509 -req -days 365 -in server.csr -CA ca.crt -CAkey ca.key -set_serial 0 -out server.crt
% rm server.csr
```

On the other hand, if the clients will connect from a different
machine, and `jst execute` will use a different interface (see
[Expose a particular interface](#expose-a-particular-interface) below),
the steps are a bit more involved. We need an additional configuration
file in which we state the IP address of the interface used. For example,
if the interface IP address is `192.168.1.14`, we will write:

``` sh
% cat << EOF > ssl-ext-x509.cnf
[v3_ca]
subjectAltName = IP.1:192.168.1.14
EOF
```

Then the certificate and key pair can be obtained with:

``` sh
% openssl req -new -nodes -newkey rsa:4096 -keyout server.key -out server.csr -subj "/CN=localhost"
% openssl x509 -req -days 365 -in server.csr -CA ca.crt -CAkey ca.key -set_serial 0 -out server.crt -extensions v3_ca -extfile ssl-ext-x509.cnf
% rm server.csr
```

##### Client certificate and key

The client, which needs the `ca.crt` and `ca.key` files, can run the
following:

``` sh
$ openssl req -new -nodes -newkey rsa:4096 -keyout client.key  -out client.csr
$ openssl x509 -req -days 365 -signkey client.key -in client.csr -CA ca.crt -CAkey ca.key -set_serial 01 -out client.crt
$ rm client.csr
```

### Expose a particular interface

To use an interface other than the loopback one, we have to name
it with the `-i` option:

``` sh
$ jst execute -i 192.168.1.14 -p 8080 --tls-ca-cert <path_to_CA_cert> --tls-server-cert <path_to_server_cert> --tls-server-key <path_to_server_key>
INFO: execution service started: {"interface":"192.168.1.14","pid":7917,"port":8080}
```

If the interface is accessible from another machine, it is also
recommended to enable mutual TLS (mTLS) authentication.

Managing multiple build environments
------------------------------------

Since multiple instances of `jst execute` can run in parallel
(listening on different ports), the same machine can be the worker for
various projects. However, to avoid conflicts between the dependencies
and to guarantee a clean environment for each project, it is
recommended that `jst execute` is invoked from within a container
or a chroot environment.

In the following sections, we will set up, step by step, a dedicated
execution service for compiling LaTeX documents in these two
scenarios.

### How to run `jst execute` inside a chroot environment

#### TL;DR

- create a suitable chroot environment
- chroot into it
- run `jst execute` from there
- in a different shell, `jst build -r <interface>:<port>`

#### Full LaTeX chroot: walkthrough

This short tutorial will use `debootstrap` and `schroot` to create and
enter the chroot environment. Of course, different strategies or programs
can be used.

##### Prepare the root file system

Install Debian bullseye in the directory `/chroot/bullseye-latex`:

``` sh
sudo debootstrap bullseye /chroot/bullseye-latex
```

##### Create a configuration file

`schroot` needs a proper configuration file, which can be generated as
follows:

``` sh
$ echo "[bullseye-latex]
description=bullseye latex env
directory=/chroot/bullseye-latex
root-users=$(whoami)
users=$(whoami)
type=directory" | sudo tee /etc/schroot/chroot.d/bullseye-latex
```

Note that `type=directory`, apart from performing the necessary
bindings, will make `$HOME` shared between the host and the chroot
environment. While this can be useful for sharing artifacts, the user
should specify a `--local-build-root` (a.k.a. the cache root) different
from the default one, to avoid conflicts between the host and the
chroot environment.

##### Install required packages in the chroot environment

`schroot` also allows running commands inside the environment by
specifying them after the `--`:

``` sh
$ schroot -c bullseye-latex -u root -- sh -c 'apt update && apt install -y texlive-full'
```

##### Start the execution service

To start the execution service inside the chroot environment, run:

``` sh
$ schroot -c bullseye-latex -- /bin/jst execute --local-build-root ~/.cache/chroot/bullseye-latex -p 8080
```

We assume that the binary `jst` is available in the chroot
environment at the path `/bin/jst`; the easiest way to achieve
this is to copy a statically linked binary into it, as described for the
Docker container below.

Since `$HOME` is shared, specifying a local build root (a.k.a. the cache
root) different from the default is highly recommended. For
convenience, we also set a port (using the flag `-p`) that the
execution service will listen on.

If the chosen port is available, the following output should be
produced (note that the PID might be different):

``` sh
INFO: execution service started: {"interface":"127.0.0.1","pid":48880,"port":8080}
```

For example, let's compile the example listed in the introduction:

``` sh
$ jst -C repos.json install -o . -r localhost:8080
```

which should report:

``` sh
INFO: Found 2 repositories to set up
INFO: Requested target 'tutorial//doc/jst-execute/latex-hello-world:tutorial' with config: {}
INFO: Discovered 1 actions, 0 trees, 1 blobs
INFO: Processed 1 actions, 0 cache hits.
INFO: Artifacts can be found in:
        /tmp/work/doc/jst-execute/latex-hello-world/hello.pdf [25e05d3560e344b0180097f21a8074ecb0d9f343:37614:f]
```

In the shell where `jst execute` is running, this line should have
appeared, confirming that the compilation happened on the remote side:

``` sh
INFO (execution-service): Execute 6237d87faed1ec239512ad952eeb412cdfab372562
```

### How to start `jst execute` inside a Docker container

Building inside a container is another strategy to ensure no
undeclared dependencies are pulled in, and to build in a fixed
environment.

We will replicate what we did for the chroot environment and create a
suitable Docker image.

#### Build a suitable Docker image

Let's write a `Dockerfile` that has `jst execute` as its
`ENTRYPOINT`. We assume the binary `jst` is available inside the
container at the path `/bin/jst`. The easiest way is to copy a
statically linked binary into the container.

``` {.docker srcname="Dockerfile"}
FROM debian:bullseye-slim

COPY ./jst /bin/jst

RUN apt update
RUN apt install -y --no-install-recommends texlive-full

ENTRYPOINT ["/bin/jst", "execute"]
```

We build the image with:

``` sh
$ sudo docker image build -t bullseye-latex .
```

Finally, we can start the execution service:

``` sh
$ docker run --network host --name execute-latex -p 8080
```

From a different shell, we can build the LaTeX hello world example
listed in the introduction by running:

``` sh
$ jst -C repos.json install -o . -r localhost:8080
```

Note that the cache that `jst execute` populates is confined within
the container. The cache is lost if the container is restarted (or the
machine is rebooted). If you want the cache to survive the container's
life cycle, you can bind a "host directory" into the container as
follows:

``` sh
$ docker run --network host --name execute-latex --mount type=bind,source="${HOME}/.cache",target=/cache bullseye-latex -p 8080 --local-build-root /cache/docker/latex
```
