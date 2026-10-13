<!-- awesome-plan project=zedbsd record=ws217-p002a -->

# ws217-p002a: compositor の中核、turn 無し（論理と物理の分離、回転の値、open の組の比較）

Parent: [WS217](../ws.md)
Status: in-progress（2026-10-13 P4 実装済み、agent/p4 `b58ff4b06`。host 試験と 3 つの build は PASS。T1 の 0° の回帰の試験待ち（依頼は下の「T1 への依頼」、Q1 経由、番号は未割当））
Disposition: normal
Primary Milestone: MG006（WS から継承）
Queue / attempts: Q1 の指示（2026-10-13、P4「WS217 の p002a を実行する」。設計は p001 第 4 版、review-3 の判定で開始可）。同日 Q1 の追加の指示（review-4 `315105fed` の条件: C7・L49・S16 の mode の引数（推奨）・L44 を入れてから実装）。
Purpose / goal: p001 第 4 版 §13 の p002a の行。turn pass を作らずに、回転のための状態の分け方（物理の mode と論理の大きさ、選んだ回転と実効の回転）、回転の値の保存と読み、open の全経路の（論理の大きさ, 回転）の組の比較を入れる。この段では 0 以外の回転は全部 UNSUPPORTED で、**0° の今の動きは変わらない**ことが最重要。
Prerequisites: p001 第 4 版（review-3 の判定で p002a は開始可）、review-4 の C7・L49 の直し（この phase.md に入れた）、WS213 p004 の scoped な出力（main 済み）。

## 範囲（第 4 版 §13 の p002a の行と Q1 の指示）

入れた物:

1. `rotate.c`／`rotate.h`（新）: §2 の写像（点・逆・矩形・逆・24.8 の固定小数と逆・vector）、角度の検査と bit、`kwl_rotate_mask`（mask と why）、`kwl_rotate_plan`、`kwl_rotate_size`・`kwl_rotate_swaps`・`kwl_rotate_steps`（下の「設計との違い」の 1）、`struct kwl_rotate_state`（論理の大きさ・選択・実効・scanout・turn。review-4 L49 で選択の値を持つ）、`KWL_ROTATE_MODE_USER/OPEN/SESSION_PAUSE/SESSION_RESUME`（review-4 S16 の推奨、L49）。
2. 物理と論理の分離（M1・C2）: `compose->mode_width/mode_height`、`server->size_width/size_height`（既定 320x240、`--width/--height` が書く）、`server->width/height` を書くのは `kwl_output_size_set()`（output-switch.c）の 2 行だけ。mode を読む所（`vkdemo_display_open_on`・`compose_refresh`・game mode の `kl_backend_scanout_open`）は `compose->mode_*` に。
3. 回転の値: `compose->rotation_selected`・`rotation`・`scanout_rotation`・`turn_rotation`・`hw180`（まだ読まない、p010）・`rotations`・`rotate_why`。
4. 回転の選び方 `compose_rotation_choose()`（compose.c）: `--rotate=N`（試験、config より優先）か displays.conf の `rotate=KEY N`、login 画面は常に 0（D9）、mask で検査し、mask に無い角度は 0 で開いて `rotate=` の行は保持し `KWL OUTPUT rotate unsupported key=… rotation=… reason=mask why=…` を出す。呼ぶのは `compose_display`（起動時、glass・scene・layers の前、config はここで読む: `kwl_heads_config_load` を早めた）と `kwl_compose_output_prepare`（open の全経路の最初、surface を作る前。review-4 S13 の (2) の「plan は prepare の前」にも合う）。この段の mask は常に `KWL_ROTATION_0` だけ（`hw180 = 0`、`KWL_TURN_BUILD = 0`）なので、**0 以外は全部 UNSUPPORTED**（L20）。
5. open の組の比較（C5・review-4 C7）: compose が「最後に開いた・回した状態」`compose->opened`（`struct kwl_rotate_state`）を持ち、`kwl_compose_output_open` の最後（自分の park の後）に `kwl_output_rotated()` が `kwl_rotate_steps(&compose->opened, &now, KWL_ROTATE_MODE_OPEN)` で比べて書き換える。RESIZED は `output_resized(server, 1)` と `kwl_outputs_changed`、ROTATED は全体の描き直しと `KWL OUTPUT rotate … steps=… by=open` の log（入力の cancel・geometry・同じ物理の点は p003、Settings への tell は p004）。REOPEN・TURN は OPEN の mode では立たない（L49）。最初の値は `compose_display` が glass・scene の作られる状態で書く。output switch の `width != old_width` の比較はこれに置き換わり、switch は open の前に新しい display の mode を 0° で `kwl_output_size_set` し（回転は open の choose が決める）、失敗の戻しは古い mode と `compose->opened` の回転で戻す（open が成功していないので `opened` は古いまま、戻した open の比較は何もしない）。switch は `kwl_outputs_changed` を常に 1 回送る（L44: open が RESIZED で送った時は重ねない）。
6. `output_resized(server, glass_rebuild)` の引数（M2）。今の呼び手は 1 だけ。
7. displays.conf の `rotate=KEY N`（§7.4、L36: N は最後の語、KEY は空白を含んでよい、0 は書かない、90/180/270 以外・数でない・長すぎる key・17 個目は捨てて数える）、`kwl_displays_rotation`・`kwl_displays_set_rotation`・`kwl_displays_parse_rotation`、書く順は `off` の後。読みの log `KWL DISPLAYS config rotations=N dropped=D`（行がある時だけ）。
8. wl_output は今のまま（transform normal、mode は論理、案 E）。`struct kwl_output_view` に `rotation`・`mode_width`・`mode_height`（head は p005 まで 0 と論理の大きさ）。
9. 直の scanout の fact `scanout_rotation`・`turn_rotation` と `KWL_SCANOUT_ROTATED 10`（gpu_buffer の後、size の前、log の語 `rotated`）。
10. cursor の plane: `rotation != 0` で `plane_state()` が −1（合成）。L24: 空の像の publish を C の park の解除の前に。
11. `--rotate=0|90|180|270`（usage にも）。
12. `SHOT` の答え `OK W H F rotation=R scanout=S turn=T`、`DISPLAYS` の output の行の末尾に ` rotation=R scanout=S turn=T`（head の行は不変）。
13. `pass_load` の寿命（L26）: 作るのは `kwl_compose_load_pass` のまま、壊すのを `kwl_backdrop_destroy` から compositor の終わり（`compose->pass` の隣）へ。
14. M11 の helper: 描く先の論理の大きさ `compose->target_width/target_height`（`compose_targets` が swapchain の大きさで書く）と `kwl_compose_target_pass(compose, slot, load)`。§1.8 の「論理」の 7 行と pass の 4 か所をこれに置き換えた（下の表）。
15. M12: glass の `wallpaper_create`・`blur_create`・`wallpaper_draw`（と `wallpaper_fill`）が幅・高さを引数で取る。今の呼び手は `server->width/height` を渡す（0° では同じ値）。

