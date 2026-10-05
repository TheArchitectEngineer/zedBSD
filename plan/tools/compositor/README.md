# compositor の role の試験（WS110 から移した）

compositor（`userland/desktop/wayland/`）の起動の role（`role.c`）を確かめる。引数なしと `--session` は通常の session（期限なし）、`--testing` は有限の試験（既定 150 秒、`--timeout`・`--max-frames`）、`--greeter` は login 画面。`--timeout`・`--max-frames` は `--testing` が要り、矛盾する組み合わせは exit 2。

| file | 役割 |
| --- | --- |
| `run-host-role.sh` | host で `host-role.c` と `role.c` を build して 16 case を流す。最後の行 `host-role: PASS` |
| `roles-guest.sh [OUTDIR]` | Venus の guest（files の image）で READY の行の `role=`・`timeout_ms=`、`--testing --timeout=20` が自分で終わること、拒む 4 通りを確かめる。最後の行 `roles-guest: PASS` |

compositor を起動する試験を書く時は、有限に終わらせるなら `--testing --timeout=N` を付ける（付けないと通常の session として期限なしで動く）。
