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
readonly WORK="${PWD}/work"

mkdir -p work/main/errors work/lib-targets/sub work/lib-rules
cd work

# repository "main" binds the name "lib" to the repository "library", whose
# target root and rule root differ
cat <<EOF > repo-config.json
{ "main": "main"
, "repositories":
  { "main":
    { "workspace_root": ["file", "${WORK}/main"]
    , "bindings": {"lib": "library"}
    }
  , "library":
    { "workspace_root": ["file", "${WORK}/lib-targets"]
    , "rule_root": ["file", "${WORK}/lib-rules"]
    }
  }
}
EOF

# imported from the target root, with a relative import of the library
cat <<'EOF' > lib-targets/common.jst
local util = import 'sub/util.jst';
{greeting: util.greet('targets')}
EOF
cat <<'EOF' > lib-targets/sub/util.jst
{greet(name): 'Hello ' + name}
EOF

# imported from the rule root (same path, different content)
cat <<'EOF' > lib-rules/common.jst
{greeting: 'Hello rules'}
EOF

cat <<'EOF' > main/RULES
local lib = import '@lib//common.jst';
{
  greet: {
    expression:
      local out = {'rules.txt': jst.make_blob(lib.greeting)};
      jst.result(artifacts=out, runfiles=out),
  },
}
EOF

cat <<'EOF' > main/TARGETS
local lib = import '@lib//common.jst';
{
  from_targets: {type: 'file_gen', name: 'targets.txt', data: lib.greeting},
  from_rules: {type: 'greet'},
  all: {type: 'install', deps: ['from_targets', 'from_rules']},
}
EOF

cat <<'EOF' > main/errors/TARGETS
local other = import '@other//common.jst';
{unbound: {type: 'file_gen', name: 'out.txt', data: other.greeting}}
EOF

echo "files are imported from the root of the same kind"
"${JUST}" install --local-build-root "${LBR}" -B repo-config.json \
  -o "${OUT}" all 2>&1
printf 'Hello targets' | cmp - "${OUT}/targets.txt"
printf 'Hello rules' | cmp - "${OUT}/rules.txt"

echo "names that are not bound are reported"
"${JUST}" analyse --local-build-root "${LBR}" -B repo-config.json \
  -f "${LOG}" //errors:unbound 2>&1 && exit 1 || :
grep 'Repository other is not bound by repository main' "${LOG}"

echo "OK"
