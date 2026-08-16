#!/usr/bin/env python3
# Copyright 2022 Huawei Cloud Computing Technology Co., Ltd.
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

import hashlib
import json
import os
import subprocess
import sys
import tempfile
import platform

from pathlib import Path
from concurrent.futures import ThreadPoolExecutor

from typing import Any, Dict, List, Optional, cast

# generic JSON type that avoids getter issues; proper use is being enforced by
# return types of methods and typing vars holding return values of json getters
Json = Dict[str, Any]

# path within the repository (constants)

DEBUG = os.environ.get("DEBUG")

REPOS: str = "etc/repos.json"
MAIN_MODULE: str = ""
MAIN_TARGET: str = "jst_backend"
MAIN_STAGE: str = "src/buildtool/main/jst_backend"
BOOTSTRAP_MODULE: str = os.environ.get("BOOTSTRAP_MODULE", "")
BOOTSTRAP_TARGET: str = os.environ.get("BOOTSTRAP_TARGET", "ALL")

LOCAL_LINK_DIRS_MODULE: str = "src/buildtool/main"
LOCAL_LINK_DIRS_TARGET: str = "jst_backend"

# architecture related configuration (global variables)
g_CONF: Json = {}
if 'BOOTSTRAP_CONF' in os.environ:
    g_CONF = json.loads(os.environ['BOOTSTRAP_CONF'])

if "BUNDLED" not in os.environ:
    g_CONF["ADD_CFLAGS"] = ["-Wno-error", "-Wno-pedantic"] + g_CONF.get(
        "ADD_CFLAGS", [])
    g_CONF["ADD_CXXFLAGS"] = ["-Wno-error", "-Wno-pedantic"] + g_CONF.get(
        "ADD_CXXFLAGS", [])

ARCHS: Dict[str, str] = {
    'i686': 'x86',
    'x86_64': 'x86_64',
    'arm': 'arm',
    'aarch64': 'arm64'
}
if "OS" not in g_CONF:
    g_CONF["OS"] = platform.system().lower()
if "ARCH" not in g_CONF:
    MACH = platform.machine()
    if MACH in ARCHS:
        g_CONF["ARCH"] = ARCHS[MACH]
if 'SOURCE_DATE_EPOCH' in os.environ:
    g_CONF['SOURCE_DATE_EPOCH'] = int(os.environ['SOURCE_DATE_EPOCH'])

g_CONFIG_PATHS: List[str] = []
if 'PKG_CONFIG_PATH' in os.environ:
    g_CONFIG_PATHS += [os.environ['PKG_CONFIG_PATH']]

ENV: Dict[str, str] = g_CONF.setdefault("ENV", {})
if 'PKG_CONFIG_PATH' in ENV:
    g_CONFIG_PATHS += [ENV['PKG_CONFIG_PATH']]

g_LOCALBASE: str = "/"
if 'LOCALBASE' in os.environ:
    g_LOCALBASE = os.environ['LOCALBASE']
    g_CONF["LOCALBASE"] = g_LOCALBASE
    pkg_paths = ['lib/pkgconfig', 'share/pkgconfig']
    if 'PKG_PATHS' in os.environ:
        pkg_paths = json.loads(os.environ['PKG_PATHS'])
    g_CONFIG_PATHS += [os.path.join(g_LOCALBASE, p) for p in pkg_paths]

if g_CONFIG_PATHS:
    ENV['PKG_CONFIG_PATH'] = ":".join(g_CONFIG_PATHS)

CONF_STRING: str = json.dumps(g_CONF)

OS: str = g_CONF["OS"]
ARCH: str = g_CONF["ARCH"]
g_AR: str = "ar"
g_CC: str = "cc"
g_CXX: str = "c++"
g_CFLAGS: List[str] = []
g_CXXFLAGS: List[str] = []
g_FINAL_LDFLAGS: List[str] = ["-Wl,-z,stack-size=8388608"]

