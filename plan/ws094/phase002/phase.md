<!-- awesome-plan project=zedbsd record=ws094-p002 -->

# ws094-p002: compositor の desktop surface と probe

Status: cleared（2026-09-30）
Disposition: normal
Parent: [WS094](../ws.md)
Queue: main の依頼（2026-09-29、worktree `.claude/worktrees/ws094-desktop`、branch `wt/ws094`、`git merge main -m WIP` の後）。
Approval: main の許可: `userland/desktop/wayland/` を変える（新しい protocol と desktop の層は新しい file に、既存の file への差し込みは最小に）。

## 範囲と受け入れ

[design.md](../design.md) §3: `keiland_desktop_v1`（role・token・configure）、重ね順（壁紙の上・窓の下・全ての仮想 desktop）、合成（alpha・App Home の層・
backdrop）、入力（窓の無い所の press・右 click・key・touch・DnD の対象、compositor の gesture が先）、focus、popup（context menu）の親、起動と起こし直し、
log。試験用の probe。受け入れ: probe で上を QEMU の Venus で確かめ、WS035 の回帰の該当が変わらず、build の warning 0、boot test。

## 変えたこと

新しい file:
- `userland/desktop/wayland/desktop.c`・`desktop.h`: protocol（`keiland_desktop_manager_v1.get_desktop_surface(id, surface, token)`、
  `keiland_desktop_surface_v1.ack_configure(serial)`・`destroy`、event `configure(serial, x, y, width, height)`、error `role`(0)・`token`(1)）、
  描画（`zwl_desktop_draw`: glass の look は `glass_shape` の MODE_IMAGE で App Home の層の変換を受ける、plain の look は alpha の quad）、入力
  （`zwl_desktop_press`・`zwl_desktop_unfocus`・`zwl_desktop_front`・`zwl_desktop_at`）、起動（`--session` か `--desktop-client=COMMAND` の時に
  `KEILAND_DESKTOP_TOKEN=<32 桁の hex> exec COMMAND` を `zwl_spawn`、終われば 2 秒後に起こし直し、1 分に 4 回まで、`/etc/keiland/desktop` が `off`
  なら起こさない、`--desktop-client=none`・`--desktop-token=TOKEN` は試験用）、frame callback（描かない frame でも答える）。
  desktop surface は role を持たない surface として commit を受け（cursor と同じ経路）、窓の一覧・Wiseview・システムバー・focus の巡回には入らない。

既存の file への差し込み（呼び出しと分岐だけ）:

| file | 差し込み |
| --- | --- |
| `zwl.h` | object の種類 `ZWL_DESKTOP_MANAGER`・`ZWL_DESKTOP_SURFACE`（2 行） |
| `protocol.c` | global 21 `keiland_desktop_manager_v1`（20 は WS095 の IME）、request の振り分け（case 2 つ） |
| `objects.c` | surface・desktop surface が消える時の `zwl_desktop_object_gone` |
| `display.c` | pass ごとの `zwl_desktop_tick`、focus を `zwl_desktop_front(server, top)` に |
| `shell.c` | 壁紙の後・窓の前の `zwl_desktop_draw`（Wiseview の間は描かない）、backdrop の壁紙の後にも、`zwl_glass_button` で窓の無い所の press を `zwl_desktop_press` に、窓の press で `zwl_desktop_unfocus` |
| `compose.c`・`compose.h` | plain の look の描画、frame が desktop の buffer と callback を持つ（held の数 +1） |
| `data.c` | DnD の対象: 窓の本体が無ければ `zwl_desktop_at` |
| `touch.c` | 指の下の surface: 窓が無ければ `zwl_desktop_at` |
| `menu.c` | context menu（`keiland_menu_popup`）を desktop surface にも許す |
| `main.c` | option の `zwl_desktop_option`、usage |
| `Makefile` | `desktop.c` |

試験（`plan/ws094/tests/`）: `desktop-probe.c`（wl_shm で右端に 3 つ・左上に 1 つの四角を描き、pointer・key・touch・DnD を log する client、
token は環境から**複写してから** `unsetenv`）、`build-probe.sh`（build の clang と sysroot で probe を作る）、`desktop-guest.sh`（手順
install・role・refuse・input・home・home-drag・dnd・touch・restart）。

## 確認

