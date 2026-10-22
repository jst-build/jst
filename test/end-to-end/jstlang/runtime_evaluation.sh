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

set -eu

readonly JUST="${PWD}/bin/tool-under-test"
readonly LBR="${TMPDIR}/local-build-root"
readonly OUT="${TMPDIR}/out"
readonly LOG="${TMPDIR}/log.txt"

mkdir -p work/lib work/runtime work/errors/missing work/nested work/cycle
cd work
touch ROOT

cat <<'EOF' > lib/common.jst
local greet(name) = 'Hello ' + name;
{
  // create target definition by function
  echo_target(name, out): {
    type: 'generic',
    arguments_config: ['SUFFIX'],
    outs: [out],
    cmds: [
      jst.join_cmd(['echo', greet(name) + jst.env('SUFFIX', default='')])
      + ' > ' + out,
    ],
  },
}
EOF

cat <<'EOF' > TARGETS
local common = import 'lib/common.jst';
// unused config-dependent top-level local
local suffix = jst.env('SUFFIX', default='');
local names = ['world', 'moon'];
{
  world: common.echo_target('world', 'world.txt'),
  moon: common.echo_target(names[1], 'moon.txt'),
  shouting: {
    type: 'configure',
    target: 'world',
    config: {SUFFIX: '!'},
  },
  all: {
    type: 'install',
    arguments_config: ['SUFFIX'],
    files: {[name + '.txt']: name for name in names},
  },
}
EOF

# requires runtime evaluation (lookup in runtime value)
cat <<'EOF' > runtime/TARGETS
{
  runtime_only: {
    type: 'generic',
    arguments_config: ['M'],
    outs: ['out.txt'],
    cmds: ['echo ' + jst.env('M', default={k: 'v'}).k + ' > out.txt'],
  },
}
EOF

cat <<'EOF' > errors/TARGETS
{
  bad_field: {
    type: 'generic',
    outs: ['out.txt'],
    cmds: [jst.join(42)],
  },
  bad_static_field: {
    type: 'generic',
    arguments_config: if jst.env('SUFFIX') then ['SUFFIX'] else [],
    outs: ['out.txt'],
    cmds: ['touch out.txt'],
  },
}
EOF

# nested imports (relative to the importing file and to the root)
cat <<'EOF' > lib/name.jst
{name: (import 'suffix.jst').suffix('nested')}
EOF
cat <<'EOF' > lib/suffix.jst
{suffix(s): s + '!'}
EOF
cat <<'EOF' > nested/TARGETS
local common = import '/lib/common.jst';
local name = import '../lib/name.jst';
{nested: common.echo_target(name.name, 'nested.txt')}
EOF

cat <<'EOF' > cycle/a.jst
{a: (import 'b.jst').b}
EOF
cat <<'EOF' > cycle/b.jst
{b: (import 'a.jst').a}
EOF
cat <<'EOF' > cycle/TARGETS
local a = import 'a.jst';
{cycle: {type: 'generic', outs: [a.a], cmds: ['true']}}
EOF

cat <<'EOF' > errors/missing/TARGETS
local m = import 'missing.jst';
{missing: {type: 'generic', outs: [m.out], cmds: ['true']}}
EOF

echo "build function-created target"
"${JUST}" install --local-build-root "${LBR}" -o "${OUT}/world" world 2>&1
grep -x 'Hello world' "${OUT}/world/world.txt"

echo "build with configuration"
"${JUST}" install --local-build-root "${LBR}" -o "${OUT}/shouting" shouting 2>&1
grep -x 'Hello world!' "${OUT}/shouting/world.txt"
"${JUST}" install --local-build-root "${LBR}" -o "${OUT}/suffix" \
  -D '{"SUFFIX": "?"}' moon 2>&1
grep -x 'Hello moon?' "${OUT}/suffix/moon.txt"

echo "install with computed file names"
"${JUST}" install --local-build-root "${LBR}" -o "${OUT}/all" \
  -D '{"SUFFIX": "_x"}' all 2>&1
grep -x 'Hello world_x' "${OUT}/all/world.txt"
grep -x 'Hello moon_x' "${OUT}/all/moon.txt"

echo "nested imports"
"${JUST}" install --local-build-root "${LBR}" -o "${OUT}/nested" \
  //nested:nested 2>&1
grep -x 'Hello nested!' "${OUT}/nested/nested.txt"

echo "import cycles are reported"
"${JUST}" analyse --local-build-root "${LBR}" -f "${LOG}" \
  //cycle:cycle 2>&1 && exit 1 || :
grep 'Cycle detected in jstlang imports' "${LOG}"

echo "missing imports are reported"
"${JUST}" analyse --local-build-root "${LBR}" -f "${LOG}" \
  //errors/missing:missing 2>&1 && exit 1 || :
grep 'missing.jst' "${LOG}"

echo "values only known at runtime are supported"
"${JUST}" analyse --local-build-root "${LBR}" //runtime:runtime_only 2>&1

echo "error locations refer to TARGETS file"
"${JUST}" analyse --local-build-root "${LBR}" -f "${LOG}" \
  //errors:bad_field 2>&1 && exit 1 || :
grep 'errors/TARGETS:5:' "${LOG}"

echo "fields read as plain values must not depend on the configuration"
"${JUST}" analyse --local-build-root "${LBR}" -f "${LOG}" \
  //errors:bad_static_field 2>&1 && exit 1 || :
grep "Runtime variable 'SUFFIX' cannot be read" "${LOG}"

echo "OK"
