<!-- awesome-plan project=zedbsd record=ws213-p003 -->

# ws213-p003: S1: 描く物の一覧（display list）を同じ thread で再生する

Parent: [WS213](../ws.md)
Status: in-progress（実装は agent/p4 db29b07f5 まで、host 試験 PASS・build warning 0。T1（i915・Venus）と Pi の確かめ待ち）
Phase disposition: normal
Queue: P4（2026-10-12 Q1「p003 に着手してよい」）

## 範囲

- `struct kwl_list` と op、描画の関数の `VkCommandBuffer command` の引数（37 file）を `struct kwl_list *` に。compose->command を直に使う所（`compose.c:2365〜`、`layer-cache.c:184`）も grep で全部挙げて一覧へ。
- batch（WS211 p003）の頂点を一覧に。bind の省略の鍵を（import、種類、世代）に、`struct kwl_import` に作成ごとの世代の番号。
- cursor の形の画像を起動時に host image の batch（`shm.c:354-392`）で全部作る（review-3 M-6）。その時間を `KWL STARTUP step=cursor-shapes ms=` で独立に出す（review-4 L-c）。
- walk と再生の時間を分けて `KWL PERF` に出す（walk の中央値・p90・最長、draw の数で 100 未満と 300 以上に分けて）。
- 再生は今の `compose_record` と同じ所・同じ thread。thread はまだ使わない。

設計は [p001](../phase001/phase.md)。

## 受け入れの条件

1. host: `plan/ws213/tests/list-replay-host-test.sh`（p003 で作る）で、p001 の 9 節 (a) の全場面の命令の列が直の記録と一致（descriptor set の handle は（import、種類）に正規化）。golden は p003 の最初の commit で取る。
2. build warning 0（下のコマンド）。
3. T1（i915 の 5330・Venus の QEMU）: shot.c の画が前と同じ、`KWL VULKAN_ERROR`・`KWL FAILED` なし。T1 の 1 回は 30 分まで。
4. Pi: `KWL STARTUP step=cursor-shapes` の ms が 50ms 以下（READY 全体の差は boot ごとの揺れと同じ大きさなので使わない、review-4 L-c）。walk と再生の時間を pointer だけ（10 秒）・App Home の開閉（5 回）・drag（10 秒）で測る（1 boot、ユーザーか Q1 の手で）。
5. **p004 に進む閾値**（記録する、満たさなくても p003 は cleared にできる。review-4 H-B で中央値から p90 と最長に）: Pi で walk が普通のコマ（100 draw 未満）の p90 8ms 以下かつ最長 16.7ms 以下、App Home のコマ（300 draw 以上）の p90 16.7ms 以下かつ最長 25ms 以下（p001 review-5 N-6 の Q1 の判断。33.3ms では p004 の `pass_max` ≤ 33.3ms に余裕が無い）。満たさなければ WS211 の削減を先にし、p004 はその後（p004 の依存）。

## コマンドと上限

- build（warning 0、出力は自分の `build/ws213-p003/` に、review-4 L-b）: `make -j16 ZEDBSD_CONFIG=config/ci/config-rpi4.mk BUILD=build/ws213-p003/arm64 build/ws213-p003/arm64/bin/wayland`、`make -j16 ZEDBSD_CONFIG=config/ci/config-amd64.mk BUILD=build/ws213-p003/amd64 build/ws213-p003/amd64/bin/wayland`、`make -j16 KEILAND_ASK=n KEILAND_LINUX_BUILD=build/ws213-p003/linux keiland-linux`（`KEILAND_ASK=n` で package の導入と install を聞かない、`Makefile:476-482`）（各 900 秒まで）。FreeBSD は T1 の guest（`plan/tools/keiland-freebsd/backend-test.sh`）。
- build の注記（p001 review-5 の答え (4)、Q1）: `BUILD=` の override は動き（`Makefile:79`）、libc.so なども自分の dir に作るので最初の build は長い（`vmunix.mk:411`）。LLVM と sysroot は共有のまま使う（wayland は `$(DYNAMIC_SYSROOT)/usr/lib/crt1.o` に依存、`userland-dynamic.mk:239`）。**make が共有の sysroot を作り直そうとしたら（sysroot の規則が動き出したら）止めて main に報告する**。toolchain と共有の `build/` は変えない。
- host 試験は各 120 秒まで。同じ条件で変更なしの再試行は 3 回まで。QEMU・実機は T1 と Q1 経由（担当は QEMU を起動しない）。

## 実装（2026-10-12 P4、agent/p4、main への merge は 1918feefb まで Q1 が済み、以後 261a6cf78・db29b07f5）

