# WS031 報告 E-109（2026-09-19）: eDP を常駐動作へ接続、LCD-A 第 1 片、shader compiler の拒否と減算の是正

## 結論
- **E-108 の単一 thread 向け適応 5 件を、常駐化と同じ単位で解消**（実 mutex／tick で譲る sleep／遅延 worker／async put／正本の probe 位置）。GPU-free の実 thread 試験と、**新構成での実機**で確認: 遅延 VDD-off が誰にも駆動されず自分で走る／期限前の再取得で使用中の VDD を落とさない／予約を残したまま停止しても同期取消・VDD off・参照返却。
- **LCD-A 第 1 片**（計算のみ、HW 未書込み）: 実機の実 AUX データから mode・bpp・link・M/N・DPLL 語を正本の関数で計算し、**同じ機体で Linux が設定した値と全部一致**。
- **Vulkan 側**: shader compiler の空成功を撤去（lower できない命令は理由つきで拒否）。「未対応」と別に、**FSUB が ADD に lower されていた誤動作を是正**。GPU job 完了契約（未完了／成功／失敗／取消）の対応表を作成。
- 現在地の区分は変えていない: **main link の設定・training、panel power、backlight、scanout は未実行**。DPCD 0x100/0x101 の読出し値を link 設定の証拠にはしていない。
- 回帰: 同一ソースで **7/7 PASS** — 受入済み 6 モード（EU 5/5、DRAW 1024/1024、R1 12/12、TEX 1024/1024、T3 9/9、BL 4/4。**いずれも明示 VBT なし**＝eDP の thread は起動しない従来構成）＋組合せ 1 本（**明示 VBT＋常駐 eDP のまま textured draw**: 描画 PASS、その間に遅延 VDD-off worker が自分で発火、停止時 refs 0・lock_errors 0）。GPU-free ktest 各 433/0。
- 変えていないもの: 10 ms tick、HAL、execlists、累積修正、OVMF と既存の GPU 初期化経路、vfio-pci、UAPI。device は非公開のまま。git commit／push なし。

## 1. 常駐化の接続（diff: `increment-results/e109-resident-connection.diff`、17 file）
| E-108 の適応 | E-109 |
|---|---|
| mutex は所有の表明 | env の `lock/unlock` → kernel の `struct mutex` 2 本（PPS、AUX hw）。**取得順は正本の helper 境界のまま**（生成 text 無変更）: `intel_pps_lock` = DISPLAY_CORE 参照 → PPS mutex／unlock は逆順。VDD の AUX 参照は PPS mutex の内側で power-domain lock（PPS→pd、逆順なし）。AUX hw mutex は transfer の外側。巨大 lock も wrapper 独自 lock も無し |
| busy-wait | 2 tick 以上は wait queue＋tick 期限で譲り、残りだけ短い bounded delay。要求値・retry 回数は正本のまま（変えたのは env の backend）。実機: 200 ms 待ち = tick 190.2 ms＋短い待ち 9.8 ms |
| 遅延 VDD-off が timer 未接続 | 共有基盤 `parity/backend_delayed.{c,h}`: timer thread が wait queue で期限まで眠る（timer IRQ 内では何もしない）→ 期限で共有 worker へ。ARMED（待機中）／PENDING／RUNNING を区別。`cancel`（実行中 body を待たない）／`cancel_sync`（待つ）／`flush` は別契約。**遅延 off = power_cycle_delay×5 = 3000 ms**（600 ms の power-cycle 待ちとは別）。停止時の同期取消は PPS mutex 取得**前**（正本の順序） |
| async put は即時 put | `power_domains.c` に正本の async put を移植: domain use count、parked mask 2 段、**次の get が parked 参照を取り戻す**（HW 変更なし）、work body と requeue、`flush_work`／`flush_work_sync`、状態検証。get／put は `pd->lock` で直列化 |
| 実行位置が P7 後／状態の寿命 | eDP 取得は **`intel_setup_outputs()` 内**（`intel_ddi_init` の DP connector 段）。失敗は正本の `goto err` と同じく encoder を落とす。`struct parity_edp_device` が lock・thread・env・**通常初期化の結果**を保持し、診断は再初期化せず結果を読む。停止時 `parity_edp_device_fini`。thread は実 VBT に eDP child がある機体でだけ起動 |

