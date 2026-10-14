<!-- awesome-plan project=zedbsd record=ws233-p002 -->

# ws233-p002: menuconfig の Build boot image で firmware の license を TUI で聞く

Parent: [WS233](../ws.md)
Status: in-progress（2026-10-14 P1: 実装と host の確かめを済ませた。端末での目視（ユーザー）は未実施）
Phase disposition: normal
Queue: P1 の lane（Q1 の割当、2026-10-14）

## 依頼（引用）

- ユーザー 2026-10-14「make menuconfigでbuildするメニューからビルドするとき、ファームウェアのライセンスをTUI上でyes/noする画面って、作れそうですか？」→「では作ってください。ライセンスをスクロール表示して、きちんと法的な見た目にしてほしいです。」
- Q1 の読み（未確かめ）: `tools/menuconfig.py` の `build_boot_image` は make の stdout・stderr を log に流し、stdin は端末のまま渡す。未同意の firmware があると質問は log にだけ出て、stdin が tty なので `license-agree.sh` が待ち続け、TUI が止まって見える。

## 設計

### 1. make の goal（何も聞かない）

- `license-agree.sh` に mode を足す（環境変数 `ZEDBSD_FIRMWARE_LICENSE_MODE`、既定は今の対話）:
  - `pending`: 同じ引数で、記録（`license-accepted`）に license の sha256 があれば何も出さない。無ければ license の本文を対話の mode と同じ道（repo → upstream、大きさ・sha256 を確かめて `licenses/NAME.SHA256` に置く）で取り、標準出力に 1 行を出す: `PACKAGE<TAB>NOTICE<TAB>LICENSE-TEXT<TAB>RECORD<TAB>LICENSE-NAME<TAB>SIZE<TAB>SHA256`。何も聞かない。
  - `accept`（引数 `PACKAGE RECORD LICENSE-NAME SIZE SHA256`）: `licenses/NAME.SHA256` が大きさ・sha256 どおりにあること（表示した本文が確かめた物であること）を確かめ、記録に対話の mode と同じ行（`SHA256  NAME`）を足す。記録の形式を書くのはこの script だけ（menuconfig は形式を持たない）。
- `make firmware-licenses-pending`: 選ばれた package（rpi4-boot は arm64 なら常に）のうち cache が無い物（build が初回に聞く物と同じ条件）について `pending` の mode を流す。parse の時に聞く goal ではない（`ZEDBSD_FIRMWARE_BUILD_GOALS` に入れない）。

### 2. curses の画面

- `build_boot_image`: 「are you sure?」の Yes と config の保存の後、build の前に `make -s firmware-licenses-pending`（stdin は DEVNULL）を流して一覧を取り、package ごとに license の画面を出す。
- 画面（法的な文書の見た目）:
  - 1 行目: 反転の帯に「Firmware License Agreement」、2 行目: package 名（太字）と「(n of m)」。
  - 枠の中を scroll: 「FIRMWARE」の節（notice の前半: 何の firmware か・対象の hardware・入る file・出所）、「COPYRIGHT NOTICE」の節（notice の後半: 著作権表示）、「LICENSE — <名>」の節（license の全文を原文のまま、行は変えず枠の幅で折り返す）。
  - 枠の下: 位置「Lines 1-17 of 212 (8%)」と操作の説明。
  - その下: 「By selecting Accept, you confirm that you have read and agree to be bound by the license terms above.」と `[ Accept ]` `[ Decline ]`。
  - 操作: ↑↓（j/k）1 行、PgUp/PgDn（b/Space）1 画面、Home/End（g/G）、←→・Tab で button、Enter で決定、`a` で Accept、`d`・Esc・`q` で Decline。
  - **最後まで scroll するまで Accept は選べない**（灰色、「Scroll to the end of the license to enable Accept」）。理由: 同意の前に license の全文が画面に出たことを担保する（clickwrap の作法。法的には、条項を読む機会が与えられたことが同意の有効性の要件とされることが多い）。初めの focus は Decline（Enter の押し間違いで同意しない）。
  - 狭い端末: 40x12 未満なら「The terminal is too small to show the license (at least 40x12). Enlarge it, or press D to decline.」だけを出し、Accept はできない。`KEY_RESIZE` で描き直す。
- Accept → `license-agree.sh` の `accept` の mode を呼ぶ（失敗すれば message を出して build しない）。Decline → build を始めず、断った firmware と「Firmware の menu で外せば build できる」（rpi4-boot は「Raspberry Pi 4 の起動に要るので外せない」）を出す。

### 3. build の stdin

`make -n` の数え上げと build の make は `stdin=subprocess.DEVNULL`。同意していない firmware が残っていても、parse の時の質問は EOF で直ちに失敗し、理由（`yes | make firmware-licenses` か menu の画面）は log に出る。

