# bash completion for neowall

_neowall() {
    local cur prev words cword
    _init_completion -n = || return

    local commands="new import docs preview watch kill next pause resume reload
        set list current status ping pause-shader resume-shader set-terminal"
    local opts="--config --foreground --verbose --help --version --date --timelapse"

    # the subcommand, if any
    local cmd="" i
    for ((i = 1; i < cword; i++)); do
        case ${words[i]} in
            -c | --config | --date | --timelapse) ((i++)) ;;
            -*) ;;
            *) cmd=${words[i]}; break ;;
        esac
    done

    case $prev in
        -c | --config) _filedir vibe; return ;;
        --date | --timelapse) return ;;
        --template) COMPREPLY=($(compgen -W "scene flat reactive" -- "$cur")); return ;;
    esac

    case $cmd in
        "")
            if [[ $cur == -* ]]; then
                COMPREPLY=($(compgen -W "$opts" -- "$cur"))
            else
                COMPREPLY=($(compgen -W "$commands" -- "$cur"))
            fi
            ;;
        new)
            if [[ $cur == --template=* ]]; then
                local v=${cur#--template=}
                COMPREPLY=($(compgen -P --template= -W "scene flat reactive" -- "$v"))
            else
                COMPREPLY=($(compgen -W "--template= --force --list" -- "$cur"))
                [[ ${COMPREPLY-} == *= ]] && compopt -o nospace 2>/dev/null
            fi
            ;;
        import)
            [[ $cur == -* ]] && COMPREPLY=($(compgen -W "--force" -- "$cur")) || _filedir json
            ;;
        preview | watch)
            if [[ $cur == -* ]]; then
                COMPREPLY=($(compgen -W "--date= --timelapse=" -- "$cur"))
                compopt -o nospace 2>/dev/null
            else
                _filedir '@(glsl|frag|neowall)'
            fi
            ;;
        set-terminal)
            COMPREPLY=($(compgen -c -- "$cur"))
            ;;
    esac
} && complete -F _neowall neowall
