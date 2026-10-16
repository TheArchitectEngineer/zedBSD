<!-- awesome-plan project=zedbsd record=ws245-p047 -->

# ws245-p047: 回帰の証拠の道具（survey と書き込みの記録）

Status: cleared（2026-10-16 Q1: main 3e1e0c687 に merge（親 4aadd5780・38b4fed9e）、ci の vmunix は warning 0。5330 の素の起動の回帰に合格。LCD の目視は beta2.md の U11）
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

## 5330 の素の起動の回帰（2026-10-16、素の起動。QEMU ではない）

image: main 786f90afd（networkd の DHCP の 169.254 を外した物、Q1 の指示「マージしたら、5330でのイメージに反映して」）を merge した 510b267f5 の上で `yes | make ZEDBSD_CONFIG=config/ci/config-amd64.mk firmware-licenses`（試験の image の build で担当が同意を流してよい: Q1、WS233 の取り決め、`plan/tools/guest/test-image.sh:67` と同じ）→ `make -j16 ZEDBSD_CONFIG=config/ci/config-amd64.mk disk-image`（`build/amd64/hdd-image.img`、2,485,125,120 byte、sha256 8f4eb268…、kernel の source の warning 0）。5330 の Linux から USB（`/dev/sda`、TRAN usb、removable、mount 無しを確かめた）に dd。GPT の PARTUUID が変わったので UEFI の entry `Boot0006 zedBSD USB ws245` を `efibootmgr --create-only` で足した（BootOrder は変えない）。基準の kernel は親の main 786f90afd の source から `BUILD=build/ws245-base` で build（sha256 01097983…、warning 0）。段の kernel は image の物（e4b5b87d…）。どちらも USB の ZEDBSD 区画の `vmunix` を差し替え、`efibootmgr -n 0006` → reboot、ssh で `5330-survey.sh`。AC 接続。

| 見る物 | 基準（786f90afd） | 段（p047、510b267f5） |
| --- | --- | --- |
| zedBSD の ssh まで | 54 s | 59 s |
| kern.boot.login / msgbuf_dropped | graphical / 0 | graphical / 0 |
| sessiond の SESSION start と HANDOFF | あり（ready=1 waited 17898 ms） | あり（SESSION start、HANDOFF ready=0 waited 30000 ms。同じ形は今日の基準 49c578bd2 の起動にもあった） |
| lease 1 claimed / picture up / device lost / i915 の error・warn の行 | 1 / 2 / 0 / 3 | 1 / 2 / 0 / 3（同じ 3 行: intel_bios_init・N0・edp acquire の文言の語） |
| perf の per submit | 4.20 ms・6.84 ms | 4.02 ms・6.71 ms |
| backlight-probe（get / set 100 / get） | 100 / 100 / 100 | 100 / 100 / 100 |
| survey の判定 | （survey は無い） | `lit`: panel on、backlight on、transcoder on、frames +225、live surface resident、CRC 0x717ca0a4 の 2 回、underrun 0 |
| 書き込みの記録の digest | — | 13613 writes、599 entries（5 runs）、割り込みの register 205、dropped 0、order 0xb961e20f、set 0xdcd7004e |

判定: R4 の共通の物は基準と同じ、survey は lit → **合格**（Q1 の判定待ち）。LCD の目視: 未確認（ユーザーの一覧に載せる。以後の段の受け入れに引き継ぐ: 改訂 6 の m14）。証拠: `plan/ws245/tests/evidence/5330-base-786f90afd-20261016.md`・`5330-p047-510b267f5-20261016.md`（dmesg の全部は `plan/ws245/temp/5330/`、git に入れない）。

### それより前の起動（記録）

- 13:59 基準（main 49c578bd2 の kernel、U10 の image）: 合格の形（evidence `5330-base-49c578bd2-20261016.md`）。
- 14:03 p047（de20a7fa）の 1 回目: ssh が届かず（DHCP が取れず link-local だった: `/var/log/messages` の networkd の行）。ユーザーの目視は「backlight が消えた状態で止まっていた」。調べ: USB の boot log（boot-000007）と次の起動で読んだ `kernel.log.old` では survey が `lit`（frames +1713、PWM enable、DUTY 0xbc3c）、kernel は電源を切られるまで 60 s ごとの perf の行を出し続けた。`/var/log/messages` に 05:18:16 UTC `sessiond: sleep asked from kei` / `NOSLEEP network reason=radio`。AC の cable が抜けていて battery（`KWL POWER source=battery`）だったので idle の限度が短く、compositor の idle の screen off（`userland/desktop/wayland/sleep.c:446`、backlight を off）が先に来た、で説明がつく。ユーザーは入力を試さずに電源を切った（2026-10-16 ユーザー「起動が止まったとき、何も試さずに電源を長押しして電源OFFしたので、もうわかりません。電源ケーブルは抜けていました。」）→ Q1 の判断: p047 の不具合ではないと扱う。survey は picture up の後の約 1〜30 s しか見ないので、その後の消灯・静止・入力の不達は見分けられない（`5330-survey.sh` に userland の log と前の起動の kernel.log.old を取る項目を足した: Q1 の指示 (a)。任意の時の survey の口（§7 #32）は作らない）。
- 14:24 p047 の 2 回目: ssh で証拠（evidence `5330-p047-de20a7fa-20261016.md`、survey lit、frames +142）。DHCP はユーザーが手で直した。05:27:34 UTC に auto login の session の compositor が `KWL FAILED site=compose_draw errno=5 operation=submit result=-4` で終わった（kernel の device lost は 0）→ BUG-325（tracking、記録だけ: 2026-10-16 ユーザー「GPU投入終了は記録だけ残して先に進みましょう。」）。