- **一覧**: `userland/desktop/wayland/draw-list.{c,h}` を新設。`struct kwl_list` は op の列（15 種: render pass の開始と終わり、viewport、scissor、pipeline、descriptor set、push constant、vertex buffer、draw、barrier、image の copy、buffer→image、image→buffer、blit、clear）、op の配列の data（clear 値・push の値・barrier・region は書いた時に写す、8 byte 揃え）、束の頂点、host image の copy の list、数（draw・束に入った quad・束の draw）、失敗の印を持つ。足りなければ倍に伸ばし、伸ばせなければ failed で再生しない（`KWL VULKAN_ERROR operation=record`）。`kwl_list_replay` が同じ vkCmd* を同じ引数・同じ順で呼ぶ。
- **描画の関数**: `VkCommandBuffer command` の引数（45 file、header を含む）を `struct kwl_list *command` に、vkCmd* を `kwl_list_*`（引数は同じ）に 1 対 1 で替えた。引数の名前 `command` は P3（WS211）と同じ file の差分を小さくするため保った。一覧の外で vkCmd* を呼ぶのは draw-list.c の再生、shm.c（起動時の layout の移しと host image の copy の記録）、import.c（import の即時の layout）、cursor-plane.c（自分の command buffer）だけ（host 試験の script が grep で確かめる）。
- **compose.c**: `compose_record` は `compose->list` に書き（`kwl_compose_list_begin` で空にし、bind を忘れ、束を始め、walk の時刻を取る）、終わりに書かれた host image を一覧へ移す（`kwl_host_images_take`、印を下ろす）。`compose_replay` が copy_command に host image の copy（`kwl_host_images_record(list, ...)`）、束の頂点を `batch.map` に写し、command に一覧を再生して End。`compose_submit`・`kwl_compose_submit_quiet` は記録をせず送るだけ。layer の静かな焼き（`kwl_layers_tick`）は `kwl_compose_list_begin` → `layers_record_pass` → `kwl_compose_submit_quiet`（take と再生を含む）。`kwl_layers_begin_quiet` は無くした。
- **束（batch.c）**: 頂点は一覧へ（`kwl_list_add_vertices`）、再生が mapped buffer の同じ offset に写す。
- **bind の鍵（設計からの違い、2026-10-12 Q1 の判断）**: 省略の鍵は VkDescriptorSet の handle のまま、`kwl_compose_set_put` が bind 中の set を返す時に忘れる今の規則を保つ。理由: 同じ recording（walk）の中の ABA は put の時に忘れる規則で防げている。bind の状態は walk の始めに毎回忘れる。p004 では set を返す（壊す）のは R の step 1 だけで walk の外。（import、種類、世代）の鍵は glass_shape の set の約 20 か所（shell.c の 8 か所を含む）に持ち主を持たせる必要があり、得る物が無い。
- **世代**: `struct kwl_import` に `generation`（`kwl_compose_generation`、1 から増えるだけ）。client の import（import.c）と host image（shm.c の image_create）で付ける。cursor の plane の `held[]` と最後に出した像（source）の比較に世代を足した（同じ address の新しい image で古い像を出さない）。
- **cursor の形の起動時の作成**（review-3 M-6）: `kwl_cursor_images_create`（cursor.c）が 10 枚を起動時の host image の batch（`kwl_compose_open` の arrow の後）で作る。時間は `KWL STARTUP step=cursor-shapes ms=` で独立に出す（review-4 L-c）。作れなかった形は今どおり最初に頼まれた時に作る（`KWL CURSOR images errno=`）。
- **walk と再生の時間**: `KWL PERF walk drawings= dropped= | small n= p50= p90= max= | middle ... | large ... | replay ... | quiet ... | quiet_replay ...`（5 秒の窓、ms）。walk は `kwl_compose_list_begin` から再生の始めまで（frame では host image の take を含む）、replay は 2 つの command buffer の Begin〜End と頂点の写し。frame は描いた物の数（一覧の draw − 束の draw ＋ 束に入った quad、`KWL DRAWS` の draws= と同じ数え方）で small（100 未満）・middle（100〜299）・large（300 以上、App Home）に分ける。quiet は layer の静かな焼き。1 窓に 1024 件まで（超えた数は dropped）。
- 注: `KWL LAYER baked ... us=` は今まで記録＋libvulkan の encode の時間だったが、今は一覧に書く時間（walk）だけになる。
- 変えた file: `userland/desktop/wayland/` の draw-list.c・draw-list.h（新規）、compose.c・compose.h・batch.c・compose-bind.c・layer-cache.c・shm.c・import.c・cursor.c・cursor-plane.c・main.c・kwl.h・extras.h・Makefile・Makefile.linux・Makefile.freebsd、描画の関数の型を替えた file（apps-bar.c ほか）。試験: `plan/ws213/tests/list-replay-host-test.{c,sh}`・`list-replay.golden`（新規）、`cursor-due-host-test.c`（直した、下）。WS211 の `plan/ws211/tests/{anim,batch,bind}-host-test.c` と batch・bind の `.sh` を型の変更に合わせた（Q1 の許可、別の commit 1918feefb）。

