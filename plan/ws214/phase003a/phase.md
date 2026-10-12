<!-- awesome-plan project=zedbsd record=ws214-p003a -->

# ws214-p003a: kernel の段（承認不要）: display の op の gate 外し、状態から作る list、coalesce、B-k2

Parent: [WS214](../ws.md)
Status: in-progress（2026-10-12 P2。実装と host・build の確認は済み、Pi・T1 の確認の前。clear は Q1 の判定）
Phase disposition: normal
Queue: P2（2026-10-12 Q1 の割り当て）

## 範囲

設計は [p001 第 3 版](../phase001/phase.md) の 4.3 (1)〜(9) のうち p003a の分（7 節の表）。UAPI（`include/uapi/gpu-display.h`）の外に見える番号・意味は変えない（PRESENT・PRESENT_SYNC の返す番号は「成功した present の数」、WAIT は眠らない）。新しい UAPI は p003b（2026-10-12 ユーザーの承認済み、p003a の merge の後）。HAL の API・toolchain は変えない。

- core（`src/drivers/gpu/gpu.c`、`include/drivers/gpu/gpu-display.h`）: `drv_gpu_display_ops` に `adoption_wait` と `flags`（`DRV_GPU_DISPLAY_UNGATED`）。UNGATED の backend では PRESENT・PRESENT_SYNC・WAIT・CURSOR_SET・MOVE・HIDE が session の gate を通らない。session の `table_lock`（spinlock）の下で resource の表を歩き・変え、resource に pin の数。fence（PRESENT_SYNC）は lookup の前。pin は backend の記録の op の間だけ、採用の待ちは pin を放した後の `adoption_wait`。DESTROY は pin が 0 になるまで待つ。`gpu_ops_validate` の組の検査。i915・Venus は flags 0 で今どおり。
- bcm2711（`display-flip.c`・`display-flip.h`・`display-device.c`・`display-irq.c`）: 状態（primary・cursor）から list を作る、3 slot（各 256 word、SRAM の word 256〜1023）、まだ採られていない list の差し替えと coalesce、settle と EBUSY（pending・busy）の撤去、記録の時の `state_serial` と IRQ の `adopted_serial`・waitq、借りを状態の項目に持たせ thread の文脈で解く、lease の写しと失効（revoking）、cursor の op を controller の mutex から外す（plane の mutex、B-k2）、cursor の画像 5 枚と `cursor_named` を 3 slot で、`check_pipeline` の next・回復の位置、差し替えで PV の status を書かない、underrun を publish で止めない、attach の大きさ `0x5000`、copy の経路は 2 枚＋controller の mutex＋採用待ち、clock は lease の間は下げない（clock の mutex）、console の省略（不透明な全画面の primary の下）、計数（記録・coalesce・差し替え・underrun）。
- 値の出典の表: [../phase003/hvs-values.md](../phase003/hvs-values.md)（p003a で使う値）。終わりに GPL の類似の監査。

範囲外: overlay、COMMIT・OVERLAY_INFO、WAIT を眠る形に、外に見せる番号を `state_serial` に、帯域の予算と ENOSPC、`pair` と採用の時刻の輪（p003b）。

## 受け入れ

1. host: `plan/ws141/tests/display-host-test.sh`（新しい形に直した flip・device の試験を含む）と `host-display-cursor.sh`、新しい `plan/ws214/tests/` の試験（ungated の op の並行、DESTROY の pin の待ち、revoking、coalesce、3 slot の差し替え、cursor の画像 5 枚）が PASS、ASan・UBSan の版も。
2. build: rpi4 の vmunix と amd64 の vmunix が warning 0。
3. Pi（焼き直しの後、T1・Q1 経由）: 今の desktop で pointer を動かしながら窓の中身を描き続けて、primary の present の EBUSY・ETIMEDOUT が 0、cursor の op が primary の採用の待ちの間に返る（WS141 p016 の数え方）、flip の report の underrun・差し替え・coalesce の数。T1: i915・Venus の回帰。3 は p003a の clear の条件で、Q1 が判定する。

## コマンドと上限

- host: `timeout 300 sh plan/ws141/tests/display-host-test.sh <out>`、`timeout 300 sh plan/ws141/tests/host-display-cursor.sh <out>`、`timeout 300 sh plan/ws214/tests/display-p003a-host.sh <out>`。
- build: `timeout 3600 make -j16 -o <worktree>/build/arm64/sysroot/.zedbsd-sysroot-complete ZEDBSD_CONFIG=config/ci/config-rpi4.mk build/arm64/vmunix`、amd64 は `ZEDBSD_CONFIG=config/ci/config-amd64.mk build/amd64/vmunix`。
- QEMU は起動しない。同じ条件で変更なしの再試行は 3 回まで。

## 結果（2026-10-12 P2）

commit: 0c60852f7（実装・試験、Q1 が main に merge 済み）、続く style の直しと記録の commit（この phase.md と同じ commit）。

### 実装

