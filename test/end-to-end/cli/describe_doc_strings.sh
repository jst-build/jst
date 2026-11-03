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

ROOT="$(pwd)"
JUST="${ROOT}/bin/tool-under-test"
LBR="${TEST_TMPDIR}/build-root"
OUT="${TEST_TMPDIR}/output"

mkdir -p work "${OUT}"
cd work
touch ROOT

# documentation given as single (multi-line) strings and as lists of strings
cat > RULES <<'EOF'
{
  greet: {
    doc: |||
      Write a GREETING to a file.

      The file is named after the RECIPIENT.
    |||,
    string_fields: ['name', 'prefix'],
    config_vars: ['SUFFIX'],
    field_doc: {
      name: 'The NAME of the recipient',
      prefix: ['The PREFIX of the', 'greeting'],
    },
    config_doc: {SUFFIX: 'The SUFFIX of the greeting'},
    artifacts_doc: 'The GREETING file',
    runfiles_doc: ['NOTHING'],
    expression:
      local name = jst.join(jst.field('name'));
      local greeting = jst.join(jst.field('prefix')) + name + $SUFFIX;
      jst.result(artifacts={[name + '.txt']: jst.make_blob(greeting)}),
  },
}
EOF

cat > TARGETS <<'EOF'
{
  payload: {type: 'greet', name: ['world'], prefix: ['Hello ']},
  'exported-target': {
    type: 'export',
    doc: 'Here we export the canonical EXPORTED greeting',
    target: 'payload',
    flexible_config: ['SUFFIX'],
    config_doc: {SUFFIX: 'The SUFFIX of the EXPORTED greeting'},
  },
  '': {
    type: 'configure',
    doc: 'Configure the canonical greeting to set a DEFAULT suffix',
    target: 'exported-target',
    config: {SUFFIX: '!'},
  },
}
EOF

# Sanity check: the target description actually works
"${JUST}" install --local-build-root "${LBR}" -o "${OUT}/install" 2>&1
printf 'Hello world!' | cmp - "${OUT}/install/world.txt"

echo "Description of a rule"
"${JUST}" describe --local-build-root "${LBR}" payload > "${OUT}/rule.doc"
cat "${OUT}/rule.doc"
for word in GREETING RECIPIENT NAME PREFIX SUFFIX NOTHING; do
  grep "${word}" "${OUT}/rule.doc"
done

echo "JSON description of a rule has strings"
"${JUST}" describe --local-build-root "${LBR}" --json payload \
  > "${OUT}/rule.json"
cat "${OUT}/rule.json"
for path in .doc '.field_doc.name' '.field_doc.prefix' '.config_doc.SUFFIX' \
            .artifacts_doc .runfiles_doc; do
  [ "$(jq -r "${path} | type" "${OUT}/rule.json")" = string ]
done
[ "$(jq -r '.field_doc.name' "${OUT}/rule.json")" = \
  "The NAME of the recipient" ]
# lists of strings are joined, each entry terminated by a newline
[ "$(jq '.field_doc.prefix' "${OUT}/rule.json")" = \
  '"The PREFIX of the\ngreeting\n"' ]

echo "Description of an export target"
"${JUST}" describe --local-build-root "${LBR}" exported-target \
  > "${OUT}/export.doc"
cat "${OUT}/export.doc"
grep "export the canonical EXPORTED greeting" "${OUT}/export.doc"
grep "SUFFIX of the EXPORTED greeting" "${OUT}/export.doc"

echo "Description of a configure target"
"${JUST}" describe --local-build-root "${LBR}" > "${OUT}/configure.doc"
cat "${OUT}/configure.doc"
grep "DEFAULT suffix" "${OUT}/configure.doc"

echo OK
