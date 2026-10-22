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

mkdir -p work/runtime work/errors && cd work
touch ROOT

cat <<'EOF' > RULES
local stage(fieldname, transition={}) = jst.union(
  [jst.dep_artifacts(dep, transition=transition) for dep in jst.field(fieldname)],
  disjoint=true,
  msg=['Conflicting artifacts in field', fieldname],
);
local transition = {SUFFIX: '?'};
{
  greet: {
    doc: ['Concatenate the inputs and append a greeting.'],
    string_fields: ['name'],
    target_fields: ['deps'],
    config_vars: ['SUFFIX'],
    config_transitions: {deps: [transition]},
    tainted: ['test'],
    expression:
      local name = jst.join(jst.field('name'));
      local out = name + '.txt';
      local action = jst.action(
        inputs=stage('deps', transition),
        cmd=[
          'sh', '-c',
          'cat *.in > ' + out + '; echo '
          + jst.join_cmd([name + jst.env('SUFFIX', default='')]) + ' >> ' + out,
        ],
        outs=[out],
        may_fail=['test'],
        fail_message='greeting ' + name + ' failed',
      );
      jst.result(artifacts=action, provides={greeting: [name]}),
  },
  collect: {
    target_fields: ['deps'],
    tainted: ['test'],
    expression:
      local greetings = jst.flatten([
        jst.dep_provides(dep, 'greeting', default=[])
        for dep in jst.field('deps')
      ]);
      jst.result(
        artifacts={'all.txt': jst.make_blob(jst.join(greetings, sep='\n'))},
        runfiles={tree: jst.make_tree(stage('deps'))},
      ),
  },
}
EOF

# requires runtime evaluation (lookup in runtime value)
cat <<'EOF' > runtime/RULES
{
  runtime_only: {
    config_vars: ['M'],
    expression:
      local data = jst.env('M', default={k: 'v'}).k;
      jst.result(artifacts={'out.txt': jst.make_blob(data)}),
  },
}
EOF
cat <<'EOF' > runtime/TARGETS
{runtime_only: {type: 'runtime_only'}}
EOF

cat <<'EOF' > errors/RULES
{
  broken: {
    expression: jst.result(artifacts={'out.txt': jst.make_blob(jst.join(42))}),
  },
  broken_static: {
    target_fields: if jst.env('SUFFIX') then ['deps'] else [],
    expression: jst.result(),
  },
}
EOF
cat <<'EOF' > errors/TARGETS
{
  broken: {type: 'broken'},
  broken_static: {type: 'broken_static'},
}
EOF

cat <<'EOF' > TARGETS
{
  a: {type: 'file_gen', name: 'a.in', data: 'A\n'},
  hello: {type: 'greet', name: ['hello'], deps: ['a'], tainted: ['test']},
  world: {type: 'greet', name: ['world'], deps: [], tainted: ['test']},
  all: {type: 'collect', deps: ['hello', 'world', 'a'], tainted: ['test']},
}
EOF

echo "build targets of jstlang rules"
"${JUST}" install --local-build-root "${LBR}" -o "${OUT}/hello" hello 2>&1
printf 'A\nhello\n' | cmp - "${OUT}/hello/hello.txt"
"${JUST}" install --local-build-root "${LBR}" -o "${OUT}/hello2" \
  -D '{"SUFFIX": "!"}' hello 2>&1
printf 'A\nhello!\n' | cmp - "${OUT}/hello2/hello.txt"
"${JUST}" install --local-build-root "${LBR}" -o "${OUT}/all" all 2>&1
printf 'hello\nworld' | cmp - "${OUT}/all/all.txt"
printf 'A\nhello\n' | cmp - "${OUT}/all/tree/hello.txt"
printf 'A\n' | cmp - "${OUT}/all/tree/a.in"

echo "values only known at runtime are supported"
"${JUST}" analyse --local-build-root "${LBR}" //runtime:runtime_only 2>&1

echo "error locations refer to RULES file"
"${JUST}" analyse --local-build-root "${LBR}" -f "${LOG}" \
  //errors:broken 2>&1 && exit 1 || :
grep "errors/RULES:3:" "${LOG}"

echo "plain rule fields must not depend on the configuration"
"${JUST}" analyse --local-build-root "${LBR}" -f "${LOG}" \
  //errors:broken_static 2>&1 && exit 1 || :
grep "Runtime variable 'SUFFIX' cannot be read" "${LOG}"

echo "OK"
