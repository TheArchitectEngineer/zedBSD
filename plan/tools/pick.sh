# Q1 の cherry-pick の道具（2026-10-07 に /tmp から写した）: pick SHA...
pick() {
	git cherry-pick -n "$@" >/tmp/claude-1000/p.txt 2>&1
	u=$(git diff --name-only --diff-filter=U)
	if [ -n "$u" ]; then echo "PICK CONFLICT: $u"; tail -2 /tmp/claude-1000/p.txt; return 1; fi
	if git grep -q '^<<<<<<< ' -- plan userland src include platform tools; then echo MARKERS; return 1; fi
	git commit -q -m WIP && git log --oneline -1
}
