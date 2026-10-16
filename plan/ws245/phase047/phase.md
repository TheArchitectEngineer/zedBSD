<!-- awesome-plan project=zedbsd record=ws245-p047 -->

# ws245-p047: 回帰の証拠の道具（survey と書き込みの記録）

Status: in-progress（q1218-i01 P1: 実装と host 試験・build は済み。5330 の素の起動の回帰はこれから）
Parent: [WS245](../ws.md)
Queue: q1218 / q1218-i01（承認: 2026-10-16 ユーザー「別なサブエージェントで、i915の実装変更のWSを進めてオーケーです。こちらはmainに取り込む前にベアメタルテストして、オーケーならmainに取り込んでいきます。USBを挿しておくので、UEFIのエントリーを制御して自動テストしてほしいです。実機は 10.0.30.3 です。」）
設計: [design.md](../design.md) §6.2 の p047 の行、§6.3「証拠の道」、表 R の PIPE_CRC_CTL の行（改訂 6）

## 範囲

各段の 5330 の素の起動の回帰で、内蔵 LCD に映っている事を register の証拠で確かめ（2026-10-15 夜 ユーザー「register などの証拠で代える」）、段が display の書き込みの列を変えたかを比べる道具。旧 code の置き換えは無い。

## 作った物（2026-10-16）

| file | 中身 |
| --- | --- |
| `src/drivers/gpu/i915/display/survey.c/.h`（own、Zlib、新） | survey 1（window の最初の serve = picture up の直後）と survey 2（1 s 以上後の最初の flip）。31 個の register（PWR_WELL_CTL・DC_STATE_EN・DPLL0・DPCLKA・DDI_BUF_CTL_A・MBUS/DBUF・PP・PWM と、resident の pipe の TRANS_CONF・FUNC_CTL・SRCSZ・PLANE_CTL/SURF/SURFLIVE・FRMCNT・CRC_CTL/RES_1・WM・BUF_CFG・LINETIME・DBOX・SCANLINE）を `i915: survey <回> <名前>=0x…` で出す。resident の pipe は PLANE_SURF が resident の buffer の pipe（無ければ A）で、その pipe の well（display 13 は PW_A〜D、12 は PW_1/3/4/5）が on の時だけ pipe の register を読む。pipe CRC は survey 1 で plane 1 の source で enable、survey 2 で frame を挟んで 2 回読み 0 に戻す（frame を待つのは最大 50 ms、1 起動に 1 回）。window が先に閉じれば閉じる時に止めて `survey 2 skipped`。判定の 1 行 `i915: survey: lit|NOT lit: panel …, backlight …, transcoder …, frames +N, live surface …, crc …, underruns …` |
| `src/drivers/gpu/i915/mmio-record.c/.h`（own、Zlib、新） | start から survey 1 までの display の書き込みの記録。範囲 0x40000〜0x9FFFF・0xC0000〜0xCFFFF・0x1010A0〜A7・0x138124〜2F・0x160000〜0x16FFFF。割り込みの register（0x44200、0x44400〜0x4447F、0xC4000〜0xC400F）は数だけ。連続 16 個以上の番地の列は 1 個の run（個数と和）に畳む。4096 個の ring（溢れは数だけ）、最初の Gen12 の register block だけ、262144 回の書き込みで報告されなければ自分で閉じる。digest は順序つき（FNV-1a）と順序無し（和）。guard は interrupt を止めた flag（ranked lock ではない: MMIO の書き込みはどの lock の下からも来る） |
| `src/drivers/gpu/i915/mmio.c`（own、GT と共有） | 書き込みの 5 関数（`write32`・`write32_auto`・`raw_write32`・`rmw32`・`write32_masked`）を static の `i915_mmio_store` に集め、記録が開いている時だけ `drv_i915_mmio_record_write` を呼ぶ（weak: host の contract 試験は mmio-record.c を link しない） |
| `src/drivers/gpu/i915/display/xd-io.c` | `drv_i915_xd_io_step` が device の時は記録に印を入れる |
| `src/drivers/gpu/i915/display/present.c` | `i915_present_window_serve` の入口と serve の後、`i915_present_flip` の入口に呼び 1 行ずつ。R11: 開発の管理情報 47 箇所（ws・BUG・review の番号、日付つきの決定）を comment から消した（log の文字列は無し） |
| `platform/amd64/vmunix.mk` | i915 の行に 2 file。触った i915 の comment の「(WS245)」を消した（vmunix.mk の他の行の 56 箇所は担当の範囲の外: Q1 に報告） |
| `src/drivers/gpu/i915/tests/display/host-xd-survey-test.c`、`plan/ws245/tests/run-xd-host-tests.sh` | host 試験 |
| `plan/ws245/tests/5330-survey.sh`・`5330-survey-diff.py` | 5330 の証拠の取得と比べ（要約は `tests/evidence/`、dmesg の全部は `temp/5330/`（git に入れない）） |

段の phase.md の必須の項目: 消す file 無し、消す関数無し、殻の写し無し。表 R: PIPE_CRC_CTL（survey だけが書く。`I915_TEST_SET=display` の build（`drv_i915_test_lcd_start` が link される）では survey は CRC に触らない）。hook 無し。R11 の数: `present.c` 47 → 0、`mmio.c`・`xd-io.c` 0、`vmunix.mk` の触った行 1 → 0。

## 記録が閉じた後の cost（Q1 の依頼、2026-10-16 の build の `llvm-objdump`）

`drv_i915_write32` は forcewake の判定の後 `jmp i915_mmio_store` に、`i915_mmio_store` は `callq *0x8(%rax)`（bus の書き込み）の後に `cmpl $0x0, drv_i915_mmio_record_on(%rip)` と `je` で返る。記録の本体（`drv_i915_mmio_record_write`）は `noinline` で外に出した。増えたのは 1 回の direct の jmp/ret と callee-saved の 3 register の push/pop、1 回の memory の比較と分岐（約 10 命令）。weak の address の検査は kernel では link で定まり命令に残らない。

## 確かめた事

- `sh plan/ws245/tests/run-xd-host-tests.sh build/ws245-p047-host`: regs 495・io 48・platform 80・vbt 119・**survey 57** の検査、失敗 0（ASan/UBSan）。
- `sh plan/ws084/tests/run-native-decide-host-test.sh build/ws245-p047-nd`: 14 検査、失敗 0（display の host の lib が survey.c を含んでも link する事）。
- `make -j16 ZEDBSD_CONFIG=config/ci/config-amd64.mk vmunix`: warning 0。`… BUILD=build/ws245-p047-tests I915_TESTS=y vmunix`: warning 0、`amd64 vmunix check: PASS`。
- `git diff --check`: 問題無し。

## 5330 の素の起動の回帰

（未実施。基準 = 親の main 49c578bd2 の kernel、段 = この段の kernel。USB は U10 の image（vmunix の sha256 は `build/uat/ws245-amd64/vmunix` と同じ c4219d62…、main 1716c14bc）。1716c14bc → 49c578bd2 の kernel と uapi の差は AMD の driver の追加だけで、U10 の userland のまま差し替えてよい。）