触らなかった物（後の段）: turn pass と turn の画像・`turn.frag`・`--rotate-policy=any`・`kwl_output_rotate` の経路・入れ替えの前に届いた壁紙の loader の結果の捨て（p002b）、scanout の回転・`hw180` の読み（p009・p010）、入力（p003）、protocol の版 34・Settings（p004）、head の回転（p005）、結び付け（p006）、game の session（p012）。

## 設計との違い（実装の判断、Q1 の確認を求める）

1. `kwl_rotate_size`・`kwl_rotate_swaps`・`kwl_rotate_steps` は rotate.c でなく rotate.h の `static __inline` にした。output-switch.c が使う。output-switch.c を単独で build する host 試験（`plan/ws113/tests/host-output-switch.sh`）に rotate.c の link を足さずに済む。同じ理由で displays.c は角度の検査を自分で持つ（`displays_angle`、`plan/ws113/tests/host-displays.sh`・`host-arrange.sh` は displays.c だけを link する）。
2. `compose_target_size()` は関数でなく `compose->target_width/target_height` の field にした（`compose_targets` が書く、p002b は turn の画像の大きさを書く）。layer-cache.c を単独で build する `plan/ws211/tests/anim-host-test.sh` が compose.c の関数を link せずに済む。`compose_target_layout`・`compose_target_image`（barrier と写しの元）は turn の画像ができる p002b で入れる（この段では返す値が今と同じで、置き換えても何も確かめられない）。
3. `kwl_rotate_plan(rotation, mask, hw180, &scanout, &turn)` は policy・turn_allowed でなく mask を取る（mask が policy を含む）。戻り値は 0・EINVAL（角度でない）・ENOTSUP（mask に無い）。
4. 第 4 版 §4.10 の mask の式は「policy ANY なら turn_allowed = turn_build」で、§4.9 の「rpi4（`KWL_TURN_DISABLED`）でも測る時だけ `--rotate-policy=any` で許す」と食い違う（rpi4 の turn_build は 0 なので ANY でも 90 が出ない）。実装は §4.10 の式どおり。p002b で「turn の code がある（build）」と「rpi4 で既定で止める」を別の入力に分ける必要があり、第 5 版で決めてほしい。
5. `HEADS_TEXT_MAX` は 4096 のまま。`rotate=` を足した最も長い displays.conf（16 の place・off・rotate、最長の key）は 3.8 KB で収まる（host 試験で確かめた）。8192 は `touch=` を足す p006 で要る。
6. `kwl_rotate_to_wayland()` は入れていない（案 E では呼ばない。Wayland の transform は反時計回りで、使う時に向きを確かめて入れる方がよい）。
7. login 画面（`--greeter`）は config・`--rotate` に関わらず 0（D9 を明示に）。
8. `KWL OUTPUT rotate` の log の末尾は `persist=` でなく `by=open`（mode の名前）。p002b の `kwl_output_select_rotation` 系（review-4 S12）で `by=user` などを足す。

