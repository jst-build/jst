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

mkdir -p work/errors && cd work
touch ROOT

cat <<'EOF' > RULES
{
  // Report the values of all kinds of fields.
  show: {
    bool_fields: ['flag'],
    string_fields: ['name'],
    config_bool_fields: ['config_flag'],
    config_string_fields: ['config_name'],
    expression: jst.result(artifacts={
      [jst.join(jst.field('name')) + '.json']: jst.make_blob(jst.json_encode({
        flag: jst.field('flag'),
        config_flag: jst.field('config_flag'),
        config_name: jst.field('config_name'),
      })),
    }),
  },
  // Config bool fields can be used in config transitions.
  pick: {
    config_bool_fields: ['upper'],
    target_fields: ['deps'],
    config_transitions: {deps: [{UPPER: jst.field('upper')}]},
    expression: jst.result(artifacts=jst.union([
      jst.dep_artifacts(dep, transition={UPPER: jst.field('upper')})
      for dep in jst.field('deps')
    ])),
  },
  // Bool fields of abstract nodes, normalized to their truth values.
  provide: {
    expression: jst.result(provides={nodes: [
      jst.abstract_node(
        'node',
        bool_fields={flag: ['yes'], config_flag: []},
        string_fields={name: ['anonymous'], config_name: ['x']},
      ),
    ]}),
  },
  collect: {
    target_fields: ['srcs'],
    anonymous: {
      nodes: {target: 'srcs', provider: 'nodes', rule_map: {node: 'show'}},
    },
    expression: jst.result(artifacts=jst.union([
      jst.dep_artifacts(node) for node in jst.field('nodes')
    ])),
  },
  // The deprecated name of config string fields.
  legacy: {
    config_fields: ['config_name'],
    expression: jst.result(artifacts={
      'legacy.json': jst.make_blob(jst.json_encode(jst.field('config_name'))),
    }),
  },
}
EOF

cat <<'EOF' > TARGETS
{
  given: {
    type: 'show',
    name: ['given'],
    flag: true,
    config_flag: ['non-empty list'],
    config_name: ['a', 'b'],
  },
  absent: {type: 'show', name: ['absent']},
  falsy: {type: 'show', name: ['falsy'], flag: [], config_flag: ''},
  configured: {
    type: 'show',
    arguments_config: ['FLAG'],
    name: ['configured'],
    flag: $FLAG,
  },
  letter: {
    type: 'file_gen',
    arguments_config: ['UPPER'],
    name: 'letter.txt',
    data: if $UPPER then 'A' else 'a',
  },
  upper: {type: 'pick', upper: true, deps: ['letter']},
  lower: {type: 'pick', deps: ['letter']},
  provider: {type: 'provide'},
  anonymous: {type: 'collect', srcs: ['provider']},
  legacy: {type: 'legacy', config_name: ['old']},
}
EOF

cat <<'EOF' > errors/RULES
{
  conflict: {
    bool_fields: ['x'],
    string_fields: ['x'],
    expression: jst.result(),
  },
  both_config_string_fields: {
    config_string_fields: ['x'],
    config_fields: ['y'],
    expression: jst.result(),
  },
  provide: {
    expression: jst.result(provides={nodes: [
      jst.abstract_node('node', string_fields={flag: ['yes']}),
    ]}),
  },
  show: {bool_fields: ['flag'], expression: jst.result()},
  collect: {
    target_fields: ['srcs'],
    anonymous: {
      nodes: {target: 'srcs', provider: 'nodes', rule_map: {node: 'show'}},
    },
    expression: jst.result(),
  },
}
EOF
cat <<'EOF' > errors/TARGETS
{
  conflict: {type: 'conflict'},
  both_config_string_fields: {type: 'both_config_string_fields'},
  provider: {type: 'provide'},
  wrong_map: {type: 'collect', srcs: ['provider']},
}
EOF

install() {
  "${JUST}" install --local-build-root "${LBR}" -o "${OUT}/$1" "$@" 2>&1
}

echo "bool fields are truth values, given or absent"
install given
[ "$(cat "${OUT}/given/given.json")" \
  = '{"config_flag":true,"config_name":["a","b"],"flag":true}' ]
install absent
[ "$(cat "${OUT}/absent/absent.json")" \
  = '{"config_flag":false,"config_name":[],"flag":false}' ]
install falsy
[ "$(cat "${OUT}/falsy/falsy.json")" \
  = '{"config_flag":false,"config_name":[],"flag":false}' ]

echo "bool fields can depend on the configuration"
install configured -D '{"FLAG": {"key": "value"}}'
grep '"flag":true' "${OUT}/configured/configured.json"
install configured -D '{"FLAG": 0}'
grep '"flag":false' "${OUT}/configured/configured.json"

echo "config bool fields can be used in config transitions"
install upper
printf 'A' | cmp - "${OUT}/upper/letter.txt"
install lower
printf 'a' | cmp - "${OUT}/lower/letter.txt"

echo "abstract nodes have bool fields"
install anonymous
[ "$(cat "${OUT}/anonymous/anonymous.json")" \
  = '{"config_flag":false,"config_name":["x"],"flag":true}' ]

echo "config_fields is still accepted for config string fields"
install legacy
[ "$(cat "${OUT}/legacy/legacy.json")" = '["old"]' ]

echo "a field can only have one kind"
"${JUST}" analyse --local-build-root "${LBR}" -f "${LOG}" \
  //errors:conflict 2>&1 && exit 1 || :
grep "only one type" "${LOG}"

echo "config_string_fields and config_fields cannot be given both"
"${JUST}" analyse --local-build-root "${LBR}" -f "${LOG}" \
  //errors:both_config_string_fields 2>&1 && exit 1 || :
grep "config_fields (deprecated)" "${LOG}"

echo "bool fields of abstract nodes must be given as bool_fields"
"${JUST}" analyse --local-build-root "${LBR}" -f "${LOG}" \
  //errors:wrong_map 2>&1 && exit 1 || :
grep "Expected bool field 'flag' in bool_fields" "${LOG}"

echo "describe reports the kinds of fields"
"${JUST}" describe --local-build-root "${LBR}" given > "${OUT}/describe.txt"
cat "${OUT}/describe.txt"
grep "^BOOL FIELDS" "${OUT}/describe.txt"
grep "^CONFIG BOOL FIELDS" "${OUT}/describe.txt"
grep "^CONFIG STRING FIELDS" "${OUT}/describe.txt"
"${JUST}" describe --local-build-root "${LBR}" --json given \
  > "${OUT}/describe.json"
[ "$(jq -c '[.bool_fields, .string_fields, .config_bool_fields,
             .config_string_fields]' "${OUT}/describe.json")" \
  = '[["flag"],["name"],["config_flag"],["config_name"]]' ]
"${JUST}" describe --local-build-root "${LBR}" --json legacy \
  > "${OUT}/legacy.json"
[ "$(jq -c '.config_string_fields' "${OUT}/legacy.json")" = '["config_name"]' ]

echo "OK"