## 2. 常駐化の試験（三組＋基盤）
| 試験 | GPU-free（実 thread・実 tick、ktest） | 実機（`-DPARITY_AUX_TEST=1`、新構成） |
|---|---|---|
| 自動の遅延 off | 誰も駆動せず約 1 s 後に worker が PPS lock を取り VDD off＋参照返却 | `auto-off: reserved_ms=3000 … vdd_hw=0 vdd_wakeref=0 worker_ran=0->1 timer_fired=1 refs core=0 aux=0 well_refs=0` |
| 再取得との競合 | 期限前の read が予約を取消、古い期限を過ぎても VDD on、新予約が自動発火 | `re-acquire: reads_ok=1 kept_past_old_deadline=1 new_off_after_ms=1250 cancelled(timer/queue)=3/0 worker_ran=2` |
| 終了との競合 | **待機中**の停止＋**実行中**の停止（body が VDD-off 書込み途中で PPS lock 保持中）: deadlock なし、二重 off なし、終了後 access なし、end 後の body は何も触らない | `edp fini: end_rc=0 vdd_hw=0 vdd_wakeref=0 off_reserved=0 refs core=0 aux=0 lock_errors=0 | vdd-off work armed=7 fired=2 ran=2 cancelled(timer/queue)=5/0 | async put: puts=24 … state_errors=0 use_count_errors=0` |
基盤: `dwork:` 7 件（plain cancel は待たず cancel_sync は終了後に戻る、flush は今走らせる、ほか）、`pw-async:` 8 件（PARK／GRAB／RELEASE／SECOND／REQUEUE／FLUSH／EMPTY／NOT-LAST）。host 72/0、ktest **433/0**、実機 `verdict: PASS (resident=1 dpcd_match=1 edp_dpcd_match=1 edid_match=1 lcd_a_match=1 auto_off=1 reacquire_kept=1 reacquire_off=1)`、`probe=COMPLETE cleanup=1`。
参照の所有者の移動を記録: transfer の AUX 参照 → async put（VDD が別の 1 本を持つ間は parked にならない）／VDD の AUX 参照 → worker か停止経路が返す／停止 = 同期取消 → VDD off → `flush_work_sync`。合格条件は「DP 層が何も持たない＋power 層の parked は flush 後 0」（常駐中の正当な保持があるので、あらゆる中間時点での全参照 0 は求めない）。
DC_off 修正の回帰入力（P7 で DC6 許可後に DC_off を再取得）は AUX 試験に残っており、`DC state mismatch` 0 件。

## 3. LCD-A 第 1 片（`parity/lcd/`、`tools/port_lcd_calc.py`）
| 項目 | 計算値（実機の実 AUX データ＋VBT 18 bpp） | Linux が同じ機体で設定した値 |
|---|---|---|
| mode | 1920×1080、140800 kHz、h 1920/1936/1952/2080、v 1080/1083/1097/1128、6 bpc | transcoder A の timing と一致 |
| bpp | 18 | DDI func ctl の 6 bpc |
| link | HBR×2 lane、必要 316800／可用 540000 kBps | DDI A x2 HBR |
| M/N | TU 64、data 0x4b17e4/0x800000、link 273406/524288 | `PIPE_DATA_M1/N1`、`PIPE_LINK_M1/N1` と一致 |
| DPLL | ref 38400 kHz（CDCLK readout）、CFGCR0=0x00e001a5、CFGCR1=0x88 | DPLL0 と一致（WA #22010492432 の DCO fraction 半減込み） |
host 20/0（期待値の出所は保存済み Linux 値。私の可用帯域の誤算 432000→540000 を 1 件訂正）。拒否: 帯域不足、未知 rate code、lane 3、**eDP 1.4 sink（rate table 未移植）**、detailed timing 無し、sync 幅 0 — 近似しない。
**明記**: rate／lane の選択は正本の一般探索を移植せず、eDP<1.4 の `use_max_params` 規則だけを適用して正本の帯域関数で検算。EDID quirk 表は未移植。
**LCD-A の残り**: DDI buffer translation、transcoder／plane の register 語、CDCLK・帯域・DBUF／watermark の必要条件、enable／disable の状態列。**scanout object は未着手**（次増分の先頭: GGTT 窓拡張、ring／LRC／HWSP／scratch との重複検査、一枚の確保・pin・回収。Linux で観測した plane アドレスは使わない）。