## M11 の grep の表（main からの差分の後、agent/p4 `b58ff4b06`）

`grep -n 'server->width =\|server->height =\|server\.width =\|server\.height =' userland/desktop/wayland/*.c`:

| 所在 | 意味 |
| --- | --- |
| `output-switch.c:315-316` | `kwl_output_size_set()` の中の 2 行だけ（受け入れどおり） |

`grep -n 'compose->output\.width\|compose->output\.height' userland/desktop/wayland/*.c`（物理のまま残る行だけ）:

| 所在 | 意味 |
| --- | --- |
| `cursor-plane.c:522-523` | cursor の plane の mode の照合（物理） |
| `compose.c:2115-2116` | `compose_targets` が描く先の大きさ（`target_*`）を swapchain の大きさで書く（p002b で turn の大きさに） |
| `compose.c:2138-2139` | swapchain の framebuffer の作成（物理） |
| `shot.c:436, 441-442` | shot の buffer（完成した swapchain の画像、物理） |

`target_*` に置き換えた §1.8 の「論理」の行: scene の作成（`compose.c` の `kwl_scene_open`）、damage の clamp と 70%（`compose_region`）、frame の renderArea・viewport・scissor（`compose_record`）、layers（`layer-cache.c:101-102`）、backdrop の target と viewport（`backdrop.c:71, 132`）、mirror の fit と blit の元（`heads.c` の `heads_record_mirror`）。

`grep -n 'compose->pass\b\|pass_load' userland/desktop/wayland/*.c`（helper の外に残る行）: `compose->pass` の作成（`compose.c:1862`）と破棄（`compose.c:934`）、pipeline と framebuffer の作成の render pass の互換（`compose.c:2095, 2135`、`heads.c:989`）、prepare の一度だけの作成の判定（`compose.c:333`）、`pass_load` の作成（`backdrop.c:316, 355`）と破棄（`compose.c:935-937`）。frame の pass を選ぶ 4 か所（`compose_record` の 3 つ、`backdrop.c` の取り直し、`heads.c` の extended）は `kwl_compose_target_pass()`。

## 0° で動きが変わらないことの見直し

- 起動: `compose_display` の mode は今の `server->width/height` の値と同じ（native、または `size_*` = 今の既定と `--width/--height`）。choose は 0 を選び `kwl_output_size_set(mode, 0)` が同じ値を書く。main の `kwl_output_size_set(size, 0)` は compose の前の 320x240（または `--width/--height`）を今どおり置く（cursor の箱の初期化が読む）。`kwl_heads_config_load` は parse と `display_mode` だけで、早めても heads が無い間は `kwl_outputs`・`kwl_output_view` の結果が同じ。boot light は `frame != 0` を待つので順は変わらない。
- open: 最初の open（`enter_window_mode`）と game mode の後の open は `opened` と同じ状態なので何もしない。switch と hotplug は今と同じ時（大きさが違う時）に `output_resized` が走り、`kwl_outputs_changed` は今と同じく 1 回。WS113 の host-output-switch を（下の stub を足した写しで）新旧の output-switch.c で流し、log が一致した。
- 描画: `target_*` は `compose_targets` で swapchain の大きさ（今読んでいた値）。`kwl_compose_target_pass` は今の `compose->pass`／`pass_load` を返す。`pass_load` は output の close で壊さなくなっただけ（format は一生変わらない、次の open は作り直さずに使う）。
- glass: 呼び手が今と同じ `server->width/height` を渡す。
- cursor の plane: `rotation` が 0 なら今どおり。L24 は「合成の cursor」の時の publish を park の解除の前に移しただけ（解除の直後に古い像を 1 回出す窓が無くなる）。
- game mode: 回転 0 では ROTATED は返らない（host 試験）。claim の大きさは mode（= 今の値）。
- log・試験の channel: 新しい行は rotate の行が config にある時だけ。`DISPLAYS` の output の行と `SHOT` の `OK` の行の末尾に field が増える（`keiland-shot` は `sscanf("OK %u %u %d")` で 3 つを読むので影響無し、WS113 の displays-p004b.sh は head の行だけを `$` まで照合する）。

