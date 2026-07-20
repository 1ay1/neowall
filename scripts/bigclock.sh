#!/bin/sh
# Dependency-free big ASCII clock. Renders HH:MM:SS as 5-row block digits.
# Used as a neowall terminal-wallpaper demo that unmistakably animates.

# 5-row glyphs for 0-9 and colon. Each glyph is 5 lines, stored row-major.
g0="  ###  # #### # # # # ## # # ### ####  ###  "
# We build a lookup by row instead — clearer:

# Rows for each character, 5 rows tall, fixed 5 cols wide.
d0_0=" ### "; d0_1="#   #"; d0_2="#   #"; d0_3="#   #"; d0_4=" ### "
d1_0="  #  "; d1_1=" ##  "; d1_2="  #  "; d1_3="  #  "; d1_4=" ### "
d2_0=" ### "; d2_1="#   #"; d2_2="  ## "; d2_3=" #   "; d2_4="#####"
d3_0="#### "; d3_1="    #"; d3_2=" ### "; d3_3="    #"; d3_4="#### "
d4_0="#   #"; d4_1="#   #"; d4_2="#####"; d4_3="    #"; d4_4="    #"
d5_0="#####"; d5_1="#    "; d5_2="#### "; d5_3="    #"; d5_4="#### "
d6_0=" ### "; d6_1="#    "; d6_2="#### "; d6_3="#   #"; d6_4=" ### "
d7_0="#####"; d7_1="    #"; d7_2="   # "; d7_3="  #  "; d7_4="  #  "
d8_0=" ### "; d8_1="#   #"; d8_2=" ### "; d8_3="#   #"; d8_4=" ### "
d9_0=" ### "; d9_1="#   #"; d9_2=" ####"; d9_3="    #"; d9_4=" ### "
dc_0="   "; dc_1="  #  "; dc_2="   "; dc_3="  #  "; dc_4="   "

glyph_row() { # $1=char $2=row -> echo the 5-col (or 3 for colon) strip
  eval "printf '%s' \"\$d${1}_${2}\""
}

printf '\033[?25l' # hide cursor
trap 'printf "\033[?25h\033[0m\n"; exit 0' INT TERM HUP

while :; do
  now=$(date +%H:%M:%S)
  printf '\033[H\033[2J' # home + clear
  printf '\n\n'
  r=0
  while [ "$r" -lt 5 ]; do
    printf '  '
    i=1
    while [ "$i" -le 8 ]; do
      c=$(printf '%s' "$now" | cut -c "$i")
      case "$c" in
        :) key=c ;;
        *) key="$c" ;;
      esac
      glyph_row "$key" "$r"
      printf '  '
      i=$((i + 1))
    done
    printf '\n'
    r=$((r + 1))
  done
  # Sub-second update so motion is obvious.
  sleep 0.2
done
