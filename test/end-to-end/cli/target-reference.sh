#!/bin/sh
# Copyright 2026 The jst-build authors.
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

set -e

ROOT=$(pwd)
TOOL=$(realpath ./bin/tool-under-test)
mkdir -p .root
BUILDROOT=$(realpath .root)

# A target of every module, so that the default target, i.e. the
# lexicographically first one, is told apart from a named one.
target_file() {
  cat <<'EOF'
{ "AAAdefault":
  {"type": "generic", "outs": ["o"], "cmds": ["touch o"]}
, "baz":
  {"type": "generic", "outs": ["o"], "cmds": ["touch o"]}
, "bar":
  {"type": "generic", "outs": ["o"], "cmds": ["touch o"]}
}
EOF
}

mkdir -p src/foo/bar src/sub
touch src/ROOT
target_file > src/TARGETS
target_file > src/foo/bar/TARGETS
target_file > src/sub/TARGETS
SRCDIR=$(realpath src)

mkdir -p oth/mod
touch oth/ROOT
target_file > oth/TARGETS
target_file > oth/mod/TARGETS
OTHDIR=$(realpath oth)

cat > repos.json <<EOF
{ "main": ""
, "repositories":
  { "": {"workspace_root": ["file", "${SRCDIR}"]}
  , "other": {"workspace_root": ["file", "${OTHDIR}"]}
  }
}
EOF
REPOS=$(realpath repos.json)

# Analyse the given reference and compare the target the tool reports with
# the expected one. An empty reference means none is given at all.
check() {
  expected="$1"
  shift
  echo "--- analyse $* (in $(pwd | sed "s|${ROOT}||"))"
  "${TOOL}" analyse --local-build-root "${BUILDROOT}" -B "${REPOS}" "$@" \
    > "${ROOT}/out.log" 2>&1 || {
      echo "FAILED to analyse $*"; cat "${ROOT}/out.log"; exit 1; }
  got=$(grep -o "Requested target '[^']*'" "${ROOT}/out.log" | head -1 \
        | sed "s|Requested target '||; s|'$||")
  if [ "${got}" != "${expected}" ]; then
    echo "FAILED: expected ${expected}, got ${got}"
    cat "${ROOT}/out.log"
    exit 1
  fi
  echo "OK: ${got}"
}

# expect the analysis to fail, with the given text in the report
check_fail() {
  expected="$1"
  shift
  echo "--- analyse $* (expecting failure)"
  if "${TOOL}" analyse --local-build-root "${BUILDROOT}" -B "${REPOS}" "$@" \
       > "${ROOT}/out.log" 2>&1; then
    echo "FAILED: analysing $* should not have succeeded"
    cat "${ROOT}/out.log"
    exit 1
  fi
  grep -q "${expected}" "${ROOT}/out.log" || {
    echo "FAILED: expected '${expected}' in the report"
    cat "${ROOT}/out.log"; exit 1; }
  echo "OK"
}

cd "${SRCDIR}"

echo "=== from the top-level module ==="
check '""//:baz'         baz
check '""//:baz'         :baz
check '""//:AAAdefault'  :
check '""//:AAAdefault'
check '""//sub:baz'      ./sub:baz
check '""//sub:AAAdefault' ./sub:
check '""//foo/bar:baz'  //foo/bar:baz
check '""//foo/bar:AAAdefault' //foo/bar:
check '""//foo/bar:bar'  //foo/bar
check '""//:AAAdefault'  //:
check 'other//mod:baz'   other//mod:baz
check 'other//mod:AAAdefault' other//mod:
check 'other//:AAAdefault' other//:

echo "=== from a subdirectory, the current module is used ==="
cd "${SRCDIR}/foo/bar"
check '""//foo/bar:baz'        baz
check '""//foo/bar:AAAdefault' :
check '""//foo/bar:AAAdefault'
check '""//:AAAdefault'        //:
check 'other//mod:baz'         other//mod:baz

echo "=== names needing a fully qualified reference ==="
cd "${SRCDIR}"
check '""//:baz'  ':"baz"'
check '""//:baz'  '//:"baz"'

echo "=== errors ==="
check_fail 'Empty module without target' '//'
check_fail 'Missing module name' 'other//'
check_fail 'outside of workspace' './../..:baz'

echo OK
