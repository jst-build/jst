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

###
# Verify that SSH remotes are fetched correctly, and that the SSH configuration
# of the user (ssh command, environment) is honored while doing so.
#
# The remote side is mocked: instead of a real ssh connection, a stub ssh is
# used that runs the requested git-upload-pack locally. This keeps the test
# hermetic, while still exercising the very mechanism that a real ssh setup
# relies upon.
#
# If libgit2 provides SSH support natively (by executing the system's ssh
# binary), no shell-out to the git binary may happen; otherwise, the fetch is
# expected to go through the git binary, which honors the same settings.
##

JUST_MR="$(pwd)/bin/mr-tool-under-test"
JUST="$(pwd)/bin/tool-under-test"
PROBE="$(pwd)/utils/ssh-support-probe"
WRKDIR="$(pwd)/work"
MOCK_TOOLS="$(pwd)/mock-bin"
UPSTREAM="${TEST_TMPDIR}/upstream.git"
FAKE_HOME="${TEST_TMPDIR}/fake-home"
WITNESS="${TEST_TMPDIR}/witness"
OUT="${TEST_TMPDIR}/out"

# Determine how the tool under test handles SSH remotes
if "${PROBE}"; then
  NATIVE_SSH=yes
else
  NATIVE_SSH=no
fi
echo "Native SSH support in libgit2: ${NATIVE_SSH}"
echo

# Create the upstream repository to fetch from
mkdir -p "${TEST_TMPDIR}/src"
cd "${TEST_TMPDIR}/src"
git init 2>&1
git branch -m stable-1.0 2>&1
echo 'checked-out sources' > sources.txt
echo '{}' > TARGETS
git config user.email "nobody@example.org" 2>&1
git config user.name "Nobody" 2>&1
git add . 2>&1
git commit -m "Sample output" 2>&1
COMMIT=$(git log -n 1 --format="%H")
git clone --bare . "${UPSTREAM}" 2>&1
echo "Created commit ${COMMIT} in ${UPSTREAM}"
echo

mkdir -p "${MOCK_TOOLS}" "${WITNESS}"

# Stub ssh: libgit2 (and git) invoke it as
#   <ssh> [-p port] [user@]host "git-upload-pack '<path>'"
# The last argument is the remote command; running it locally stands in for
# the connection. The stub also refuses to work unless SSH_MARKER is visible
# in its environment, which is how we check that the environment reaches it.
MOCK_SSH="${MOCK_TOOLS}/mock-ssh"
cat > "${MOCK_SSH}" <<'EOF'
#!/bin/sh
echo "mock-ssh invoked as: $*" >&2
if [ "${SSH_MARKER:-}" != "sEcReT" ]; then
  echo "mock-ssh: SSH_MARKER not visible in environment" >&2
  exit 1
fi
touch "${SSH_WITNESS}"
eval "exec $(eval echo \$$#)"
EOF
chmod 755 "${MOCK_SSH}"

# Mock git: records that a shell-out happened, then behaves like real git
MOCK_GIT="${MOCK_TOOLS}/mock-git"
cat > "${MOCK_GIT}" <<'EOF'
#!/bin/sh
echo "mock-git invoked as: $*" >&2
touch "${GIT_WITNESS}"
exec git "$@"
EOF
chmod 755 "${MOCK_GIT}"

export SSH_WITNESS="${WITNESS}/ssh"
export GIT_WITNESS="${WITNESS}/git"
export SSH_MARKER=sEcReT

# Global git config used to test core.sshCommand pickup
mkdir -p "${FAKE_HOME}"
cat > "${FAKE_HOME}/.gitconfig" <<EOF
[core]
	sshCommand = ${MOCK_SSH}
EOF

mkdir -p "${WRKDIR}"

