<!-- awesome-plan project=zedbsd record=ws174-p001 -->
# ws174-p001: 設計 — 起動時の Ctrl で safe boot options

Parent: [WS174](../ws.md)
Status: in-progress（2026-10-05 夜、設計の担当。設計の第 2 版（design-reviewer の指摘を反映）まで。ユーザーの判断 D1〜D8 と Q1 の判定待ち）
Disposition: normal

## 由来

[WS174](../ws.md) のユーザーの指示。実機の 5330 で build/uat-0505g が kernel の起動の途中で panic したと見られるが、graphical boot で画面に何も出ず分からない（[BUG-202](../../bugs/BUG-202.md)）。

ユーザーの定義（2026-10-05 夜、Q1 経由）: 「safe boot optionsとはグラフィカルブートにせずカーネルのメッセージをコンソールに出力するオプションを選択することです。」→ safe boot は **graphical boot をしない（logo なし・`login=graphical` なし）** と **kernel の message を console に出す（`kmsg=quiet` なし）** の 2 つだけ。`ZEDBSD_GRAPHICAL_BOOT=n ZEDBSD_BOOT_KERNEL_MESSAGES=y` の config（`build/uat-0505g-text/zedbsd-native-uefi-graphical-n-kmsg-y.cfg`）と同じ選択。i915・ACPI を止めるなどは含めない。

## 設計

全文: [design.md](design.md)（第 2 版）。要点:

- **config の形**: `safe.kmsg=console`・`safe.login=console` の 2 行（loader だけの行、`logo=` と同類。許可の表で名前と値を固定し、それ以外の `safe.` の行は file ごと拒否）。古い loader は普通の行として kernel に渡し、kernel は未知の名前として無視するので後方互換。`[safe]` の節は古い loader が file ごと拒否するので不採用。一般の `safe.NAME=VALUE` の override も不採用（safe boot を config で打ち消せてしまう）。loader の組み込みの既定: Ctrl のとき normal の行の `logo`・`login`・`kmsg` を落とす（safe の行が無い ESP でも効く）。build は **native UEFI の生成 cfg** に 2 行を足す（BIOS 用の cfg は zedinst の admission が名前を限るので触らない）。normal mode でも safe の record の長さを空測りして、全 loader が同じ file を同じ理由で受け入れ・拒否する。
- **Ctrl の検出（UEFI）**: `EFI_SIMPLE_TEXT_INPUT_EX_PROTOCOL` の `ReadKeyStrokeEx` の `KeyShiftState`。入口（S0）で `SetState(EFI_KEY_STATE_EXPOSED)` を頼み、S0・config の parse 直後（S1、video と logo の前）・kernel の読み込みの後（S2、`build_bootstrap()` の前）の 3 点で queue を最大 32 回読む。どれかの event に Ctrl（左右）が立っていれば safe（Ctrl 単独は EXPOSED を受ける firmware だけ、Ctrl+任意の key はどの firmware でも）。待ち時間は足さない。**電源投入からの Ctrl の押しっぱなしで効くかは firmware 次第で未確認**なので、docs の主の手順は「Ctrl を押したまま Space を繰り返し叩く」。
- **kernel への伝達**: record が `kmsg=console login=console`（logo なし）になるだけ。kernel・init・sessiond・handoff・UAPI・**HAL は変えない**（承認の要る差分は無い）。
- **告知**: `Safe boot (Ctrl): no logo, kmsg=console, login=console` を ConOut と debug port に出し、safe の時だけ 1 秒止める。safe mode は GOP に 640x480 を「希望」（無ければ現在の mode、boot は止まらない）。
- **画面に残る物**: panic・fault は quiet な graphical boot でも既に `kern_text_reveal_fatal()` で console を出す（BUG-158）。従って BUG-202 の「何も出ない」停止は、text console の登録前の停止か panic しない hang か reset と見られ、safe boot が直すのは「kernel の最後の message が読めない」こと。実機の確認は「最後の message が画面に残る」で判定する。追加の実装は無い。i915 の初期化で console が見えなくなる可能性は未確認（実機の text image の結果で分かる）。
- **BIOS PC/AT**: int 16h AH=02h の Ctrl flag で同じ parser を safe mode で呼ぶ。別 Phase（p004、ユーザーの判断）。PC-98 は Future Work。
- **試験**: parser の host 試験 H1〜H10・判定の host 試験 K1・K2（`plan/ws174/tests/`）。QEMU は T1 に 1 件で依頼: 1 つの QEMU で `system_reset` を挟み、Ctrl 単独・Ctrl+Space・key なし・kernel 起動後の Ctrl の 4 cell（key は QMP `input-send-event` で usb-kbd に 50 ms 周期、判定は screendump の PNG と SSH の `sysctl kern.boot.login`、console log は読まない）。実機は 5330 でユーザーが Ctrl+Space で起動し写真（WS の受け入れ）。
- **Phase**: p002 parser（host 試験、3 つの BIOS loader の build と大きさを守る）→ p003 UEFI loader・build・docs（T1 の QEMU）→（p004 BIOS、任意）→ p005 規約の全文の見直し。

