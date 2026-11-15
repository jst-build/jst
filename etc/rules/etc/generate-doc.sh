#!/bin/sh
# Copyright 2022 Huawei Cloud Computing Technology Co., Ltd.
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

readonly ROOT=$(readlink -f $(dirname $0)/..)

: ${OUTFILE:=README.md}

# remove single linebreaks from multiline strings
fixlinebreaks() {
  printf '%s' "$(cat)" | sed 's/\\n/BREAK/g' | sed 's/BREAKBREAK/\\n\\n/g' | sed 's/BREAK"/"/g' | sed 's/BREAK/\ /g'
}

doc2md() {
  local MAIN="$1"
  local MODULE="$2"
  local RULE="$3"

  local RULE_DOC="$(jst describe --main "$MAIN" --rule --json "//$MODULE:$RULE" | fixlinebreaks)"
  local DOC="$(printf '%s' "$RULE_DOC" | jq -r '.doc')"
  local FIELD_DOC="$(printf '%s' "$RULE_DOC" | jq -r '.field_doc')"
  local CONFIG_DOC="$(printf '%s' "$RULE_DOC" | jq -r '.config_doc | delpaths([["AR"], ["CC"], ["CXX"], ["CFLAGS"], ["CXXFLAGS"],["LDFLAGS"], ["ADD_CFLAGS"], ["ADD_CXXFLAGS"], ["ADD_LDFLAGS"], ["ENV"], ["BUILD_POSITION_INDEPENDENT"]])')"

  echo "### Rule \`//$MODULE:$RULE\`"
  echo
  printf '%s\n' "$DOC"
  echo
  echo "| Field | Description |"
  echo "| ----- | ----------- |"
  printf '%s' "$FIELD_DOC" \
    | jq -r 'keys_unsorted[] as $k | "| `\($k)` | \(.[$k]) |"'
  echo
  if [ "$(printf '%s' "$CONFIG_DOC" | jq -r 'length')" -gt 0 ]
  then
      echo "| Config variable | Description |"
      echo "| --------------- | ----------- |"
      printf '%s' "$CONFIG_DOC" \
        | jq -r 'keys_unsorted[] as $k | "| `\($k)` | \(.[$k]) |"'
      echo
  fi
}

rm -f "$OUTFILE"
( cat "$ROOT/etc/README.template.md"
  doc2md rules CC defaults
  doc2md rules CC/proto defaults
  doc2md rules CC/foreign defaults
  doc2md rules patch defaults
  doc2md rules CC binary
  doc2md rules CC library
  doc2md rules CC/prebuilt library
  doc2md rules CC/pkgconfig system_library
  doc2md rules CC install-with-deps
  doc2md rules CC/test test
  doc2md rules shell defaults
  doc2md rules shell cmds
  doc2md rules shell/test script
  doc2md rules CC/foreign/cmake library
  doc2md rules CC/foreign/cmake data
  doc2md rules CC/foreign/make library
  doc2md rules CC/foreign/make data
  doc2md rules CC/foreign/shell library
  doc2md rules CC/foreign/shell data
  doc2md rules proto library
  doc2md rules data staged
  doc2md rules patch file
  doc2md rules CC/auto config
  doc2md rules CC/auto config_file
  doc2md rules test suite
  doc2md rules test matrix
  doc2md rules lint targets
  doc2md rules lint defaults
) >> "$OUTFILE"
