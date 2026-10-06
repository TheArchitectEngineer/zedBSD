#!/bin/sh
# ws129-p004: the release workflow's parts that run without GitHub, on the host.
#  1. tools/release/release-tag.sh in a throwaway clone with its own tags: rc and final tags classified against
#     VERSION, other versions and rc0 refused; the rc a final tag promotes (the highest in its history, or the one
#     named, a missing one refused); a final tag that changes only docs/release/ promotes, one that changes more
#     does not.
#  2. make release-info with the release configuration (version, name, zip=n) and the shadow of a locked root
#     (root's hash replaced by *, the other lines as they are).
#  3. The workflow's step "Check the sizes, sum the assets and take the notes", taken out of release.yml and run
#     over small stand-in assets: SHA256SUMS written, the notes copied; without the notes it fails.
#   sh plan/ws129/tests/host-release.sh [BUILD]   (default build/ws129-p004-host)
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
set -u
cd "$(dirname -- "$0")/../../.."
top=$(pwd)
build=${1:-build/ws129-p004-host}
rm -rf "$build"
mkdir -p "$build"
build=$(cd "$build" && pwd)
passed=0
failed=0

# Counts a check: its name, then the command that must succeed.
ok() {
	name=$1
	shift
	if "$@" >/dev/null 2>&1; then
		passed=$((passed + 1))
	else
		echo "FAIL: $name"
		failed=$((failed + 1))
	fi
}

# Counts a check that must fail.
refused() {
	name=$1
	shift
	if "$@" >/dev/null 2>&1; then
		echo "FAIL: $name (taken)"
		failed=$((failed + 1))
	else
		passed=$((passed + 1))
	fi
}

# 1. The tag checks, in a clone whose tags are its own.
clone=$build/clone
git clone -q --no-tags "$top" "$clone"
cp tools/release/release-tag.sh "$clone/tools/release/release-tag.sh"
(
	cd "$clone"
	printf '1.0.0-beta2\n' > VERSION
	git add -A && git -c user.name=t -c user.email=t@t commit -qm WIP --allow-empty
	git tag zedbsd-1.0.0-beta2-rc1
	echo x > note && git add note && git -c user.name=t -c user.email=t@t commit -qm WIP
	git tag zedbsd-1.0.0-beta2-rc2
	mkdir -p docs/release && echo notes > docs/release/zedbsd-1.0.0-beta2.md
	git add -A && git -c user.name=t -c user.email=t@t commit -qm WIP
	git tag zedbsd-1.0.0-beta2
)
tag() { (cd "$clone" && sh tools/release/release-tag.sh "$@"); }
ok "classify rc2" sh -c "cd '$clone' && sh tools/release/release-tag.sh classify zedbsd-1.0.0-beta2-rc2 | grep -qx 'kind=build' && sh tools/release/release-tag.sh classify zedbsd-1.0.0-beta2-rc2 | grep -qx 'rc=2'"
ok "classify final" sh -c "cd '$clone' && sh tools/release/release-tag.sh classify zedbsd-1.0.0-beta2 | grep -qx 'kind=promote'"
refused "classify another version" tag classify zedbsd-1.0.0-beta2-rc1
refused "classify rc0" tag classify zedbsd-1.0.0-beta2-rc0
refused "classify rc without a number" tag classify zedbsd-1.0.0-beta2-rcx
ok "rc highest" sh -c "cd '$clone' && test \"\$(sh tools/release/release-tag.sh rc zedbsd-1.0.0-beta2)\" = zedbsd-1.0.0-beta2-rc2"
ok "rc named" sh -c "cd '$clone' && test \"\$(sh tools/release/release-tag.sh rc zedbsd-1.0.0-beta2 1)\" = zedbsd-1.0.0-beta2-rc1"
refused "rc missing" tag rc zedbsd-1.0.0-beta2 5
ok "promotable rc2" tag promotable zedbsd-1.0.0-beta2-rc2 zedbsd-1.0.0-beta2
refused "promotable rc1 (note changed)" tag promotable zedbsd-1.0.0-beta2-rc1 zedbsd-1.0.0-beta2
refused "promotable backwards" tag promotable zedbsd-1.0.0-beta2 zedbsd-1.0.0-beta2-rc1

# 2. The configuration's answers and the locked root.
info=$(timeout 120 make -s ZEDBSD_CONFIG=config/release/config-amd64-beta2.mk release-info)
ok "release-info" sh -c "printf '%s\n' '$info' | grep -qx 'name=1.0.0 Beta 2' && printf '%s\n' '$info' | grep -qx 'zip=n'"
timeout 120 make -s ZEDBSD_CONFIG=config/release/config-amd64-beta2.mk BUILD="$build/image" "$build/image/gen/shadow"
ok "root locked" grep -q '^root:\*:' "$build/image/gen/shadow"
sed 1d userland/base/etc/shadow > "$build/shadow.tail"
ok "other lines kept" sh -c "sed 1d '$build/image/gen/shadow' | cmp -s - '$build/shadow.tail'"

# 3. The workflow's step over stand-in assets.
python3 - "$top/.github/workflows/release.yml" "$build/step.sh" <<'EOF'
import sys, yaml
workflow = yaml.safe_load(open(sys.argv[1]))
for step in workflow["jobs"]["build"]["steps"]:
	if step.get("name") == "Check the sizes, sum the assets and take the notes":
		open(sys.argv[2], "w").write("set -e\n" + step["run"])
		break
else:
	sys.exit("no such step")
EOF
mkdir -p "$build/run/artifacts" "$build/run/docs/release"
printf 'image\n' > "$build/run/artifacts/zedbsd-1.0.0-beta2-amd64.img.gz"
printf 'zip\n' > "$build/run/artifacts/zedbsd-1.0.0-beta2-windows.zip"
refused "step without notes" sh -c "cd '$build/run' && VERSION=1.0.0-beta2 sh '$build/step.sh'"
printf 'notes\n' > "$build/run/docs/release/zedbsd-1.0.0-beta2.md"
ok "step" sh -c "cd '$build/run' && VERSION=1.0.0-beta2 sh '$build/step.sh'"
ok "SHA256SUMS" sh -c "cd '$build/run/artifacts' && sha256sum -c SHA256SUMS && test \$(wc -l < SHA256SUMS) = 2"
ok "notes taken" cmp -s "$build/run/docs/release/zedbsd-1.0.0-beta2.md" "$build/run/artifacts/notes.md"

echo "host-release: $passed passed, $failed failed"
[ "$failed" = 0 ]