### 4. 試験（host）

`plan/tools/menuconfig-target-host-test.py`（`make menuconfig-host-test`）に足す: pending の一覧（rpi4 の config、新しい cache root、本文の大きさ・sha256）、`accept` の記録が対話の `yes` の記録と同じ行であること、`accept` の後は pending から消えること、Decline で build の make を起こさないこと、build の make の stdin が DEVNULL であること、画面を文字の配列に描く偽の screen で 80x24・120x40 を描き、Accept が最後まで灰色であること・End の後に Accept できること・狭い端末の表示。

## 結果

### 変えた file

- `userland/firmware/license-agree.sh`: mode `pending`・`accept`（`ZEDBSD_FIRMWARE_LICENSE_MODE`）。記録の行を書く所を `record_acceptance` の 1 か所にした（対話の yes と accept が同じ関数）。
- `userland/firmware/zedbsd-firmware.mk`・6 つの package の Makefile・`platform/arm64/firmware.mk`: `firmware-licenses-pending` と `<pkg>-license-pending`。
- `tools/menuconfig.py`: `firmware_licenses_pending`・`accept_license`・`license_document`・`license_paragraphs`・`wrap_document`・`license_visible`・`license_statement`・`draw_license`・`license_screen`・`declined_lines`・`settle_firmware_licenses`。`build_boot_image` は保存の後に `settle_firmware_licenses`、make の 2 つの呼び出しは `stdin=subprocess.DEVNULL`。
- `plan/tools/menuconfig-target-host-test.py`: `check_firmware_licenses` と、文字の配列に描く `_TextScreen`。build の stand-in の Makefile に `firmware-licenses-pending` を足した。
- `userland/firmware/README.md`: menu の画面の 1 段落。

### 設計の補い（実装で決めたこと）

- **license の折り返し**: 原文の行は約 78 桁で、80 桁の端末の枠（74 桁）では行ごとに折ると 1 語だけの行が並んで読みにくかった。そこで本文は段落ごとにつないでから枠の幅で折る（`license_paragraphs`）: 空行・list の項目（`*`・`#`・`(ii)`・`1.` など）・短い行（本文の最長の 6 割に届かない行。住所・著作権の行など）は原文どおり行を分ける。**語は 1 つも変えない**（試験で 7 つの license の語の並びが原文と一致することを確かめる）。原文の file は `licenses/` に残る。
- notice の `Key:   value` の行は値の桁で吊り下げて折る。
- 文言: 上の帯「Firmware License Agreement」、`License n of m`、節「1. THE FIRMWARE」「2. COPYRIGHT NOTICE」「3. LICENSE (名)」、下に「By selecting Accept, you confirm that you have read and agree to be bound by the license terms above.」（2 行に収まらない幅では「I have read and accept the license terms above.」）。
- Esc は Decline（curses の Esc の待ちの後）。

### 確かめ（host、2026-10-14）

| 確かめ | command | 結果 |
| --- | --- | --- |
| menu の host 試験（既存と追加） | `make menuconfig-host-test` | `MAC-T001 firmware licenses: PASS`、`MAC-T001 menuconfig round-trip: PASS`。追加の内容: rpi4 の config・新しい cache root で pending が 4 つ（bcm43455・brcmbt・rtl8822b・rpi4-boot）、本文の大きさ・sha256 が一致。menu の accept の記録が対話の `yes` の記録と同じ 1 行。accept の後は pending が 3 つ。見せていない本文（sha256 の違う物）の accept は記録しない。80x24・120x40 で Accept は最後まで灰色、↓・`a`・Enter では同意しない、End→Tab→Enter で同意、表示が画面の幅を越えない。30x10 は「too small」。Decline で build の make を起こさず、断った package と Firmware の menu の案内を出す。build の make と数え上げの make の stdin が DEVNULL |
| 実の端末（pty、TERM=xterm、80x24、curses.wrapper） | scratch の pty の script で End（`ESC O F`）→Tab→Enter | `license_screen` が True（同意）。scroll せずに Tab→Enter では同意せずに待つ（timeout で止めた） |
| p001 の同意の試験の回帰 | `sh plan/ws233/tests/check-license-agree.sh build/ws233-firmware-repo build/ws233-license4` | PASS（11 項目） |
| make の warning | `make -n ZEDBSD_CONFIG=config/ci/config-{rpi4,amd64}.mk bootloader` | 0 |

画面（`_TextScreen` に描いた text、試験の出力 `build/tmp/menuconfig-licenses.28rqdbpu/screen-*.txt`）は報告に添えた。

### 未実施

- ユーザーの端末での目視（色・反転・灰色の見え方、日本語の端末の幅）。
- menu から実際に image を build する通し（Build boot image の Yes → 画面 → build）。T1 か利用者の手で。
