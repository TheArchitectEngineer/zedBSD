#!/bin/sh
# ws035-p091: sets the terminal's title the way a shell or a program does (typed into terminal by
# plan/ws035/tests/zdesktop-p091.sh, which copies this file to the guest's /tmp).
#   sh p091-title.sh 1     OSC 0 ended by BEL: "Build logs"
#   sh p091-title.sh 2     OSC 2 ended by ESC \: "日本語 notes" (UTF-8)
#   sh p091-title.sh 3     OSC 2 of 50 "日" (150 bytes): kept up to whole characters that fit
#   sh p091-title.sh 4     OSC 1 (the icon's name): not a title, nothing changes
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
case $1 in
1)
	printf '\033]0;Build logs\007'
	;;
2)
	printf '\033]2;\346\227\245\346\234\254\350\252\236 notes\033\\'
	;;
3)
	text=
	i=0
	while [ $i -lt 50 ]; do
		text="$text$(printf '\346\227\245')"
		i=$((i + 1))
	done
	printf '\033]2;%s\007' "$text"
	;;
4)
	printf '\033]1;icon name\007'
	;;
esac
