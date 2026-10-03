#!/bin/sh
# Runs 10 boot-only i915 passthrough attempts and tallies the bring-up outcome.
set -u
cd "$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)"
pass=0
fail=0
summary=plan/ws029/phase007/boot-campaign.txt
: > "$summary"
echo "i915 cold-attach bring-up campaign ($(date -u +%Y-%m-%dT%H:%M:%SZ))" >> "$summary"
i=1
while [ "$i" -le 10 ]; do
    name=$(printf 'q314-i915-camp-%02d' "$i")
    if [ "$i" -eq 1 ]; then
        build=""
    else
        build="--skip-build"
    fi
    python3 plan/ws029/tests/run-i915-remote.py --attempt "$name" --boot-only $build >/dev/null 2>&1
    result=plan/ws029/temp/remote/$name/result.json
    line=$(python3 - "$result" <<'PY'
import json, sys
try:
    r = json.load(open(sys.argv[1]))
except Exception as error:
    print("status=missing error=%s" % error); raise SystemExit
rr = r.get("remote_result") or {}
i = rr.get("i915") or {}
st = i.get("attach_stopped")
sel = i.get("selftest") or {}
print("status=%s remote=%s host_restored=%s attach_stopped=%s selftest=%s registered=%s" % (
    r.get("status"), rr.get("status"), r.get("host_restored"),
    (st.get("stage") if st else None), sel.get("store"), i.get("registered")))
PY
)
    echo "$name: $line" | tee -a "$summary"
    case $line in
        status=pass*|status=boot-pass*) pass=$((pass + 1)) ;;
        *) fail=$((fail + 1)) ;;
    esac
    i=$((i + 1))
done
echo "---" | tee -a "$summary"
echo "pass=$pass fail=$fail of 10" | tee -a "$summary"