# Run a single scenario.
#  $1 description, $2 remote URL, $3 JSON list of variables to inherit
run_scenario() {
  desc="$1"
  url="$2"
  inherit="$3"

  echo "--- Scenario: ${desc}"
  rm -rf "${WITNESS}" "${OUT}" "${WRKDIR}/repo"
  mkdir -p "${WITNESS}"
  mkdir -p "${WRKDIR}/repo"
  cd "${WRKDIR}/repo"
  touch ROOT
  cat > repos.json <<EOF
{ "repositories":
  { "":
    { "repository":
      { "type": "git"
      , "commit": "${COMMIT}"
      , "repository": "${url}"
      , "branch": "stable-1.0"
      , "inherit env": ${inherit}
      }
    }
  }
}
EOF
  "${JUST_MR}" --norc --just "${JUST}" \
               --local-build-root "${TEST_TMPDIR}/lbr-$$-${SCENARIO}" \
               --git "${MOCK_GIT}" --log-limit 5 \
               install -o "${OUT}" '' sources.txt 2>&1
  grep checked-out "${OUT}/sources.txt"

  # The stub ssh must have been used; that is the only way to reach upstream
  test -f "${SSH_WITNESS}" || {
    echo "FAILED: stub ssh was not invoked"; exit 1;
  }
  if [ "${NATIVE_SSH}" = yes ]; then
    if [ -f "${GIT_WITNESS}" ]; then
      echo "FAILED: shelled out to git despite native SSH support"; exit 1
    fi
    echo "OK (no shell-out, as expected)"
  else
    test -f "${GIT_WITNESS}" || {
      echo "FAILED: expected a shell-out to git"; exit 1;
    }
    echo "OK (via shell-out, as expected)"
  fi
  echo
  SCENARIO=$((SCENARIO+1))
}

SCENARIO=1

# 1. GIT_SSH_COMMAND is honored for an ssh:// URL
export GIT_SSH_COMMAND="${MOCK_SSH}"
run_scenario "ssh:// URL, GIT_SSH_COMMAND" \
             "ssh://git@dummy-host${UPSTREAM}" \
             '["PATH", "GIT_SSH_COMMAND", "SSH_MARKER", "SSH_WITNESS", "GIT_WITNESS"]'

# 2. The same for an scp-style location
run_scenario "scp-style location, GIT_SSH_COMMAND" \
             "git@dummy-host:${UPSTREAM}" \
             '["PATH", "GIT_SSH_COMMAND", "SSH_MARKER", "SSH_WITNESS", "GIT_WITNESS"]'

# 3. Without GIT_SSH_COMMAND, core.sshCommand from the global git config is used
unset GIT_SSH_COMMAND
export HOME="${FAKE_HOME}"
run_scenario "ssh:// URL, core.sshCommand from global git config" \
             "ssh://git@dummy-host${UPSTREAM}" \
             '["PATH", "HOME", "SSH_MARKER", "SSH_WITNESS", "GIT_WITNESS"]'

# 4. Natively handled SSH remotes inherit the full environment of just-mr, so
#    the stub ssh sees SSH_MARKER even though no variable is inherited at all
if [ "${NATIVE_SSH}" = yes ]; then
  run_scenario "ssh:// URL, environment inherited without 'inherit env'" \
               "ssh://git@dummy-host${UPSTREAM}" \
               '[]'
else
  echo "--- Skipping environment-inheritance scenario (no native SSH support)"
  echo
fi

# 5. Updating the commit of a branch uses the same SSH setup
echo "--- Scenario: commit update"
rm -rf "${WITNESS}"; mkdir -p "${WITNESS}"
cd "${WRKDIR}/repo"
cat > repos.json <<EOF
{ "repositories":
  { "":
    { "repository":
      { "type": "git"
      , "commit": "0000000000000000000000000000000000000000"
      , "repository": "ssh://git@dummy-host${UPSTREAM}"
      , "branch": "stable-1.0"
      , "inherit env": ["PATH", "HOME", "SSH_MARKER", "SSH_WITNESS", "GIT_WITNESS"]
      }
    }
  }
}
EOF
"${JUST_MR}" --norc --local-build-root "${TEST_TMPDIR}/lbr-update" \
             --git "${MOCK_GIT}" --log-limit 5 update '' > updated.json 2>update.log || {
  cat update.log; echo "FAILED: update failed"; exit 1;
}
cat update.log
grep "${COMMIT}" updated.json || {
  echo "FAILED: update did not report commit ${COMMIT}"; cat updated.json; exit 1;
}
test -f "${SSH_WITNESS}" || { echo "FAILED: stub ssh was not invoked"; exit 1; }
if [ "${NATIVE_SSH}" = yes ]; then
  if [ -f "${GIT_WITNESS}" ]; then
    echo "FAILED: shelled out to git despite native SSH support"; exit 1
  fi
fi
echo "OK"
echo

echo DONE
