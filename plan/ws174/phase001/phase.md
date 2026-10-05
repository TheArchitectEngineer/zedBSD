<!-- awesome-plan project=zedbsd record=ws174-p001 -->
# ws174-p001: 設計 — 起動時の Ctrl で safe boot options

Parent: [WS174](../ws.md)
Status: in-progress（2026-10-05 夜、設計の担当。設計の第 1 版を書き、design-reviewer のレビュー待ち）
Disposition: normal

## 由来

[WS174](../ws.md) のユーザーの指示。実機の 5330 で build/uat-0505g が kernel の起動の途中で panic したと見られるが、graphical boot で画面に何も出ず分からない（[BUG-202](../../bugs/BUG-202.md)）。

ユーザーの定義（2026-10-05 夜、Q1 経由）: 「safe boot optionsとはグラフィカルブートにせずカーネルのメッセージをコンソールに出力するオプションを選択することです。」→ safe boot は **graphical boot をしない（logo なし・`login=graphical` なし）** と **kernel の message を console に出す（`kmsg=quiet` なし）** の 2 つだけ。`ZEDBSD_GRAPHICAL_BOOT=n ZEDBSD_BOOT_KERNEL_MESSAGES=y` の config（`build/uat-0505g-text/zedbsd-native-uefi-graphical-n-kmsg-y.cfg`）と同じ選択。i915・ACPI を止めるなどは含めない。

## 設計

全文: [design.md](design.md)（第 1 版）。要点:

- **config の形**: `safe.NAME=VALUE` の行（loader だけの行、`logo=` と同類）。古い loader は普通の行として kernel に渡し、kernel は未知の名前として無視するので後方互換。`[safe]` の節は古い loader が file ごと拒否するので不採用。loader の組み込みの既定: Ctrl のとき normal の行の `logo`・`login`・`kmsg` を落とす。build は全 image の cfg に `safe.kmsg=console`・`safe.login=console` の 2 行を足す（明示のため。無くても同じ結果）。
- **Ctrl の検出（UEFI）**: `EFI_SIMPLE_TEXT_INPUT_EX_PROTOCOL` の `ReadKeyStrokeEx` の `KeyShiftState`（`SetState(EFI_KEY_STATE_EXPOSED)` で Ctrl 単独の event も読める firmware がある）。queue を最大 32 回まで読み、どれかの event に Ctrl（左右）が立っていれば safe。Ctrl+任意の key も safe。待ち時間は足さず、config の parse 直後（S1、video と logo の前）と kernel の読み込みの後（S2、`build_bootstrap()` の前）の 2 点で標本を取る。
- **kernel への伝達**: record が `kmsg=console login=console`（logo なし）になるだけ。kernel・init・sessiond・handoff・UAPI・**HAL は変えない**（承認の要る差分は無い）。
- **告知**: `Safe boot (Ctrl): no logo, kmsg=console, login=console` を ConOut と debug port に出し、safe の時だけ 1 秒止める。
- **panic**: `kmsg=console` なら HAL の早期 console が最初から描き、panic は `kern_text_reveal_fatal()` の既存の経路で画面に残る。追加の実装は無い。i915 の初期化で console が見えなくなる可能性は未確認（実機の text image の結果で分かる）。
- **BIOS PC/AT**: int 16h AH=02h の Ctrl flag で同じ parser を safe mode で呼ぶ。別 Phase（p004、ユーザーの判断）。PC-98 は Future Work。
- **試験**: parser の host 試験 H1〜H9・判定の host 試験 K1・K2（`plan/ws174/tests/`）。QEMU は T1 に 1 件で依頼: Ctrl 単独・Ctrl+Space・key なし・kernel 起動後の Ctrl の 4 cell を 1 つの image で（判定は screendump の PNG と SSH の `sysctl kern.boot.login`、console log は読まない）。実機は 5330 でユーザーが Ctrl を押して写真。
- **Phase**: p002 parser（host 試験、BIOS loader の build を守る）→ p003 UEFI loader・build・docs（T1 の QEMU）→（p004 BIOS、任意）→ p005 規約の全文の見直し。

### ユーザーへの判断の項目（推奨つき、design.md §11）

| ID | 問い | 推奨 |
| --- | --- | --- |
| D1 | config の形は `safe.NAME=VALUE` でよいか | はい |
| D2 | config に safe の行が無くても Ctrl で `logo`・`login`・`kmsg` を落とす（組み込みの既定）でよいか | はい |
| D3 | safe の set は `kmsg=console login=console`（logo なし）だけ。`video=640x480` は入れない | はい（`video=` は GOP に無いと boot が止まる） |
| D4 | BIOS PC/AT の Ctrl（p004）をこの WS でやるか | 後回し |
| D5 | safe のときだけ告知の後に 1 秒止めるか | はい |
| D6 | 検出は Ex protocol だけ（simple の制御文字は使わない） | はい |
| D7 | S1・S2 の 2 点で標本を取る（S2 では video/logo は戻さない） | はい |
| D8 | `safe.kernel`・`safe.boot0` を拒否する | はい |

後の選択肢（この WS ではやらない）: `safe=1` の kernel parameter と `kern.boot.safe`、safe の set への `i915.start=manual`、PC-98 の Ctrl、削除の文法。

## レビュー

（design-reviewer の結果と反映をここに書く）

## 実行の記録

- 2026-10-05 夜: 設計の担当が `bootloader/uefi/`（bootx64.c・zedbsd-config.c/.h・video.c・logo.c・transition.S・uefi.h）、`bootloader/pcat/bootzbsd.S`・`pc98/bootzbsd.S` の parser の呼び出し、`bootloader/common/logo-path.c`、`include/kern/boot.h`・`src/kern/boot.c`（未知の名前の扱い）、`src/kern/panic.c`・`src/drivers/platform/pcat/graphics/text.c`・`src/hal/amd64/bsp-pcat/cons.c`（quiet と reveal）、`platform/amd64/vmunix.mk`（cfg の生成）、`plan/tools/boot-test.sh`・`qmp.py`・`guest/`、WS013 の試験の索引を読んで design.md を書いた。source は変えていない。build・試験は未実施（設計だけ）。