| 確認 | 命令 | 結果 |
| --- | --- | --- |
| build | `make ZEDBSD_CONFIG=config/ci/config-amd64.mk BUILD=build/ws094-amd64 build/ws094-amd64/bin/wayland`（と libwayland-client・extras-probe・files） | rc 0、warning 0 |
| style | `python3 plan/tools/style-check.py desktop.c desktop-probe.c`、`style-extra.py`、`git diff --check` | 0 件（style-extra の残りは probe の interface の表の初期化子） |
| guest（QEMU、Venus） | main の `build/ws035-sq/hdd-image.img` の複写で `sh plan/ws094/tests/desktop-guest.sh build/ws094-shots/p002 install role refuse input home-drag dnd restart` | PASS（28 件）: role（x=0 y=34 1280×766、ack、commit、frame）、2 つ目の surface と違う token の拒否（`reason=role`・`reason=token`）、窓の無い所の左・右 click と key が probe に届き（focus in、surface の座標）、Files の窓が map すると keyboard が窓へ（`unfocus via=window`）、窓の横の press で desktop へ、窓の press で窓へ（`unfocus via=press`）、左上からの drag で desktop が App Home の層と一緒に縮んで動く（`home-drag.png`）、Files の窓から desktop へ drag すると probe が `dnd enter`、`--desktop-client` の起動・終了・起こし直しと 4 回の上限。画面 `build/ws094-shots/p002/`（role・input・window・home-drag・dnd） |
| guest（注入の touch） | main の `build/main-pen/hdd-image.img` の複写で `desktop-guest.sh … install role touch` | PASS: 窓の無い所の tap が wl_touch で probe に届き（`touch down x=500 y=466`）、desktop が keyboard を得る |
| WS035 の回帰 | `plan/ws035/tests/zdesktop-p052.sh`（2 つの mode）・`p053`（wl_shm と cursor）・`p057`（backdrop） | PASS |
| WS035 の回帰 | `zdesktop-p072.sh`（最小化・Wiseview・仮想 desktop） | FAIL: 最初の client（wlshm a）が接続で `setup errno=5` になり b の窓が無い。**main の compositor に入れ替えても同じく FAIL**（既存。WS094 の変更と無関係） |
| boot | `plan/tools/boot-test.sh build/ws094-run/disk.img`（この compositor と library を入れた disk） | PASS（`build/ws094-shots/p002/boot-login.png`） |

| main の取り込みの後（WS095 の IME と同じ file の衝突を解決: zwl.h の種類・Makefile・protocol.c の global は両方を残し、desktop は 21 に） | build し直して `desktop-guest.sh … install role refuse input home-drag dnd restart`、WS035 の `p052`・`p053`・`p057`、boot test | desktop PASS、p052・p053・p057 PASS、boot PASS、build の warning 0 |

- 判定は zdesktop と probe の log（SSH）と画面。console・serial は読んでいない。

## 見つけたこと・制限

- **Files（p003）への注意**: `getenv` の文字列は `unsetenv` の後に使えない（zedBSD の libc が消す）。probe は最初この誤りで token を失った。Files も
  token を複写してから環境から消す。
- Wiseview の間は desktop を描かない（design §3 を直した）。一番下の窓の glass は desktop の icon を映さない（backdrop は他の窓の上の窓にだけ作られる。
  design §3 に書いた）。
- 起動の既定（main の判断、2026-09-30）: `--session` の compositor は `/etc/keiland/desktop` の最初の語が `on` の時だけ `/bin/files --desktop` を起こす
  （Files の `--desktop` は p003 から。p003 で既定を「起こす」に戻す）。`--desktop-client=` を明示した compositor は常に起こす。
- WS035 からの引き継ぎ（ws035-p137）: `zwl_top_window()` の候補から desktop の surface を明示で外した（`zwl_desktop_is`）。desktop の surface は
  もともと `mapped` にならず候補に入らなかったが、暗黙に頼らない。試験 input に「最後の窓を閉じても desktop に focus が移らない」を足した。
- `plan/ws035/tests/zdesktop-p072.sh` の失敗は BUG-115（WS035 の担当、main が起票）。WS094 の回帰の対象から外す。
- 観察（1 回、再現せず）: 全ての手順を続けて流した 3 回のうち 1 回、restart の手順で compositor が起こした probe が role を取った後 45 秒の間に
  終わらず、起こし直しが起きなかった。その後の単独の実行と全手順の実行では再現しなかった。compositor は role・ack を正しく扱っており、probe の側
  （timeout の loop）で止まった可能性が高いが、原因は未確認。
- 実機は未実施。`--session` での起動（sessiond の下）は未実施（`--desktop-client` で同じ起動の経路を確かめた）。

## Resume point

p003（libkeiland の client の API と Files の `--desktop` の骨組み）から。