### ユーザーへの判断の項目（推奨つき、design.md §11）

| ID | 問い | 推奨 |
| --- | --- | --- |
| D1 | config の形は `safe.kmsg=console`・`safe.login=console` の 2 行（許可の表で固定。`[safe]` の節と一般の override は不採用）でよいか | はい |
| D2 | config に safe の行が無くても Ctrl で `logo`・`login`・`kmsg` を落とす（組み込みの既定）でよいか | はい |
| D3 | safe mode で GOP に 640x480 を希望（無ければ現在の mode、止まらない）してよいか | はい（80 桁の console が画面中央に固定なので、1920x1080 のままだと写真で読みにくい） |
| D4 | BIOS PC/AT の Ctrl（p004）をこの WS でやるか | 後回し |
| D5 | safe のときだけ告知の後に 1 秒止めるか | はい |
| D6 | 検出は Ex protocol だけ（simple の制御文字は使わない） | はい |
| D7 | S0・S1・S2 の 3 点、待ち時間なし、主の手順は「Ctrl を押したまま Space を繰り返し叩く」でよいか | はい |
| D8 | 許可の表に無い `safe.` の行は file ごと拒否してよいか | はい |

後の選択肢（この WS ではやらない）: `safe=1` の kernel parameter と `kern.boot.safe`、許可の表への `i915.start=manual`、PC-98 の Ctrl、BIOS 用の cfg への safe の行（zedinst の admission の変更が要る）。

## レビュー

design-reviewer（2026-10-05 夜、第 1 版に対して。source は変えず、build も QEMU も流していない）の指摘と反映:

| # | 指摘（重大度） | 反映 |
| --- | --- | --- |
| 1 | 電源投入からの Ctrl の押しっぱなしは、EDK2 系の firmware では EXPOSED の設定前の修飾 key 単独の押下が捨てられ、検出できない見込み（High、推測） | S0（入口）で EXPOSED を頼む 3 点の標本に変更。主の手順を「Ctrl+Space の繰り返し」に。Q1 の PASS を押しっぱなしの証拠にしないと明記（§3.4・§9.2・§13） |
| 2 | QEMU の Q1 は 1.6 秒周期の押下では S1〜S2 の窓に入る確率が低く flaky（Medium-High） | 50 ms 周期で、screendump に kernel の text か logo が写るまで（最大 60 秒）送る。1 つの QEMU で `system_reset`。`usb-kbd` に `id` を付け `device` で送り先を指定（§9.2） |
| 3 | 「safe だけが parse に失敗する組み合わせは無い」は誤り（path の検査と長さは組み立て時だけ）。UEFI と BIOS loader が同じ file で食い違う（Medium） | 許可の表（path を含まない 2 行）と、normal mode の measure-only の空測りで長さも両 mode で判定（§2.2・§7.2、H8） |
| 4 | 一般の `safe.NAME=VALUE` の override は `safe.login=graphical` で safe boot を打ち消せ、ユーザーの定義を越える（Medium） | override を廃し、`safe.kmsg=console`・`safe.login=console` だけの許可の表に（D1・D8） |
| 5 | BIOS 用の cfg に safe の行を足すと zedinst の admission（名前の許可）が text の BIOS image を installer の source として拒む（Medium） | p003 は native UEFI の cfg だけに足す。BIOS 用は p004 で zedinst と調整（§2.3） |
| 6 | panic・fault は quiet boot でも既に reveal_fatal で見えるので、BUG-202 は console 登録前の停止・hang・reset の類。§5 の期待の書き方が違う（Medium） | §5 を停止の種類の表に書き直し、実機の判定を「最後の message が残る」に（§9.3） |
| 7 | D3 の根拠が誤り（`video_select` の希望の mode は失敗しない）。§0 の「text の config と同じ」は `video=640x480` の分だけ違う（Low-Medium） | §0 を訂正。D3 を「640x480 を希望する」に変え、80 桁の console が中央に固定される事実を根拠に（§3.6） |
| 8 | PC-98 loader の大きさの余裕が未測定（Low-Medium） | p002 の受け入れに 3 つの BIOS loader の `stage2_end` の記録を追加（§8・§10） |
| 9 | 試験の config が他の WS の `tests/` を include している（Low-Medium） | `plan/tools/guest/config-amd64-ssh.mk` を include（§9.2） |
| 10 | `safe-key.c` の「初回だけ SetState」が隠れた static になる。header が coding-style §13 と違う。boolean を guard の if に。`ConIn` の型付けは未使用（Low） | 呼び手が持つ `struct zbl_uefi_safe_key` と `open`/`sample`/`from_state` に。複数行の header。guard の if。`ConIn` は触らない（§3.3・§7.4） |
| 11 | 旧 loader + 新 cfg の行が未試験。docs の規則（`plan/` へ link しない）の明記。実機の確認を持つ Phase が無い。EXTRA_LINES の記述の誤り。`A64 PARAMS` が normal の record しか出さない。費用の見積もりが未検証（Low） | H10 を追加。p003 の受け入れに docs の規則。実機の確認を WS の受け入れの行に。§2.3 を訂正。`A64 PARAMS SAFE` を追加。費用を ms の桁・未計測に（§3.3） |

レビューが正しいと確かめた点（抜粋）: `[safe]` は旧 loader が拒否し `safe.NAME=VALUE` は通る。kernel は dotted の未知の名前を無視し `EEXIST` は既知の名前だけ。`kmsg=console`・`login=console` は受ける。token の照合は全部 token 全体。BIOS の asm の呼び出しは wrapper で不変。`quiet_boot` の再計算で `framebuffer_stage` と transition の選択が変わる。`uefi.h` の slot は layout 中立。HAL・hal.h・UAPI・handoff の変更は不要。int 16h AH=02h bit 2 は Ctrl の状態。Phase の順は妥当。

未解決（firmware 次第、実機で記録する）: 5330 の firmware が EXPOSED を受けるか、内蔵 keyboard が Ctrl を typematic で繰り返すか、BDS が `StartImage` の前に ConIn を flush するか。i915 の probe が console の login で scanout に触るか。OVMF の `ConsoleInHandle` の Ex が usb-kbd と i8042 のどちらを流すか。

## 実行の記録

- 2026-10-05 夜: 設計の担当が `bootloader/uefi/`（bootx64.c・zedbsd-config.c/.h・video.c・logo.c・transition.S・uefi.h）、`bootloader/pcat/bootzbsd.S`・`pc98/bootzbsd.S` の parser の呼び出し、`bootloader/common/logo-path.c`、`include/kern/boot.h`・`src/kern/boot.c`（未知の名前の扱い）、`src/kern/panic.c`・`src/drivers/platform/pcat/graphics/text.c`・`src/hal/amd64/bsp-pcat/cons.c`（quiet と reveal）、`platform/amd64/vmunix.mk`（cfg の生成）、`plan/tools/boot-test.sh`・`qmp.py`・`guest/`、WS013 の試験の索引を読んで design.md の第 1 版を書いた。design-reviewer のレビューを受けて第 2 版に改めた。source は変えていない。build・試験は未実施（設計だけ）。commit はしていない（Q1 が行う）。
- 残課題: ユーザーの判断 D1〜D8。p002〜p005 の Queue 化は Q1。kernel の keyboard driver が LED を自分で設定するか（`SetState` の副作用の確認、p003 で確かめる）。