## 確かめ（実行した物）

| コマンド | 結果 |
| --- | --- |
| `sh plan/ws217/tests/host-rotate.sh` | PASS（plain・ASan/UBSan）: 4 つの panel（5x3・3x5・1x1・8x2）× 4 つの角度の全画素の写像・逆・全単射、固定小数の 7 sub-pixel ごとの一致と往復、5x3 の全矩形の面積・往復・中身、vector、mask と why の 8 通り、plan、**steps の 16 の組**（第 4 版 §4.6 の表）・USER の SAVE・OPEN の mode（REOPEN・TURN を落とす、C7 の大きさ違いの switch、90→270 の同じ mode）・game の pause と end |
| `sh plan/ws217/tests/host-displays-conf.sh` | PASS: 空白を含む key、CRLF、同じ key の 2 行、捨てる 11 通りと数、変更・0 で消す・16 個の上限・順、`off` の後に書く、往復、rotate の無い file は今と同じ文、最も長い file が 4096 に収まる |
| `sh plan/ws217/tests/host-scanout-rules.sh` | PASS: 回転 0 の規則の順（今のまま）、180（scanout）・90・270・180（turn）が gpu_buffer の後・size の前で `ROTATED`、語 `rotated` |
| `sh plan/ws113/tests/host-displays.sh`、`host-arrange.sh` | PASS（回帰） |
| `sh plan/ws213/tests/cursor-due-host-test.sh build/ws217-p002a/cursor-host` | PASS（plain・ThreadSanitizer、回帰） |
| `sh plan/ws141/tests/vulkan-cursor-host.sh` | PASS（回帰） |
| `plan/ws113/tests/host-output-switch.sh` | **main で既に link が壊れている**（`kwl_outputs`・`kwl_pointer_warp` の stub が無い、ws213-p004 の後）。下の「他の WS の試験」の写しで新旧を比べた |
| `plan/ws211/tests/anim-host-test.sh` | この変更で FAIL（試験の compose が `output.width` だけを書き、`target_width` が 0）。下の 2 行を足した写しで PASS |
| `make -j16 ZEDBSD_CONFIG=config/ci/config-amd64.mk BUILD=build/ws217-p002a/amd64 build/ws217-p002a/amd64/bin/wayland` | exit 0、warning 0 |
| `make -j16 ZEDBSD_CONFIG=config/ci/config-rpi4.mk BUILD=build/ws217-p002a/arm64 build/ws217-p002a/arm64/bin/wayland` | exit 0、warning 0 |
| `make -j16 KEILAND_ASK=n KEILAND_LINUX_BUILD=build/ws217-p002a/linux keiland-linux` | exit 0、warning 0（install はしていない） |
| `sh plan/tools/keiland-os-boundary/check.sh` | PASS |
| `python3 -I plan/tools/style-check.py`（新しい file と試験、変えた 12 file） | 新しい file・試験は 0 件。変えた file は main の時と同じ 29 件（増えた 2 件は直した） |
| `git diff --check` | 指摘無し |

未実施: FreeBSD の build（`Makefile.freebsd` に rotate.c を足しただけ、T1 の guest の `plan/tools/keiland-freebsd/backend-test.sh`）、QEMU・実機（T1 へ）。

## 他の WS の試験（Q1 に直しを頼む、WS113・WS211 の path なので P4 は変えていない）

1. `plan/ws113/tests/host-output-switch.c`（main で既に link が壊れている）: (a) `kwl_outputs`（anchor に `server->width/height`）と `kwl_pointer_warp`（anchor の move を呼ぶ）の stub を足す、(b) stub の `kwl_compose_output_open` の成功の所で `(void)kwl_output_rotated(server);` を呼ぶ（本物の open が最後に比べるので、C7）。この 2 つを足した写し（`build/tmp/ws217-osw.64Lp8B/host-output-switch-c7.c`、stub は scratchpad の `output-switch-stubs.c` の内容）を新しい output-switch.c で ASan/UBSan 付きで流して PASS、log は main の output-switch.c（同じ stub）と一字一句同じ。
2. `plan/ws211/tests/anim-host-test.c:529` の後に `compose->target_width = 1000U;`・`compose->target_height = 500U;` の 2 行（写し `build/tmp/ws217-anim.HbywjU/anim-host-test.c` で PASS）。

