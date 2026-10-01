# Copyright 2022 Huawei Cloud Computing Technology Co., Ltd.
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

########################### jst_backend completion

# The option names of a help text, which lists them at the beginning of a line,
# possibly several separated by commas. Only those are taken, so that options
# merely mentioned in a description are not offered.
_jst_help_options(){
    $@ --help 2>/dev/null \
        | sed -nE 's/^[[:space:]]{1,12}(-[^[:space:],]+([[:space:]]*,[[:space:]]*-[^[:space:],]+)*).*/\1/p' \
        | tr ',' '\n' | tr -d '[:blank:]' | grep '^-' | sort -u
}

_jst_backend_subcommand_options(){
    _jst_help_options jst_backend $1
}

_jst_backend_targets(){
    command -v python3 &>/dev/null || return
    python3 - <<EOF
from json import load
from os import environ, path


def print_targets(target_file):
    if not path.exists(target_file):
        return
    with open(target_file) as f:
        targets = load(f)
        for t in targets.keys():
            print(t)
    exit()


def main(conf, repo, prev):
    if conf != "":
        if conf[0] == "$":
            # try to expand if it is an env var, otherwise treat as it is
            conf = environ.get(conf[1:]) or conf
        with open(conf, "r") as f:
            d = load(f)
        repos = d["repositories"]
        repo = repo if repo != "" else d["main"]
        workspace = repos[repo]["workspace_root"]
        # only file-type repositories are supported
        root = workspace[1]
        target_file = repos[repo].get("target_file_name", "TARGETS")
        # if prev is a directory, then look for targets there
        module = path.join(root, prev)

        # non-file type repos don't satisfy the following conditions
        # so, we fall back to
        if path.isdir(module):
            print_targets(path.join(module, target_file))
        else:
            print_targets(path.join(root, target_file))

    # fall back to current working directory
    target_file = "TARGETS"

    # if prev is not valid subdir of current working directory, the function
    # will return with no output
    print_targets(path.join(prev, target_file))

    # last option: try to read targets in current working directory
    print_targets(target_file)


main('$1', '$2', '$3')
EOF
}

_jst_backend_completion(){
    local readonly SUBCOMMANDS=(build analyse describe install-cas install rebuild gc eval execute -h --help version add-to-cas serve)
    local word=${COMP_WORDS[$COMP_CWORD]}
    local prev=${COMP_WORDS[$((COMP_CWORD-1))]}
    local cmd=${COMP_WORDS[1]}
    local main
    local conf
    # first check if the current word matches a subcommand
    # if we check directly with cmd, we fail to autocomplete install to install-cas
    if [[ $word =~ ^(build|analyse|describe|install-cas|install|rebuild|gc|eval|execute) ]]
    then
        COMPREPLY=($(compgen -W "${SUBCOMMANDS[*]}" -- $word))
    elif [[ $cmd =~ ^(install-cas|execute|gc|eval) ]]
    then
        local _opts=($(_jst_backend_subcommand_options $cmd))
        COMPREPLY=($(compgen -f -W "${_opts[*]}" -- $word ))
        compopt -o plusdirs -o bashdefault -o default
    elif [[ $cmd =~ ^(build|analyse|describe|install|rebuild) ]]
    then
        local _opts=($(_jst_backend_subcommand_options $cmd))
        # look for -C and --main
        for i in "${!COMP_WORDS[@]}"
        do
            if [[ "${COMP_WORDS[i]}" == "--main" ]]
            then
                main="${COMP_WORDS[$((++i))]}"
            fi
            if [[ "${COMP_WORDS[i]}" == "-C" ]] || [[ "${COMP_WORDS[i]}" == "--repository-config" ]]
            then
                conf="${COMP_WORDS[$((++i))]}"
            fi
        done
        # if $conf is empty and this function is invoked by jst
        # we use the auto-generated conf file
        if [ -z "$conf" ]; then conf="${jstmrconf}";
        fi
        local _targets=($(_jst_backend_targets "$conf" "$main" "$prev" 2>/dev/null))
        COMPREPLY=($(compgen -f -W "${_opts[*]} ${_targets[*]}" -- $word ))
        compopt -o plusdirs -o bashdefault -o default
    else
        COMPREPLY=($(compgen -W "${SUBCOMMANDS[*]}" -- $word))
    fi
}

