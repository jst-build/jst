# `jst` Build Dependencies

## Debian 12 / Ubuntu 24.04 and newer

Complete dependencies for a build linked against system dependencies:

```sh
apt update
apt install -y \
    g++ \
    wget \
    python3 \
    pkgconf \
    protobuf-compiler \
    protobuf-compiler-grpc \
    libprotobuf-dev \
    libgrpc++-dev \
    libfmt-dev \
    nlohmann-json3-dev \
    libgit2-dev \
    libssl-dev \
    libcli11-dev \
    libmsgsl-dev \
    libarchive-dev \
    libcurl4-openssl-dev

# build with jst
jst build
# or bootstrap via
./bin/bootstrap.py
```

Complete dependencies for a fully bundled build:

```sh
apt update
apt install -y g++ wget python3 git patch unzip

# build with jst
jst -C etc/bundled.json build
# or bootstrap via
BUNDLED=YES ./bin/bootstrap.py
```

Additional dependencies for running tests:

```sh
apt install -y jq git libcatch2-dev     # 'catch2' on older distros

# run tests
jst --main tests build
```

Additional dependencies needed for building with man pages:

```sh
apt install -y pandoc

# build ALL target with man pages
jst build ALL -D'{"BUILD_MANPAGES":true}'
```