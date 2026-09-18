# WS031 報告 E-107／E-108（2026-09-18〜19）: 明示 VBT → 実 panel 設定、PPS／VDD＋AUX → 実機で DPCD／EDID 取得

## 結論
- **最小目標を達成**: 現在の OVMF 構成のまま、明示した正しい VBT を採用し、**zedBSD の実 AUX 経路から対象 LCD の DPCD／EDID を取得、採取済みの識別情報と一致、PPS／VDD の所有を安全に終了**（実機 PASS ×2、取得値は 2 回で完全一致）。
- 方式: VBT parser も PPS／AUX も、**正本の関数本体を再入力せず generator で取り込む**（削除・置換は生成ファイルの header に全部記録、元の copyright／permission notice を保持）。hardware と時間への依存を 1 つの env 構造体に集め、**同じ本番関数**を register model（GPU-free）と実機の両方で動かした。
- 既存の乖離を 1 件発見・修正（DC_off 有効化が `gen9_set_dc_state()` を通っていなかった）。初期化経路に触れたので受入済み 6 モードを現ソースで再実行: **6/6 PASS**（EU 5/5、DRAW 1024/1024、R1 12/12、TEX 1024/1024、T3 9/9、BL 4/4、全モード `probe=COMPLETE cleanup=1`、GPU-free ktest 各 407/0）。
- 変えていないもの: 10 ms tick、HAL、execlists、累積修正、OVMF と既存の GPU 初期化経路、vfio-pci。UAPI／HAL の契約変更は無し。git commit／push なし。

## 主作業 1（E-107）: 明示 VBT → 正本 parser → 実 panel 設定
| 項目 | 内容 |
|---|---|
| 供給元 | read-only firmware provider `zedbsd/vbt/dell-latitude-5330-1028-0b02.vbt`（採取済み `i915_vbt.bin` の byte 写し、8704 B） |
| 採用条件 | build が明示的に要求（`PARITY_VBT_EXPLICIT=1`、表示試験 flag も要求する）**かつ** PCI subsystem = 1028:0b02 **かつ** kernel 内で計算した SHA-256 が pin と一致 **かつ** `intel_bios_is_valid_vbt()`。flag 無し＝触らない、別機種（試験: 1028:0b03）＝黙って適用しない |
| 完全 hash | sha256 `3bff4a0920d55c9aee0ea3c678904f982f8671c0e7b5bc97a335e429b29624cd`（SHA-256 実装は FIPS 180-4 の "abc" vector で検査） |
| 記録 | source = `EXPLICIT_BLOB`(3)。log に名前・size・hash・subsystem・採否と「OpRegion present=0 is unchanged」。**OpRegion があるふりはしない**（ASLS は firmware が残した 0 のまま、採取した OpRegion 全体／ASLS 値は不使用） |
| 取得と解析の分離 | 取得順 = OpRegion（今は byte 無し）→ 明示 blob → PCI ROM。選ばれた byte 列を `intel_bios_init()` 相当へ渡す。native 用 OpRegion／RVDA 経路は同じ parser へ繋ぐ形だけ用意（未実装） |
| 解析結果 | BDB 249、block 12、**child 4 = A: eDP（type 0x1806、DVO DP-A、AUX A）／B: HDMI（0x60d2、DDC pin 2）／TC1・TC2: DP Type-C/TBT（0x68c6）**、port C 無し。panel type 2、18 bpp、PPS T3 2000／T8 800／T9 2000／T10 1100／T12 5000（×100 µs）、PWM backlight 200 Hz／active high／controller 0／min 15 |
| 既定 child との区別 | child は一箇所から: VBT があれば実 VBT だけ、無ければ正本の `init_vbt_missing_defaults()` だけ（A/B/C、version 155）。継ぎ足さない。GPU-free で両方を照合 |
| parser の差分 | `intel_bios.c` 3681 行 → 2614 行。変更 = include → compat、関数単位の削除 22 本（SDVO／PSR／MIPI DSI／DSC／SPI・PCI ROM 取得／TV・LVDS 判定）、その呼出し 4 箇所を comment out、VBT pointer の取得元 1 箇所、末尾の glue include。`intel_vbt_defs.h`／`intel_bios.h` は include 行だけ変えた複製 |
| 検証 | host（ASan/UBSan）19/0 — 期待値は**独立デコーダ** igt `intel_vbt_decode`。切詰め・signature 不正・BDB offset 範囲外は拒否、巨大 block size で arena を溢れさせない、二重 init は −EBUSY。ktest +8。実機: 実 VBT 由来の 4 出力で P0〜P7 完走、`probe=COMPLETE cleanup=1` |