if "TOOLCHAIN_CONFIG" in g_CONF and "FAMILY" in g_CONF["TOOLCHAIN_CONFIG"]:
    if g_CONF["TOOLCHAIN_CONFIG"]["FAMILY"] == "gnu":
        g_CC = "gcc"
        g_CXX = "g++"
    elif g_CONF["TOOLCHAIN_CONFIG"]["FAMILY"] == "clang":
        g_CC = "clang"
        g_CXX = "clang++"

if "AR" in g_CONF:
    g_AR = g_CONF["AR"]
if "CC" in g_CONF:
    g_CC = g_CONF["CC"]
if "CXX" in g_CONF:
    g_CXX = g_CONF["CXX"]
if "ADD_CFLAGS" in g_CONF:
    g_CFLAGS = g_CONF["ADD_CFLAGS"]
if "ADD_CXXFLAGS" in g_CONF:
    g_CXXFLAGS = g_CONF["ADD_CXXFLAGS"]
if "FINAL_LDFLAGS" in g_CONF:
    g_FINAL_LDFLAGS += g_CONF["FINAL_LDFLAGS"]

BOOTSTRAP_CC: List[str] = [g_CXX] + g_CXXFLAGS + [
    "-std=c++20", "-DBOOTSTRAP_BUILD_TOOL"
]

# relevant directories (global variables)

g_SRCDIR: str = os.getcwd()
g_WRKDIR: Optional[str] = None
g_DISTDIR: List[str] = []


def git_hash(content: bytes) -> str:
    header = "blob {}\0".format(len(content)).encode('utf-8')
    h = hashlib.sha1()
    h.update(header)
    h.update(content)
    return h.hexdigest()


def get_checksum(filename: str) -> str:
    with open(filename, "rb") as f:
        data = f.read()
    return git_hash(data)


def get_archive(*, distfile: str, fetch: str) -> str:
    # Fetch the archive, if necessary. Return path to archive
    for d in g_DISTDIR:
        candidate_path = os.path.join(d, distfile)
        if os.path.isfile(candidate_path):
            return candidate_path
    # Fetch to bootstrap working directory
    fetch_dir: str = os.path.join(cast(str, g_WRKDIR), "fetch")
    os.makedirs(fetch_dir, exist_ok=True)
    target: str = os.path.join(fetch_dir, distfile)
    subprocess.run(["wget", "-O", target, fetch])
    return target


def quote(args: List[str]) -> str:
    return ' '.join(["'" + arg.replace("'", "'\\''") + "'" for arg in args])


def run(cmd: List[str], *, cwd: str, **kwargs: Any) -> None:
    print("Running %r in %r" % (cmd, cwd), flush=True)
    subprocess.run(cmd, cwd=cwd, check=True, **kwargs)


