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
readonly LOG="${TMPDIR}/log.txt"

mkdir -p work && cd work
touch ROOT

cat <<'EOF' > RULES
{
  // Provides a generated file 'out.txt' with the given content.
  generate: {
    string_fields: ['content'],
    expression: jst.result(provides={stage: jst.action(
      cmd=['sh', '-c', 'echo ' + jst.join(jst.field('content')) + ' > out.txt'],
      outs=['out.txt'],
    )}),
  },
  // Stages the provided files of all dependencies, which must not conflict.
  collect: {
    target_fields: ['deps'],
    expression: jst.result(artifacts=jst.union(
      [jst.dep_provides(dep, 'stage', default={}) for dep in jst.field('deps')],
      disjoint=true,
      msg='Conflicting files',
    )),
  },
}
EOF

cat <<'EOF' > TARGETS
{
  hello: {type: 'generate', content: ['hello']},
  world: {type: 'generate', content: ['world']},
  same: {type: 'generate', content: ['hello']},
  conflict: {type: 'collect', deps: ['hello', 'world']},
  'no conflict': {type: 'collect', deps: ['hello', 'same']},
}
EOF

echo "equal values do not conflict"
"${JUST}" analyse --local-build-root "${LBR}" ':no conflict' 2>&1

echo "conflicting values are reported, with the targets providing them"
"${JUST}" analyse --local-build-root "${LBR}" -f "${LOG}" conflict 2>&1 \
  && exit 1 || :
cat "${LOG}"
grep 'Conflicting files' "${LOG}"
grep 'conflicting values for key "out.txt"' "${LOG}"
[ "$(grep -c '"type":"ACTION"' "${LOG}")" -ge 2 ]
[ "$(grep -c 'occurs in direct dependencies' "${LOG}")" -eq 2 ]
grep -F "'@\"\"//:hello'" "${LOG}"
grep -F "'@\"\"//:world'" "${LOG}"

echo "OK"
