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

# The option --main selects the main repository, and the repository segment of
# the target reference is a name that repository binds, just as it is in a
# target file. So the two compose: --main picks the point of view, the
# reference names a target from it. This runs through the launcher, which is
# bin/jst.py for the bootstrap variant of this test suite, so both launchers
# are covered.

set -eu

readonly JUST="${PWD}/bin/tool-under-test"
readonly JUST_MR="${PWD}/bin/mr-tool-under-test"
readonly LBR="${TEST_TMPDIR}/local-build-root"
readonly WORK="${TEST_TMPDIR}/work"
mkdir -p "${WORK}"

cd "${WORK}"

mkdir -p main_repo other_repo dep_repo
touch main_repo/ROOT other_repo/ROOT dep_repo/ROOT
for d in main_repo other_repo dep_repo; do
  cat > "${d}/TARGETS" <<'EOF'
{"t": {"type": "generic", "outs": ["o"], "cmds": ["touch o"]}}
EOF
done

# Both main_repo and other_repo bind dep_repo, under the name "dep" that
# differs from the repository's own name, so that resolving the segment is
# told apart from taking it verbatim.
cat > repos.json <<EOF
{ "main": "main_repo"
, "repositories":
  { "main_repo":
    { "repository": {"type": "file", "path": "main_repo"}
    , "bindings": {"dep": "dep_repo"}
    }
  , "other_repo":
    { "repository": {"type": "file", "path": "other_repo"}
    , "bindings": {"dep": "dep_repo"}
    }
  , "dep_repo": {"repository": {"type": "file", "path": "dep_repo"}}
  }
}
EOF
cat repos.json
echo

# Analyse through the launcher and compare the target the backend reports with
# the expected one. Everything after the expectation is passed on verbatim.
check() {
  expected="$1"
  shift
  echo "--- analyse $*"
  "${JUST_MR}" --norc --local-build-root "${LBR}" -C repos.json \
    --backend "${JUST}" analyse "$@" > out.log 2>&1 || {
      echo "FAILED to analyse $*"; cat out.log; exit 1; }
  got=$(grep -o "Requested target '[^']*'" out.log | head -1 \
        | sed "s|Requested target '||; s|'$||")
  if [ "${got}" != "${expected}" ]; then
    echo "FAILED: expected ${expected}, got ${got}"
    cat out.log
    exit 1
  fi
  echo "OK: ${got}"
}

# expect the analysis to fail, with the given text in the report
check_fail() {
  expected="$1"
  shift
  echo "--- analyse $* (expecting failure)"
  if "${JUST_MR}" --norc --local-build-root "${LBR}" -C repos.json \
       --backend "${JUST}" analyse "$@" > out.log 2>&1; then
    echo "FAILED: analysing $* should not have succeeded"
    cat out.log
    exit 1
  fi
  grep -q "${expected}" out.log || {
    echo "FAILED: expected '${expected}' in the report"
    cat out.log; exit 1; }
  echo "OK"
}

# without a repository in the reference, the configuration decides
check 'main_repo//:t' ':t'

# the repository segment is a binding of the main repository
check 'dep_repo//:t' 'dep//:t'

# --main selects the main repository
check 'other_repo//:t' --main other_repo ':t'

# ... and the reference is then resolved from its point of view
check 'dep_repo//:t' --main other_repo 'dep//:t'

# a repository is named by its binding, not by its own name, and the main
# repository does not bind itself
check_fail 'not bound by repository' 'dep_repo//:t'
check_fail 'not bound by repository' 'main_repo//:t'

echo OK