## 確かめ（2026-10-12 P4）

- 受け入れ 1（host）: `sh plan/ws213/tests/list-replay-host-test.sh` → PASS（799 行）。golden は最初の commit 0585fac33 で直の記録（vkCmd* を直に呼ぶ版）から取った。場面: plain の look（デスクトップ・窓 2 つと sub-surface・popup・cursor）、App Home（layer の pass、床の線 85 本を束で 62＋18＋5、tile と label、frame で layer の絵を描く）、App Home の束なし、drag の部分の描き直し（pass_load・damage の renderArea・cut の scissor と戻し）、backdrop（frame の pass の終わり、小さい pass、ぼかし 2 回、load の pass で再開）、heads 2 つ（mirror の barrier・clear・blit、extended の pass、present への barrier）、layer の静かな焼き、scene の復元と取り込み（scene.c）、shot の取り込み（memory・buffer・image の barrier）。descriptor set は（image、種類）の名前に正規化して比べる。一覧の伸び（1024 op・64KiB を超える、3000 push＋600 quad）で全部の op・値・頂点が残ることも見る。一覧の外の vkCmd* が無いことを grep で見る。
  - 制限: 場面は compose_record と callee の命令の並びを試験の中で組み立てた物で、batch.c・compose-bind.c・scene.c・draw-list.c は本物を通すが、compose.c・glass.c・shell.c・heads.c・backdrop.c・layer-cache.c の walk そのものは host で動かしていない（それらの変更は vkCmd*→kwl_list_* の 1 対 1 の置き換えで、同じ引数を写して同じ順に再生することをこの試験が op の種類ごとに確かめる）。実物の画面の一致は T1 の shot で確かめる（受け入れ 3）。
- `sh plan/ws213/tests/cursor-due-host-test.sh` → PASS。main の時点で既に FAIL だった（ws211-p006 が保留を 1 refresh − 2.5ms に変えた後、試験の due の値 12・7ms が古かった）ので 10・5ms に直した（p002 の記録の「12・7ms」は当時の値）。
- WS211 の host 試験 anim・batch・bind → PASS。
- 受け入れ 2（build、warning 0）: rpi4 `make -j16 ZEDBSD_CONFIG=config/ci/config-rpi4.mk build/arm64/bin/wayland` rc 0・warning 0、amd64 `make -j16 ZEDBSD_CONFIG=config/ci/config-amd64.mk build/amd64/bin/wayland` rc 0・warning 0、`make -j16 KEILAND_ASK=n KEILAND_LINUX_BUILD=build/keiland-linux keiland-linux` rc 0・warning 0（db29b07f5）。`BUILD=build/ws213-p003/...` ではなく、自分の worktree の既存の `build/arm64`・`build/amd64`（p002 で作った自分の物）を使った（libc を作り直す時間を省くため）。sysroot の規則は動いていない。FreeBSD の build は未実施（T1 の guest）。`git diff --check` 空。`plan/tools/style-check.py` で draw-list.c・draw-list.h・試験の指摘 0、触った file は base より指摘が増えていない（cursor-plane.c の世代の条件演算子は直前の version の行と対の短い選択として残した）。
- 未実施: 受け入れ 3（T1: i915 の 5330・Venus の QEMU で shot.c の画が前と同じ、`KWL VULKAN_ERROR`・`KWL FAILED` なし、App Home の開閉・drag・終了、FreeBSD の build）、受け入れ 4・5（Pi）。Q1 に依頼を返した。

## Pi で測ること（Q1 が束を置いて測る、1 boot）

- `KWL STARTUP step=cursor-shapes ms=` が 50ms 以下（受け入れ 4）。
- 場面: pointer だけ 10 秒、App Home の開閉 5 回、窓の drag 10 秒。各窓の `KWL PERF walk` の行を読む。
- p004 に進む閾値（受け入れ 5）: `small`（100 未満）の p90 ≤ 8ms かつ max ≤ 16.7ms、`large`（300 以上）の p90 ≤ 16.7ms かつ max ≤ 25ms。あわせて `replay` の p50・p90・max（walk から外れた分の目安）と `KWL PERF compose draw_ms` を前の boot と比べる。
- `KWL CURSOR_PLANE` の present が今までどおり出ること（世代の比較で像の copy が増えないこと: `copy=` の数が像の変化の時だけ）。