def setup_deps(repos_config: str) -> Json:
    # unpack all dependencies and return a list of
    # additional C++ flags required
    with open(repos_config) as f:
        config = json.load(f)["repositories"]
    include_location: str = os.path.join(cast(str, g_WRKDIR), "dep_includes")
    link_flags: List[str] = []
    os.makedirs(include_location)
    for repo, total_desc in config.items():
        desc: Optional[Json] = total_desc.get("repository", {})
        if not isinstance(desc, dict):
            # Indirect definition; we will set up the repository at the
            # resolved place, which also has to be part of the global
            # repository description.
            continue
        name = repo
        if repo.startswith("jst/"):
            name = repo.split('/')[1]
        if name in ["ssl", "fmt", "gsl", "json", "cli11", "libgit2"] and desc.get("type") in ["archive", "zip"]:
            fetch = desc["fetch"]
            distfile = desc.get("distfile") or os.path.basename(fetch)
            archive = get_archive(distfile=distfile, fetch=fetch)
            actual_checksum = get_checksum(archive)
            expected_checksum = desc.get("content")
            if actual_checksum != expected_checksum:
                print("Checksum mismatch for %r. Expected %r, found %r" %
                      (archive, expected_checksum, actual_checksum))
            print("Unpacking %r from %r" % (name, archive))
            unpack_location: str = os.path.join(cast(str, g_WRKDIR), "deps",
                                                name)
            os.makedirs(unpack_location)
            if desc["type"] == "zip":
                subprocess.run(["unzip", "-d", ".", archive],
                               cwd=unpack_location,
                               stdout=subprocess.DEVNULL)
            else:
                subprocess.run(["tar", "xf", archive], cwd=unpack_location)
            subdir = os.path.join(unpack_location, desc.get("subdir", "."))
            os_map = {}
            arch_map = {}
            build = ""

            if name == "ssl":
                include_dir = os.path.join(subdir, "src/include/openssl")
                os.symlink(os.path.normpath(include_dir),
                           os.path.join(include_location, "openssl"))
                arch_map = {"arm64": "aarch64"}
                build = "{cxx} {cxxflags} -I . -I src/include -c `find . '(' -ipath './src/crypto/*.cc' -o -ipath './src/gen/crypto/*.cc' -o -ipath './src/crypto/*.S' -o -ipath './src/gen/bcm/*.S' -o -ipath './src/gen/crypto/*.S' -o -ipath './src/third_party/fiat/asm/*.S' ')' -type f ! -ipath '*_test.*' ! -ipath '*/test/*'` && {ar} cqs libcrypto.a *.o"
            elif name == "fmt":
                include_dir = os.path.join(subdir, "include/fmt")
                os.symlink(os.path.normpath(include_dir),
                           os.path.join(include_location, "fmt"))
                build = "cd src && {cxx} {cxxflags} -I ../include -c os.cc format.cc && {ar} cqs ../libfmt.a *.o"
            elif name == "gsl":
                os.symlink(os.path.normpath(subdir),
                           os.path.join(include_location, "gsl"))
            elif name == "json":
                os.symlink(os.path.normpath(subdir),
                           os.path.join(include_location, "nlohmann"))
            elif name == "cli11":
                include_dir = os.path.join(subdir, "include/CLI")
                os.symlink(os.path.normpath(include_dir),
                           os.path.join(include_location, "CLI"))
            elif name == "libgit2":
                include_dir = os.path.join(subdir, "include/git2")
                include_file = os.path.join(subdir, "include/git2.h")
                os.symlink(os.path.normpath(include_dir),
                           os.path.join(include_location, "git2"))
                os.symlink(os.path.normpath(include_file),
                           os.path.join(include_location, "git2.h"))

            run([
                    "sh", "-c", build.format(
                        os=os_map.get(OS, OS),
                        arch=arch_map.get(ARCH, ARCH),
                        cc=g_CC,
                        cxx=g_CXX,
                        ar=g_AR,
                        cflags=quote(g_CFLAGS),
                        cxxflags=quote(g_CXXFLAGS),
                    )
                ],
                cwd=subdir)
            link_flags.extend(["-L", subdir])

    link_flags += ["-lfmt", "-lcrypto", "-pthread"]

    return {"include": ["-I", include_location], "link": link_flags}


