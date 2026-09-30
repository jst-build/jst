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

# A target reference naming a repository selects it as the main repository,
# the same way --main does, and takes precedence over that option. What the
# launcher decided is visible in the multi-repository configuration it
# generates for the backend, so inspect the "main" of that configuration
# instead of running a build.

set -eu

readonly JUST_MR="${PWD}/bin/mr-tool-under-test"
readonly LBR="${TEST_TMPDIR}/local-build-root"
readonly WORK="${TEST_TMPDIR}/work"
readonly TOOLS="${TEST_TMPDIR}/tools"
mkdir -p "${WORK}" "${TOOLS}"

# A stand-in for the backend, reporting the "main" of the configuration it is
# given after -B. The reference itself is of no interest here; it is the
# launcher's choice of the main repository that is tested.
readonly FAKE_BACKEND="${TOOLS}/fake-backend.py"
cat > "${FAKE_BACKEND}" <<'EOF'
#!/usr/bin/env python3
import json
import sys

args = sys.argv[1:]
config = None
for i, arg in enumerate(args):
    if arg == "-B":
        config = args[i + 1]
        break
if config is None:
    print("MAIN=<no -B given>")
    sys.exit(1)
with open(config) as f:
    print("MAIN=%s" % (json.load(f).get("main"), ))
EOF
chmod 755 "${FAKE_BACKEND}"

cd "${WORK}"

mkdir -p main_repo other_repo
touch main_repo/ROOT other_repo/ROOT
for d in main_repo other_repo; do
  cat > "${d}/TARGETS" <<'EOF'
{"t": {"type": "generic", "outs": ["o"], "cmds": ["touch o"]}}
EOF
done

cat > repos.json <<EOF
{ "main": "main_repo"
, "repositories":
  { "main_repo": {"repository": {"type": "file", "path": "main_repo"}}
  , "other_repo": {"repository": {"type": "file", "path": "other_repo"}}
  }
}
EOF
cat repos.json
echo

# Run the launcher and compare the main repository it chose with the expected
# one. Everything after the expectation is passed on verbatim.
check() {
  expected="$1"
  shift
  echo "--- analyse $*"
  "${JUST_MR}" --norc --local-build-root "${LBR}" -C repos.json \
    --backend "${FAKE_BACKEND}" analyse "$@" > out.log 2>&1 || {
      echo "FAILED to run with $*"; cat out.log; exit 1; }
  got=$(grep -o 'MAIN=.*' out.log | head -1 | sed 's|MAIN=||')
  if [ "${got}" != "${expected}" ]; then
    echo "FAILED: expected main ${expected}, got ${got}"
    cat out.log
    exit 1
  fi
  echo "OK: main is ${got}"
}

# without a repository in the reference, the configuration decides
check main_repo ':t'
check main_repo 'baz'

# a reference naming a repository selects it
check other_repo 'other_repo//:t'

# ... also behind a value-taking option of the backend, which must not be
# mistaken for the reference
check other_repo --dump-graph "${WORK}/graph.json" 'other_repo//:t'

# ... and after a "--"
check other_repo -- 'other_repo//:t'

# --main still works on its own
check other_repo --main other_repo ':t'

# ... but the reference takes precedence over it
check main_repo --main other_repo 'main_repo//:t'

echo OK