## T1 への依頼（Q1 経由、0° の回帰、p002a 単独の束）

- image: agent/p4 `b58ff4b06`（Q1 の merge の後なら main）の tree で、(A) CI の image（`config/ci/config-amd64.mk`）、(B) AAT の image（`plan/tools/aat/build-image.sh build/<T1>-aat`）、(C) WS113 の 2 画面の image（`plan/ws113/tests/config-amd64-p004b.mk`）。過去の build を入力にしない。
- 試験と合否:
  1. (A) `plan/tools/boot-test.sh` → `boot-test: PASS` と login の PNG。
  2. (B) `plan/tools/aat/run-aat.sh qemu OUTDIR smoke` → summary.md の全部が PASS（not-run・needs-person は除く）。desktop の 0° の見た目と操作が今と同じ。
  3. (C) `VENUS_DISPLAY=dbus VENUS_OUTPUTS=2 plan/tools/files/files-guest.sh start IMAGE` で `plan/ws113/tests/output-switch-p004a.sh OUTDIR` → 最後の行 `output-switch-p004a: PASS`（**C7 の回帰**: head 0 を抜くと `KWL OUTPUT switch … width=1024 height=768` と `KWL OUTPUT resized width=1024 height=768`、戻すと 1280x800）。同じ image で `plan/ws113/tests/displays-p004b.sh OUTDIR` → `displays-p004b: PASS`。
  4. (C) の続き（手で、SSH）: guest の `$HOME/.config/keiland/displays.conf` に `plan/ws217/tests/displays-rotate.conf` を写して compositor を起こし直す（logout/login か `--testing` の起動し直し）→ log に `KWL DISPLAYS config rotations=1 dropped=0` と `KWL OUTPUT rotate unsupported key=Venus virtual display 0 rotation=90 reason=mask why=1`、`KWL OUTPUT open width=1280 height=800`（0° のまま）、`keiland-shot --request DISPLAYS` の output の行が `… rotation=0 scanout=0 turn=0`。`MODE mirror` を当てた後も displays.conf に `rotate=Venus virtual display 0 90` が残る。
- 返し先: 結果（PASS/FAIL、PNG、log の場所）を Q1 へ。FAIL の解析は P4 か bug-analyzer。

## 結果と残り

- 実装は commit 済み（`b58ff4b06`、変えた path は下）。Phase の clearance は T1 の結果を Q1 が判定してから。
- 変えた path: `userland/desktop/wayland/{rotate.c,rotate.h（新）,compose.c,compose.h,kwl.h,output-switch.c,displays.c,displays.h,heads.c,glass.c,main.c,scanout.c,scanout-rules.c,scanout-rules.h,cursor-plane.c,shot.c,backdrop.c,layer-cache.c,Makefile,Makefile.linux,Makefile.freebsd}`、`plan/ws217/tests/{host-rotate.c,host-rotate.sh,host-displays-conf.c,host-displays-conf.sh,host-scanout-rules.c,host-scanout-rules.sh,displays-rotate.conf}`、この phase.md と ws.md の行。
- p002b へ渡す物: `KWL_TURN_BUILD` を 1 に（rpi4 の止め方は上の 4 の決め）、`target_*` を turn の大きさに、`compose_target_layout/image`、`kwl_output_rotate` を review-4 S12 の 2 つの関数で、入れ替えの前の loader の結果の捨て、ROTATED の入力の手順（p003）と tell（p004）は `kwl_output_rotated` の枠に足す。
- 試験の登録: host-rotate・host-displays-conf・host-scanout-rules は WS217 の回帰として WS の完了時に master.md の Tools へ移す候補（Q1 の判断）。

## Event

- 2026-10-13 / ws217-p002a-impl-1: P4 が実装（agent/p4 `b58ff4b06`）。review-4 の C7（`compose->opened` と open の最後の比較）、L49（state に選択、OPEN の mode で REOPEN・TURN を落とす、open は自分で park）、S16 の mode の引数、L44（switch は常に 1 回 tell）を入れた。build 3 つと host 試験 PASS。T1 の試験待ち。