def bootstrap(repos_config : str, is_system_build: bool) -> None:
    if is_system_build:
        print("Bootstrap build in %r from sources %r against LOCALBASE %r" %
              (g_WRKDIR, g_SRCDIR, g_LOCALBASE))
    else:
        print("Bootstrapping in %r from sources %r, taking files from %r" %
              (g_WRKDIR, g_SRCDIR, g_DISTDIR))
    os.makedirs(cast(str, g_WRKDIR), exist_ok=True)
    with open(os.path.join(cast(str, g_WRKDIR), "build-conf.json"), 'w') as f:
        json.dump(g_CONF, f, indent=2)
    ro_srcdir: str = g_SRCDIR
    objdir: str = os.path.normpath(os.path.join(cast(str, g_WRKDIR), "src"))
    os.makedirs(objdir)
    empty_dir: str = os.path.join(cast(str, g_WRKDIR), "empty_directory")
    os.makedirs(empty_dir)

    # Phase 1: build minimal bootstrap-jst_backend
    #          (for analysing targets and dumping action graph)
    dep_flags = setup_deps(os.path.join(ro_srcdir, repos_config))
    # handle proto
    flags = ["-I", ro_srcdir] + dep_flags["include"] + [
        "-I", os.path.join(g_LOCALBASE, "include"),
        "-I", os.path.join(ro_srcdir, 'extern/justlang/src')
    ]
    cpp_files: List[str] = []
    for root, dirs, files in os.walk(ro_srcdir):
        if 'test' in dirs:
            dirs.remove('test')
        if 'execution_api' in dirs:
            dirs.remove('execution_api')
        if 'other_tools' in dirs:
            dirs.remove('other_tools')
        if 'archive' in dirs:
            dirs.remove('archive')
        if 'computed_roots' in dirs:
            dirs.remove('computed_roots')
        if 'tree_structure' in dirs:
            dirs.remove('tree_structure')
        if 'tree_operations' in dirs:
            dirs.remove('tree_operations')
        if 'examples' in dirs:
            dirs.remove('examples')
        if 'etc/rules' in root:
            continue
        if 'extern/justlang' in root and not 'src/justlang' in root:
            continue
        base = os.path.relpath(root, ro_srcdir)
        for f in files:
            if f.endswith(".cpp"):
                cpp_files.append(os.path.join(base, f))
    object_files: List[str] = []
    with ThreadPoolExecutor(max_workers=1 if DEBUG else None) as ts:
        for f in cpp_files:
            obj_file_name = f[:-len(".cpp")] + ".o"
            object_files.append(obj_file_name)
            os.makedirs(os.path.dirname(os.path.join(objdir, obj_file_name)), exist_ok=True)
            cmd: List[str] = BOOTSTRAP_CC + flags + [
                "-c", os.path.join(ro_srcdir, f), "-o", obj_file_name
            ]
            ts.submit(run, cmd, cwd=objdir)
    bootstrap_jst_backend: str = os.path.join(cast(str, g_WRKDIR), "bootstrap-jst_backend")
    final_cmd: List[str] = BOOTSTRAP_CC + g_FINAL_LDFLAGS + [
        "-o", bootstrap_jst_backend
    ] + object_files + dep_flags["link"]
    run(final_cmd, cwd=objdir)

    # Phase 2:
    # - run ./bin/jst.py to setup dependencies and generate backend config
    # - run bootstrap-jst_backend to transform generate action graph
    # - run bootstrap traverser to build feature-complete jst_backend
    CONF_FILE: str = os.path.join(cast(str, g_WRKDIR), "repo-conf.json")
    LOCAL_ROOT: str = os.path.join(cast(str, g_WRKDIR), ".jst")
    os.makedirs(LOCAL_ROOT, exist_ok=True)
    distdirs = " --distdir=".join(g_DISTDIR)
    run([
        "sh", "-c",
        "cp `./bin/jst.py --always-file -C %s --local-build-root=%s --distdir=%s setup jst` %s"
        % (repos_config, LOCAL_ROOT, distdirs, CONF_FILE)
    ],
        cwd=ro_srcdir)
    GRAPH: str = os.path.join(cast(str, g_WRKDIR), "graph.json")
    TO_BUILD: str = os.path.join(cast(str, g_WRKDIR), "to_build.json")
    run([
        bootstrap_jst_backend, "analyse", "-C", CONF_FILE, "-D", CONF_STRING,
        "--dump-graph", GRAPH, "--dump-artifacts-to-build", TO_BUILD,
        MAIN_MODULE, MAIN_TARGET
    ],
        cwd=ro_srcdir)
    if DEBUG:
        traverser = "./bin/bootstrap-traverser.py"
    else:
        traverser = "./bin/parallel-bootstrap-traverser.py"
    traverser = os.path.join(ro_srcdir, traverser)
    run([
        traverser, "-C", CONF_FILE, "--default-workspace", objdir, GRAPH,
        TO_BUILD
    ],
        cwd=objdir)

    # Phase 3: run feature-complete jst_backend on backend config build final jst
    OUT: str = os.path.join(cast(str, g_WRKDIR), "out")
    run([
        "./out-boot/%s" %
        (MAIN_STAGE, ), "install", "-C", CONF_FILE, "-D", CONF_STRING, "-o",
        OUT, BOOTSTRAP_MODULE, BOOTSTRAP_TARGET
    ],
        cwd=objdir)