complete -F _jst_backend_completion jst_backend

########################### jst completion
_jst_options(){
    _jst_help_options $1
}

_jst_parse_subcommand() {
    # The options of jst may appear before or after the subcommand, so they are
    # skipped here. Listed are those taking a value; any other word starting
    # with a dash is taken for a flag. The subcommand 'backend' is skipped as
    # well, so that the backend subcommand following it is found.
    local readonly OPTIONS=("--absent\n--backend\n--checkout-locations\n--defines\n--distdir\n--dump-rc\n--fetch-cacert\n--fetch-jobs\n--git\n--initial-backoff-seconds\n--local-build-root\n--local-launcher\n--log-file\n--log-limit\n--main\n--max-attempts\n--max-backoff-seconds\n--parallel\n--rc\n--remote-execution-address\n--remote-instance-name\n--remote-serve-address\n--repository-config\n--restrict-stderr-log-limit\n--tls-ca-cert\n--tls-client-cert\n--tls-client-key\n-C\n-D\n-J\n-L\n-R\n-f")
    shift
    while [ -n "$1" ]; do
        if [ "$1" = "--" ]; then shift; break; fi
        if [ "$1" = "backend" ]; then shift; continue; fi
        if echo -e "$OPTIONS" | grep -q -- "^$1$"; then shift; shift; continue; fi
        case "$1" in -*) shift; continue;; esac
        break
    done
    echo "$1"
}

_jst_repos(){
    command -v python3 &>/dev/null || return
    local CONF=$(jst setup --all 2>/dev/null)
    if [ ! -f "$CONF" ]; then return; fi
    python3 - <<EOF
from json import load
from os import path

if path.exists("$CONF"):
    with open("$CONF") as f:
        repos = load(f).get("repositories", {})
        for r in repos.keys():
            print(r)
EOF
}

_jst_completion(){
    local readonly SUBCOMMANDS=(version setup setup-env fetch update backend gc-repo add-to-cas analyse build describe gc eval execute install install-cas rebuild serve version -h --help)
    local word=${COMP_WORDS[$COMP_CWORD]}
    local prev=${COMP_WORDS[$((COMP_CWORD-1))]}
    local cmd=$(_jst_parse_subcommand "${COMP_WORDS[@]}")
    # first check if the current word matches a subcommand
    # if we check directly with cmd, we fail to autocomplete setup to setup-env and install to install-cas
    if [[ $word =~ ^(setup|setup-env|install-cas|install) ]]
    then
        COMPREPLY=($(compgen -W "${SUBCOMMANDS[*]}" -- $word))
    elif [ "$prev" = "--main" ]
    then
        local _repos=($(_jst_repos $prev))
        COMPREPLY=($(compgen -W "${_repos[*]}}" -- $word))
    elif [ "$prev" = "--distdir" ] || [ "$prev" = "--backend" ] || [ "$prev" = "--local-build-root" ] || [ "$prev" = "--rc" ] || [ "$prev" = "-C" ] || [ "$prev" = "-L" ]
    then
        compopt -o bashdefault -o default
    elif [[ "$cmd" =~ ^(setup|setup-env|fetch|update|gc-repo) ]]
    then
        # jst subcommand options and repository names
        local _opts=($(_jst_options "jst $cmd"))
        local _repos=($(_jst_repos $prev))
        COMPREPLY=($(compgen -f -W "${_opts[*]} ${_repos[*]}" -- $word ))
    elif [[ "$cmd" =~ ^(version|build|analyse|describe|install-cas|install|rebuild|gc|eval|execute|serve|add-to-cas) ]]
    then
        if [[ $word =~ ^- ]]
        then
            # the options of jst for this subcommand, followed by those of
            # jst_backend, both printed by the help of jst
            local _opts=($(_jst_options "jst $cmd"))
            COMPREPLY=($(compgen -W "${_opts[*]}" -- $word))
        else
            # modules/targets eventually using the auto-generated configuration
            local jstmrconf=$(jst setup --all 2>/dev/null)
            _jst_backend_completion
        fi
    else
        # jst top-level options
        local _opts=($(_jst_options "jst"))
        COMPREPLY=($(compgen -W "${_opts[*]} ${SUBCOMMANDS[*]}" -- $word))
    fi
}

complete -F _jst_completion jst
