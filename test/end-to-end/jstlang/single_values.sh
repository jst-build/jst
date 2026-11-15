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

mkdir -p work/sub work/json && cd work
touch ROOT

cat <<'EOF' > RULES
{
  // Reports its string fields and stages the artifacts of its targets.
  show: {
    string_fields: ['name', 'names'],
    config_string_fields: ['flavour'],
    target_fields: ['deps', 'more'],
    expression: jst.result(artifacts=jst.union([
      {
        [jst.join(jst.field('name')) + '.json']: jst.make_blob(jst.json_encode({
          name: jst.field('name'),
          names: jst.field('names'),
          flavour: jst.field('flavour'),
        })),
      },
    ] + [
      jst.dep_artifacts(dep) for dep in jst.field('deps') + jst.field('more')
    ])),
  },
  // Abstract nodes require lists, as they are written by rules.
  provide: {
    expression: jst.result(provides={nodes: [
      jst.abstract_node('node', string_fields={name: 'single'}),
    ]}),
  },
  collect: {
    target_fields: ['srcs'],
    anonymous: {
      nodes: {target: 'srcs', provider: 'nodes', rule_map: {node: 'show'}},
    },
    expression: jst.result(),
  },
}
EOF

cat <<'EOF' > sub/TARGETS
{c: {type: 'file_gen', name: 'c.txt', data: 'C'}}
EOF

# Target files in plain JSON require lists.
cat <<'EOF' > json/TARGETS
{ "lists": {"type": ["", "show"], "name": ["lists"]}
, "single": {"type": ["", "show"], "name": "single"}
, "single_builtin":
  {"type": "generic", "cmds": "touch out.txt", "outs": ["out.txt"]}
}
EOF

cat <<'EOF' > TARGETS
{
  a: {type: 'file_gen', name: 'a.txt', data: 'A', deps: 'b'},
  b: {type: 'file_gen', name: 'b.txt', data: 'B'},
  single: {
    type: 'show',
    name: 'single',
    names: ['x', 'y'],
    flavour: 'sweet',
    deps: 'a',
    more: '//sub:c',
  },
  generic: {
    type: 'generic',
    cmds: 'cat a.txt > out.txt && cp a.txt d/',
    outs: 'out.txt',
    out_dirs: 'd',
    deps: 'a',
  },
  shell: {
    type: 'generic',
    'sh -c': 'myshell',
    cmds: 'touch out.txt',
    outs: 'out.txt',
  },
  configured: {
    type: 'show',
    arguments_config: ['NAME'],
    name: $NAME,
  },
  // Reserved fields require lists.
  single_arguments_config: {
    type: 'show',
    arguments_config: 'NAME',
    name: 'x',
  },
  single_tainted: {type: 'show', name: 'x', tainted: 'test'},
  single_flexible_config: {
    type: 'export',
    target: 'configured',
    flexible_config: 'NAME',
  },
  single_arguments_config_builtin: {
    type: 'file_gen',
    arguments_config: 'NAME',
    name: 'x',
    data: 'X',
  },
  single_tainted_builtin: {
    type: 'file_gen',
    tainted: 'test',
    name: 'x',
    data: 'X',
  },
  installed: {type: 'install', deps: 'a'},
  tree: {type: 'tree', name: 't', deps: 'a'},
  wrong_type: {type: 'show', name: {single: true}},
  provider: {type: 'provide'},
  abstract: {type: 'collect', srcs: ['provider']},
}
EOF

install() {
  target="$1"
  shift
  "${JUST}" install --local-build-root "${LBR}" -o "${OUT}/${target}" \
    "${target}" "$@" 2>&1
}

echo "string and target fields of rules accept single values"
install single
[ "$(cat "${OUT}/single/single.json")" \
  = '{"flavour":["sweet"],"name":["single"],"names":["x","y"]}' ]
printf 'A' | cmp - "${OUT}/single/a.txt"
printf 'C' | cmp - "${OUT}/single/c.txt"

echo "fields of built-in rules accept single values"
install generic
printf 'A' | cmp - "${OUT}/generic/out.txt"
printf 'A' | cmp - "${OUT}/generic/d/a.txt"
install installed
printf 'A' | cmp - "${OUT}/installed/a.txt"
install tree
printf 'A' | cmp - "${OUT}/tree/t/a.txt"

echo "single sh -c is the prefix of the command line"
"${JUST}" analyse --local-build-root "${LBR}" --dump-graph "${OUT}/graph.json" \
  shell 2>&1
[ "$(jq -c '.actions[].command' "${OUT}/graph.json")" \
  = '["myshell","touch out.txt\n"]' ]

echo "arguments_config, tainted, and flexible_config do not accept single values"
for target in single_arguments_config single_arguments_config_builtin \
              single_flexible_config; do
  "${JUST}" analyse --local-build-root "${LBR}" -f "${LOG}" \
    -D '{"NAME": "x"}' "${target}" 2>&1 && exit 1 || :
  grep "is not a list" "${LOG}"
done
for target in single_tainted single_tainted_builtin; do
  "${JUST}" analyse --local-build-root "${LBR}" -f "${LOG}" "${target}" 2>&1 \
    && exit 1 || :
  grep "tainted should evaluate to a list of strings" "${LOG}"
done

echo "other values than strings and lists are rejected"
"${JUST}" analyse --local-build-root "${LBR}" -f "${LOG}" wrong_type 2>&1 \
  && exit 1 || :
grep 'String field name should be a list of strings' "${LOG}"

echo "target files in plain JSON do not accept single values"
install //json:lists
[ "$(cat "${OUT}/json:lists/lists.json")" \
  = '{"flavour":[],"name":["lists"],"names":[]}' ]
"${JUST}" analyse --local-build-root "${LBR}" -f "${LOG}" //json:single 2>&1 \
  && exit 1 || :
grep 'String field name should be a list of strings' "${LOG}"
"${JUST}" analyse --local-build-root "${LBR}" -f "${LOG}" \
  //json:single_builtin 2>&1 && exit 1 || :
grep 'cmds has to evaluate to a list of strings' "${LOG}"

echo "fields of abstract nodes still have to be lists"
"${JUST}" analyse --local-build-root "${LBR}" -f "${LOG}" abstract 2>&1 \
  && exit 1 || :
grep "is not a list" "${LOG}"

echo "OK"