- core: `drv_gpu_display_ops` の末尾に `adoption_wait` と `flags`、`DRV_GPU_DISPLAY_UNGATED`（`include/drivers/gpu/gpu-display.h`）。`gpu_ops_validate` は「UNGATED と adoption_wait は組」「未知の flag は EINVAL」。UNGATED の backend では PRESENT・WAIT・CURSOR_SET・MOVE・HIDE を `gpu_session_enter` を通さず（`gpu_display_admit` で device・session の error だけ見る）、PRESENT_SYNC は依存の fence を待った後に同じ道。session に `table_lock`（spinlock）と `pin_waitq`、resource に `pins`。resource の表への追加は `gpu_resource_publish`、lookup・unlink・close の walk は lock の下。ungated の PRESENT・CURSOR_SET は `gpu_resource_pin` で引き、backend の callback の後に unpin、PRESENT はその後 `adoption_wait`。DESTROY は pin が 0 になるまで眠る（interruptible）。
- bcm2711 `display-flip.c`: 状態（primary・cursor・lease の写し・revoking）と `state_serial`・`list_serial`・`adopted_serial`。list は 3 slot（word 256・512・768）に状態の全部から作る（console は不透明な全画面の primary の下で省く）。出ている list が 0 なら新しく出す（PV の status を消す）、1 なら差し替え（消さない）、2 なら coalesce して採用の IRQ が出す。vblank の IRQ は `0x30` と出した list を比べ、採られた list とそれより古い list を退役、`adopted_serial` を記録して waitq を起こす。借りは状態の項目が持ち、置き換えた記録の番号と一緒に退役待ちへ、`bcm2711_display_flip_reap`（thread の文脈）が解く。`bcm2711_display_flip_wait` は lock も pin も持たずに眠る（100ms、過ぎたら uncertain）。restore は records を止め（restoring）、console の採用で全部の借りを解き clock を下げる。clock は `clock_mutex` で直列、lease の間は上げるだけ。underrun は publish で止めず、選んだ PV の vblank ごとに消して許可し直す。flip の report（v3d.perf=1）に記録・list・差し替え・coalesce・underrun の数。settle と EBUSY（pending・busy）は無くなった。
- bcm2711 `display-device.c`: present は記録だけ（BLOB は buffer を retain して primary の hold に、copy は controller の mutex の下で 2 枚の copy 先から「どの list にも state にも名指されていない」物、無ければ list だけが名指す物の退役を `wait_unlisted` で待つ）。`output_adoption_wait` が採用を待ち、今どおり「成功した present の数」を返す（`sequence_guard`）。WAIT は mutex を取らない。cursor の SET・MOVE・HIDE は `plane_mutex` だけ（B-k2）。cursor の画像 5 枚。claim は `flip_own`、release・close は mutex の前に `flip_revoke`、restore の後に `flip_disown`。
- 他の WS の file（機械的）: i915 `display/display.c`・`display/capture.c`、`venus/display.c` の op の表に新しい欄の NULL・0（`-Wmissing-field-initializers`）と comment。
- UAPI・HAL・toolchain は変えていない。

### 確かめ（host、QEMU・実機ではない）

- `timeout 900 sh plan/ws214/tests/display-p003a-host.sh build/p2-ws214/p003a-host` PASS（ordinary と ASan/UBSan）: 新しい `host-display-ungated.c`（flag の組の検査、present の callback の中から同じ open の cursor の move と admission を取る GET_INFO が通る、pin の間の DESTROY は眠る（hook で EINTR、resource は残る）、adoption_wait の間の DESTROY は眠らない、gated の backend では同じ入れ子の move が EBUSY）、ws141 の `host-display-cursor.c`、ws113 の `host-display-control.c`、ws014 の `gpu-scanout.c`・`gpu-framework.c`、bcm2711 の flip・device。
- `timeout 300 sh plan/ws141/tests/display-host-test.sh build/p2-ws214/display-host` PASS。flip の試験は新しい形に直した: 3 slot、list の符号（console の省略・順）、差し替えで PV の status を書かない、2 つ出ている間の coalesce と採用の IRQ の publish、古い list を飛ばした採用、`adopted_serial` の「以下が全部採用済み」、借りは IRQ でなく thread で解ける・置き換えの採用まで残る・1 つの allocation を 2 回借りる、lease の写し・revoke で待ちが EBUSY、copy 先の名指し・`wait_unlisted` の timeout、timeout→uncertain→restore、拒否の場合、report の数。device の試験: ungated の present＋adoption_wait、cursor の move が primary の採用の待ちの間に通る（mutex の stub は再入を assert）、3 つの list と記録の cursor が 4 枚を名指して 5 枚目が空く、release で cursor が消える、revoke の後の move は EBUSY。
- build: `make -j16 -o <worktree>/build/arm64/sysroot/.zedbsd-sysroot-complete ZEDBSD_CONFIG=config/ci/config-rpi4.mk build/arm64/vmunix` exit 0 warning 0、`make -j16 ZEDBSD_CONFIG=config/ci/config-amd64.mk build/amd64/vmunix` exit 0 warning 0（i915・Venus を含む）。capture.c は試験の build だけで compile する file で、今回 compile していない。
- style: `python3 plan/tools/style-check.py` で新しい code の指摘は 0（残りは前からの cadence・cursor_words・cursor_set の 10 件、p002 で見る）。`git diff --check` PASS。
- 値の出典と GPL の類似の監査: [../phase003/hvs-values.md](../phase003/hvs-values.md)。

### 未実施・残件

- Pi（焼き直しの後、T1・Q1 経由）: 受け入れ 3（今の desktop で pointer を動かしながら描き続け、primary の present の EBUSY・ETIMEDOUT が 0、cursor の op が採用の待ちの間に返る、flip の report の underrun・replaced・coalesced）。差し替えで HVS が完全な list の一方だけを採ることの実機の確かめ。
- T1: i915・Venus の回帰（gate が変わらないこと）。
- SMP の並行は host の single thread の模型では確かめられない（lock の順の review だけ: controller の mutex → plane の mutex → clock の mutex → guard、table_lock は他の lock と入れ子にしない）。
- PRESENT_SYNC の ungated の道は host で流していない（bcm2711 の display node は `GPU_CAP_FENCE` を持たないので今は使われない）。
