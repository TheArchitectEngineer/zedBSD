#!/bin/sh
# Deletes every issue (open and closed) of a GitHub repository with gh.
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
#
# usage: plan/tools/github/delete-all-issues.sh [-n] OWNER/REPO
#   -n  list the issues that would be deleted and stop (dry run)
#
# Deleting an issue cannot be undone.  gh must be logged in as an account
# with admin rights on the repository (gh auth status).  Pull requests are
# not touched (gh issue list does not return them).  The script asks for
# the repository name to be typed again before it deletes anything.
set -eu

dry_run=0
if [ "${1:-}" = "-n" ]; then
	dry_run=1
	shift
fi
if [ "$#" -ne 1 ]; then
	echo "usage: $0 [-n] OWNER/REPO" >&2
	exit 2
fi
repo=$1

# The account and its access must be checked before anything is listed.
gh auth status >/dev/null

# Every issue number, open and closed; the limit is above any real count.
numbers=$(gh issue list --repo "$repo" --state all --limit 100000 --json number --jq '.[].number')
count=$(printf '%s\n' "$numbers" | grep -c '[0-9]' || true)
echo "$repo: $count issue(s)"
if [ "$count" -eq 0 ]; then
	exit 0
fi

# The dry run shows the numbers and titles only.
if [ "$dry_run" -eq 1 ]; then
	gh issue list --repo "$repo" --state all --limit 100000 --json number,title,state \
		--jq '.[] | "\(.number)\t\(.state)\t\(.title)"'
	exit 0
fi

# The repository name typed again is the confirmation.
printf 'Type %s to delete all %s issue(s) permanently: ' "$repo" "$count"
read -r answer
if [ "$answer" != "$repo" ]; then
	echo "not confirmed; nothing deleted" >&2
	exit 1
fi

# Deletes one at a time, pausing briefly so the API's rate limit is not hit.
deleted=0
failed=0
for number in $numbers; do
	if gh issue delete "$number" --repo "$repo" --yes >/dev/null 2>&1; then
		deleted=$((deleted + 1))
		echo "deleted #$number ($deleted/$count)"
	else
		failed=$((failed + 1))
		echo "FAILED #$number" >&2
	fi
	sleep 1
done

echo "$repo: deleted $deleted, failed $failed"
if [ "$failed" -ne 0 ]; then
	exit 1
fi