def write_bootstrap_config(system_deps : set[str]) -> str:
    # write a bootstrap repository config based on the bundled configuration
    # with specified dependencies remapped to the system
    imports : list[str] = []
    bundled_file = os.path.join(g_SRCDIR, 'etc/bundled.json')
    with open(bundled_file) as f:
        repos_data = json.load(f)
        for name, repo in cast(dict[str, Any], repos_data["repositories"]).items():
            if "/" in name or name in ["google_apis", "bazel_remote_apis"]:
                continue
            if isinstance(repo["repository"], dict) and repo["repository"]["type"] in ["archive","zip"]:
                imports.append(name)

    # verify system deps
    unknown_deps = system_deps.difference(imports)
    if unknown_deps:
        print(f"ERROR: unknown system deps: {', '.join(unknown_deps)}")
        print(f"       known system deps: {', '.join(imports)}")
        exit(1)
    if ('grpc' in system_deps) != ('protobuf' in system_deps):
        print("ERROR: 'grpc' and 'protobuf' must both be either system deps or bundled.")
        exit(1)

    def file_import(name : str) -> dict[str, Any]:
        return dict(source="file", path=".", repos=[dict(repo=name)])

    def git_import(name : str) -> dict[str, Any]:
        return dict(source="git",
                    branch=f"{name}/system",
                    url="https://github.com/jst-build/imports-cc",
                    repos=[dict(repo=name)])

    # generate system imports and remappings
    system_imports : list[dict[str,Any]] = [file_import("toolchain")]
    jst_remapping : dict[str, str] = {}
    for name in system_deps.intersection(imports):
        if os.path.exists(os.path.join(g_SRCDIR, f"etc/imports/{name}.TARGETS")):
            # imports from etc/repos.json (system dependencies)
            system_imports.append(file_import(name))
        else:
            # imports from github.com/jst-build/imports-cc (system branch)
            system_imports.append(git_import(name))
        jst_remapping[name] = name
        if name == "grpc":
            # grpc requires remapping the grpc_toolchain to the system toolchain
            jst_remapping["grpc_toolchain"] = "toolchain"

    # write bootstrap.in.json and generate lock file
    bootstrap_data = dict(
        main="jst",
        imports=system_imports + [
            # import from etc/bundled.json: bundled jst with remappings
            dict(
                source="file",
                path=".",
                config="etc/bundled.json",
                repos=[dict(repo="jst", map=jst_remapping)],
            ),
        ]
    )
    in_file = os.path.join(cast(str, g_WRKDIR), 'bootstrap.in.json')
    lock_file = os.path.join(cast(str, g_WRKDIR), 'bootstrap.json')
    with open(in_file, 'w') as f:
        json.dump(bootstrap_data, f, indent=2)
    run(['./bin/jst-lock.py', '-C', in_file, '-o', lock_file], cwd=g_SRCDIR)

    return lock_file


def main(args: List[str]):
    global g_SRCDIR
    global g_WRKDIR
    global g_DISTDIR
    if len(args) > 1:
        g_SRCDIR = os.path.abspath(args[1])
    if len(args) > 2:
        g_WRKDIR = os.path.abspath(args[2])
    if len(args) > 3:
        g_DISTDIR = [os.path.abspath(p) for p in args[3:]]

    if not g_WRKDIR:
        g_WRKDIR = tempfile.mkdtemp()
    if not g_DISTDIR:
        g_DISTDIR = [os.path.join(Path.home(), ".distfiles")]

    repos_config = REPOS
    is_system_build = "BUNDLED" not in os.environ
    if not is_system_build:
        system_deps = json.loads(os.environ.get("SYSTEM_DEPS", "[]"))
        repos_config = write_bootstrap_config(set(system_deps))
    bootstrap(repos_config, is_system_build)


if __name__ == "__main__":
    # Parse options, set g_DISTDIR
    main(sys.argv)
