<!-- awesome-plan project=zedbsd record=ws214-p003a -->

# ws214-p003a: kernel の段（承認不要）: display の op の gate 外し、状態から作る list、coalesce、B-k2

Parent: [WS214](../ws.md)
Status: in-progress（2026-10-12 P2 開始）
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

## 結果

（実行中）