## 主作業 2（E-108）: PPS／VDD と AUX → DPCD／EDID
**取り込み**（`tools/port_dp_aux_pps.py`）: `intel_pps.c`（lock、VDD on/off と遅延 off worker、panel status 待ち、delay 決定、レジスタ書込み、panel power／backlight bit）、`intel_dp_aux.c`（`intel_dp_aux_xfer`／`transfer`、SKL+ send ctl、TGL+ レジスタ）、DRM core（`drm_dp_dpcd_access/read`、`drm_dp_read_dpcd_caps`、I2C-over-AUX 一式、`drm_do_probe_ddc_edid`）。DRM core は i915 固定参照に無かったので kernel.org v6.8.12 を sha256 つきで参照 tree に固定。
呼出し順は `intel_edp_init_connector()` どおり: `intel_pps_init` → DPCD caps → eDP caps → EDID → `intel_pps_init_late` → 停止 `intel_pps_vdd_off_sync`。失敗時は正本の `out_vdd_off` と同じく VDD を落として返す。

**fake 試験（本番関数、host 63/0＋ktest 14）**
| 区分 | 確認したこと |
|---|---|
| 正常 | DPCD／eDP caps／EDID が採取値と一致。`PP_ON/OFF_DELAYS`=0x07d00001／0x044c0001、cycle 欄 6、software delay 200／110／600／80／200 ms（＝対象機の Linux の値）。VDD on は 1 回、初回 AUX の前に 200 ms、無電源 access 0、対象外レジスタ 0 |
| delay 規則 | firmware 値が大きければそちら／両方 0 なら eDP 仕様上限（210／500／610 ms）／firmware が残した VDD は参照 1 本で引き取る |
| 取得・解析の異常 | 無応答（−ETIMEDOUT、32×5 回で有限打切り）、native DEFER／NACK、予約 reply、**短い reply を全量扱いしない**、receive error、禁止 size 0／21、busy 固着、I2C DEFER／NACK、**部分 I2C read の継続**、checksum 不一致／途中切れ／全 0 の EDID、extension block、buffer 超の extension 数、電源 get 失敗、不正構成、二重 begin |
| 寿命・片付け | 全失敗経路で VDD off・`vdd_wakeref` 無し・worker 無し・電源参照 0・underflow 0。worker は期限前に走らず、期限後に VDD と参照を返す。end は cancel して返す |

**実機（`-DPARITY_AUX_TEST=1`、GPU 投入なし）— PASS ×2**
```
pps before:               PP_CONTROL=0x00000000 PP_ON_DELAYS=0x00000000 PP_OFF_DELAYS=0x00000000      ← OVMF 下では未設定
pps after intel_pps_init: PP_CONTROL=0x00000060 PP_ON_DELAYS=0x07d00001 PP_OFF_DELAYS=0x044c0001      ← Linux と同値
acquire: rc=0 dpcd_ok=1 edp_dpcd_ok=1 edid_ok=1 edid_blocks=1 i2c_defers=0 log_errors=0 wait_timeouts=0 elapsed_ms=396
after acquisition: PP_CONTROL=0x00000068 (VDD)  vdd_wakeref=1 refs core=0 aux=1
DPCD 000: 11 0a 02 41 00 00 01 00 02 00 00 00 00 0b 00 | eDP 700: 01 18 00 | 100: 0a 02
EDID: 128 byte 全量を log に記録、mfg=06af product=2b99（AUO B133HAN）、checksum 0x74
end: rc=0 vdd_hw=0 vdd_wakeref=0 worker_pending=0 refs core=0 aux=0 put_underflows=0 well_refs 0 -> 0
verdict: PASS (acquire=0 late=0 end=0 dpcd_match=1 edp_dpcd_match=1 edid_match=1 wells_balanced=1)
runner-result probe=COMPLETE cleanup=1、ktest 407/0
```
保存済み EDID は照合にだけ使用（実 AUX 応答の代用にしていない）。
**PPS／VDD の副作用**: `PP_ON/OFF_DELAYS` と power-cycle 欄は書いたまま残る（正本も同じ、値は Linux と同一）／VDD を約 0.4 秒 on → off／panel power と backlight は未操作（`PP_STATUS` 終始 0）／AUX_A well と DC_off を一時取得し返却（well 参照 0→0）／DPCD への書込み無し。

**まだ正本どおりでない点**（台帳 E-108 §1）: 実行位置（正本は `intel_setup_outputs` 内、今は P7 後の試験モード）／遅延 VDD-off worker は timer 未接続（期限を記録し明示駆動、停止経路が cancel-sync）／PPS・AUX の mutex は所有の表明（単一 thread）／`power_put_async` は即時 put／sleep は busy-wait（10 ms tick より細かい sleep が無い。HAL 非変更）。いずれも常駐デバイス化で解消する対象。