## 4. generator 方式の補強
- 新 generator は manifest（元 source・生成物の sha256、取り込んだ部品、置換の適用件数）を出し、部品が**ちょうど 1 回**見つからなければ失敗（今回 2 件、私の想定件数の誤りを止めた）。
- `tools/check_generated.sh`: 3 generator＋fixture generator を再生成して `src/` と byte 比較、DRM 参照を `SHA256SUMS` で検査 → **26 file 全部一致**（手編集なし）。
- 削除関数を区分: 対象経路で不要（VLV/CHV、pre-DDI、他世代 AUX、SDVO、TV/LVDS）／**今後必要だが未接続**（PSR、DSC、MIPI、`intel_pps_backlight_power`、`intel_pps_reset_all`、AUX ch の調停、AUX 完了割り込み、eDP 1.4 rate table、link config 探索）。VBT から認識される TC1／TC2／HDMI は「解析できた出力」であって「enable できる出力」ではない。
- 注意点として記録: `intel_link_port.c` は 3 つの元（intel_dp.c／intel_display.c／drm_dp_helper）が同居し、drm_dp_helper 由来 2 関数の notice 本文は参照だけ → 次の再生成で元ごとに file を分ける（回帰済み source を変えないため今回は据置き）。

## 5. Vulkan 側
- **未対応の拒否**: function body 内の実行意味を持つ未 lower 命令、解決できない pointer 経由の load／store、input の component access、3 要素以上の composite、Sin/Cos/InverseSqrt 以外の ExtInst → `ENOTSUP`＋診断（opcode・位置・理由）。debug／注釈／構造は無視してよいものとして区別。malformed は `EINVAL` のまま。**vkdemo の vertex shader は拒否される**（`opcode 62 @word 403: store through a pointer that is not an output (Function storage?)`）。fragment shader は通る（ただし「opcode を認識する」と「正しい値へ lower する」は別で、受入済みの Mesa 生成 PS とは混同しない）。
- **誤動作の是正**: `FSUB` = ADD＋**第 2 source の negate**（Gen12 bit 121、出所 Mesa `gen/xe.json`）。試験は operand の register 番号と modifier を検査（5−2 と 2−5 を区別する形。命令名は条件にしない）。`DOT`→MUL、`COMPOSE`／`EXTRACT`→第 1 source の MOV は**別の演算**だったので拒否に変更。未知の IR op も拒否。
- pipeline 作成は `VK_ERROR_FEATURE_NOT_PRESENT` を返し、pipeline object も code buffer も残さない（実 vkdemo VS で試験）。従来の host 試験 3 本は「vkdemo VS が通る」ことを合格条件にしていたので書き換えた。VK host fixture 9 本 PASS。
- **完了契約の対応表**（`handover/notes/gpu-job-completion-contract.md`）: GPU core は四つの結末を区別済みで、**失敗時も WAIT は ioctl 0＋`status=正の errno` で戻り**、fence は error つき signal、資源は reset まで保持。通常 cancel は callback を外して完了を作らない。libvulkan は 0 以外を全部 `VK_ERROR_DEVICE_LOST` に畳む。前報の私の要約「fence は成功時だけ完了」は「**成功通知は実完了時だけ**」の意味に訂正。legacy i915 の不足: VK batch に completion が付かず decode 時点で成功通知（GPU 完了の証明は batch なしの marker だけ、engine 間の順序保証なし）、`GPU_CAP_FENCE` 無し、capset 156 byte。
  **レビュー事項**: STRICT_QUEUE／QUIESCE の意味を定める文書は libvulkan の comment 2 文だけで、168 byte の vendor capset を書く側が tree に存在しない。flags を立てる前に、プロジェクト側で意味の確定が必要。
- opcode 180 は「初回 vkdemo では使わない見込み」のまま（実 stream 未採取。未対応時の拒否は維持）。

## 6. 次
scanout object（GGTT 窓拡張＋重複検査、確保・pin・回収）→ LCD-A の残り → backlight（`cnp_pwm_funcs`、rawclk readout）→ **LCD-B**: CPU の非対称既知パターン（上下左右と試験識別を含む）。失敗時と正常終了時の表示停止・buffer 解放まで実装してから実機へ。受入は buffer 読戻し／レジスタ・link 状態・エラーログ／**写真** を別々の合格条件として一組にする（カメラ撮影は動作確認済み）。

提出物: 台帳 E-109、`increment-results/e109-resident-connection.diff`・実機 log・累積 patch、notes `gpu-job-completion-contract.md`、出典台帳 §7、移植台帳の P5 行更新。
