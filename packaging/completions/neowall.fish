# fish completion for neowall

set -l cmds new import docs preview watch kill next pause resume reload set list current status ping pause-shader resume-shader set-terminal

complete -c neowall -f
complete -c neowall -s c -l config -rF -d 'Config file'
complete -c neowall -s f -l foreground -d 'Stay in the foreground'
complete -c neowall -s v -l verbose -d 'Verbose logging'
complete -c neowall -s h -l help -d 'Show help'
complete -c neowall -s V -l version -d 'Show version'
complete -c neowall -l date -x -d 'Shift the shader clock (+6d)'
complete -c neowall -l timelapse -x -d 'Speed up the shader clock (30d/20s)'

complete -c neowall -n "not __fish_seen_subcommand_from $cmds" -a new -d 'Create a shader from a template'
complete -c neowall -n "not __fish_seen_subcommand_from $cmds" -a import -d 'Import a Shadertoy export'
complete -c neowall -n "not __fish_seen_subcommand_from $cmds" -a docs -d 'Shader helpers and uniforms'
complete -c neowall -n "not __fish_seen_subcommand_from $cmds" -a preview -d 'Run one shader in the foreground'
complete -c neowall -n "not __fish_seen_subcommand_from $cmds" -a watch -d 'Preview and reload on save'
complete -c neowall -n "not __fish_seen_subcommand_from $cmds" -a kill -d 'Stop the daemon'
complete -c neowall -n "not __fish_seen_subcommand_from $cmds" -a next -d 'Next wallpaper'
complete -c neowall -n "not __fish_seen_subcommand_from $cmds" -a pause -d 'Pause cycling'
complete -c neowall -n "not __fish_seen_subcommand_from $cmds" -a resume -d 'Resume cycling'
complete -c neowall -n "not __fish_seen_subcommand_from $cmds" -a reload -d 'Reload the configuration'
complete -c neowall -n "not __fish_seen_subcommand_from $cmds" -a set -d 'Set wallpaper by index'
complete -c neowall -n "not __fish_seen_subcommand_from $cmds" -a list -d 'List wallpapers'
complete -c neowall -n "not __fish_seen_subcommand_from $cmds" -a current -d 'Current wallpaper'
complete -c neowall -n "not __fish_seen_subcommand_from $cmds" -a status -d 'Current wallpaper'
complete -c neowall -n "not __fish_seen_subcommand_from $cmds" -a ping -d 'Is a daemon answering'
complete -c neowall -n "not __fish_seen_subcommand_from $cmds" -a pause-shader -d 'Freeze the shader'
complete -c neowall -n "not __fish_seen_subcommand_from $cmds" -a resume-shader -d 'Resume the shader'
complete -c neowall -n "not __fish_seen_subcommand_from $cmds" -a set-terminal -d 'Swap the terminal command'

complete -c neowall -n "__fish_seen_subcommand_from new" -l template -xa 'scene flat reactive' -d 'Template'
complete -c neowall -n "__fish_seen_subcommand_from new import" -s f -l force -d 'Overwrite'
complete -c neowall -n "__fish_seen_subcommand_from new" -l list -d 'List templates'
complete -c neowall -n "__fish_seen_subcommand_from import" -F -a '(__fish_complete_suffix .json)'
complete -c neowall -n "__fish_seen_subcommand_from preview watch" -F -a '(__fish_complete_suffix .glsl .frag .neowall)'
complete -c neowall -n "__fish_seen_subcommand_from set-terminal" -xa '(__fish_complete_command)'