## 発見して直した既存の乖離
1 回目の実機 log に正本の診断 `DC state mismatch (0x2 -> 0x0)`。`parity_dc_off_enable()`（`gen9_disable_dc_states` 相当）が `DC_STATE_EN` を直接書き、software 側 `dc_state` が更新されていなかった。P7 で DC6 を許可した後に DC_off を取る経路は今回が初めてだった。正本どおり `gen9_set_dc_state()` 経由へ修正 → 2 回目の実機で診断は消滅。

## 表示参照の追記（保存 dump と正本から）
実 link 設定 DPCD 0x100／0x101 = 0x0a／0x02（能力の最大値とは別情報として保持、link M/N で検算）／cpu_transcoder = **TRANSCODER_A**（旧 TRANSCODER_EDP block は全 0。「EDP A」は intel_reg の表示文字列）／backlight = `cnp_pwm_funcs`、controller 0、`BXT_BLC_PWM_CTL/FREQ/DUTY(0)` = 0xC8250／0xC8254／0xC8258 = 0x80000000／96000／96000（rawclk 19200 kHz ÷ 200 Hz）／PPS T11+T12 = VBT 5000＋1000 → 6000（600 ms）→ `PP_CONTROL` bits 8:4 = 6、T8／T9 は hardware 値 1 でソフトウェア待機。

## Vulkan 側の並行作業
- **空成功の撤去**: 未実装 builtin opcode は reader を poison して `ENOTSUP`（command に長さ語が無く終端を確定できないため、その stream の解釈・実行を止める。payload を次の opcode として読まない、reply 長は公開されない）。host 試験追加（0／1／17／148／180 の後ろの正しい probe が実行されないこと）、9 fixture PASS。
- **vkdemo の実 command 依存表**（`handover/notes/vkdemo-dependency-table.md`、source と埋込み SPIR-V の decode による。実行しての採取ではない）— opcode 不足より手前の関門:
  1. libvulkan は今の i915 node を open 時に拒否する（capset に VK XML version・timeline 数・168 byte の vendor suffix が無く、STRICT_QUEUE＋QUIESCE＋JOB＋JOB_CAPACITY＝flags 7 が必要）。flags は byte 合わせではなく**契約の宣言**（fence は成功時だけ完了／submit 単位の RESERVE→COMMIT・CANCEL と WAIT／他 session を壊さない退役）→ parity の request／fence／reset の上で実装してから立てる。
  2. **SPIR-V parser は vkdemo の `-O0` vertex shader を下ろせない**（Function storage の local 8 個、float 定数、OpFNegate、component access、3〜4 要素 compose が無く、FSUB は ADD として lower、**未対応 opcode を黙って skip**＝compiler 版の空成功）。fragment 側の opcode は全部認識。
  3. `vkCmdBeginRenderPass`(133) ほか recording 系 126／115／116／103／132 に handler 無し。
  - vkdemo は compute なし。最大 command は約 3.1 KB で opcode 180 は出ない見込み（size からの導出）。未確認: 既存 31 handler が libvulkan の byte 配置をそのまま消費するか、JOB ioctl が i915 と端から端まで動くか。
- 通常 Vulkan の公開は成功扱いにしていない。

## 出典
出典台帳 §5（VBT）・§6（DP）に追記。Linux／DRM 由来の生成物と複製は元表示を保持（`drm_dp_helper.c`／`drm_dp.h` は Keith Packard の HPND 系 notice で MIT とは別文面、`drm_edid.c` は複数名義＋MIT — 名義は元ファイルから複製、推測で記入していない）。対象機の VBT と panel の DPCD／EDID は機体データとして「ライセンス・配布可否 未監査」と明記（VBT は E-107 で初めて `src/` に入った）。

## 補足: LCD の撮影
Claude 実行ホストのカメラで対象機の LCD を撮影できることを確認した（`tools/capture_lcd.ps1`、WinRT MediaCapture）。今回は点灯操作をしていないので panel は消灯のまま（`PP_STATUS` 終始 0 と整合）。LCD-B 以降の実機 run で撮影を報告に添付する。合否の一次根拠はレジスタ読み戻しとログのまま。

## 次
常駐デバイスの寿命管理（VDD-off worker と async put を worker／timer へ、mutex を実体へ、eDP 取得を正位置へ。既存の「試験して片付ける」モードは回帰用に残す）→ EDID から mode、backlight 設定（`cnp_pwm_funcs`、rawclk readout）→ full-HD scanout object（GGTT 窓拡張、表示用 pin）→ LCD-A → LCD-B。

提出物: 台帳 E-107／E-108（`plan/ws031/results-ws031.md`）、`handover/increment-results/`（実機 log、累積 patch）、`handover/notes/vkdemo-dependency-table.md`、`display-ref/README.md` 追記。
