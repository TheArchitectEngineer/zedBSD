<!-- awesome-plan project=zedbsd record=ws174-p001 -->
# ws174-p001: 設計 — 起動時の Ctrl / Shift で boot の選択を変える

Parent: [WS174](../ws.md)
Status: cleared（2026-10-05 夜 Q1: 第 3 版、design-reviewer 15 件を反映、ユーザーの判断は残り無し）
Disposition: normal

## 由来

[WS174](../ws.md) のユーザーの指示。実機の 5330 で build/uat-0505g が kernel の起動の途中で panic したと見られるが、graphical boot で画面に何も出ず分からない（[BUG-202](../../bugs/BUG-202.md)）。

ユーザーの定義（2026-10-05 夜、Q1 経由）: 「safe boot optionsとはグラフィカルブートにせずカーネルのメッセージをコンソールに出力するオプションを選択することです。」→ safe boot は **graphical boot をしない（logo なし・`login=graphical` なし）** と **kernel の message を console に出す（`kmsg=quiet` なし）** の 2 つだけ。`ZEDBSD_GRAPHICAL_BOOT=n ZEDBSD_BOOT_KERNEL_MESSAGES=y` の config（`build/uat-0505g-text/zedbsd-native-uefi-graphical-n-kmsg-y.cfg`）と同じ選択。i915・ACPI を止めるなどは含めない。

## 設計

全文: [design.md](design.md)（**第 3 版**）。要点:

- **仕様（ユーザー、2026-10-05 夜）**: Ctrl = loader が kernel に渡す `kmsg=` を `console` にする（config が `kmsg=quiet` でも）、logo を落とし、GOP に 640x480 を希望（無ければ現在の mode、止まらない）。Shift = `login=` を `console` にする（sessiond が自分で終わり getty_console が始まる。sessiond・init は変えない）。両方なら両方。**boot の config の file の形式は変えない**（`safe.*` の行は無い）。変えるのは bootloader だけ。告知は出すが **待たない**（1 秒の Stall は無し）。
- **record の書き換え**（`bootloader/common/boot-override.c`、純粋な関数、host で試験）: parse 済みの parameter record の token を name の全体一致で落とし（Ctrl: `kmsg`・`logo`、Shift: `login`）、末尾に `kmsg=console`・`login=console` を足す。落としてから足すのは kernel が既知の名前の重複を `EEXIST` で拒むため。上限 3071 に入らない token は足さない（kernel の既定が console なので意味は同じ）。冪等。parser（`zedbsd-config.c`、BIOS loader と共有）と cfg の生成は触らない。
- **key の検出**（`bootloader/uefi/boot-keys.c`、第 2 版を Shift に広げて流用）: `EFI_SIMPLE_TEXT_INPUT_EX_PROTOCOL` の `ReadKeyStrokeEx` の `KeyShiftState` だけ（左右の Ctrl・Shift のどちらでも）。入口（S0）で `SetState(EXPOSED)` を頼み、S0・parse 直後（S1、video と logo の前）・kernel の読み込みの後（S2、`build_bootstrap()` の前）の 3 点で queue を最大 32 回読む。待ち時間は足さない。電源投入からの押しっぱなしで効くかは firmware 次第で未確認なので、docs の主の手順は「Ctrl（または Shift、または両方）を押したまま Space を繰り返し叩く」。
- **loader の流れ**: S1 で書き換え → `logo_named`（Ctrl なら 0）→ `video_select`（Ctrl なら 640x480 の希望、他は今のまま）→ 告知 `Boot: kernel messages (Ctrl)` / `Boot: console login (Shift)`（SetMode の後、ConOut と debug port、`A64 PARAMS OVERRIDE` に書き換え後の record）→ `show_logo` → `quiet_boot`。S2 で増えた bit があれば書き換え直し（冪等）、`quiet_boot` を再計算、logo の上に告知。video は S2 では変えない。
- **kernel・handoff・UAPI・HAL・init・sessiond は変えない**（承認の要る差分は無い。design.md §6 で確認）。
- **試験**: host 試験 O1〜O10（書き換え）・K1・K2（検出、mock の system table）。QEMU は Q1 経由で T1 に 1 件: 1 つの QEMU で `system_reset` を挟み、C0 none・C1 Ctrl・C2 Shift・C3 Ctrl+Shift・C4 late の 5 cell（key は QMP `input-send-event` で `usb-kbd id=kbd0` に 50 ms 周期、判定は screendump の PNG（logo の有無・kernel の text）と SSH の `sysctl kern.boot.login`、console log は読まない）。実機は 5330 でユーザーが Ctrl+Shift+Space で起動し写真（WS の受け入れ、「kernel の最後の message が画面に残る」）。
- **Phase**: p002 module（`boot-override.c/.h`・`boot-keys.c/.h`・`uefi.h`・build の rule、host 試験）→ p003 UEFI loader の統合・docs 4 件・QEMU の config と script・T1 の依頼 → （p004 BIOS PC/AT、後回し）→ p005 規約の全文の見直し。
- **BIOS PC/AT**: int 16h AH=02h の flag（bit 0/1 Shift、bit 2 Ctrl）で同じ `zbl_boot_override_apply()` を呼ぶ。別 Phase（p004、後回し）。PC-98 は Future Work。

### ユーザーへの判断の項目

**無し**（2026-10-05 夜に出そろった。下の 2 節）。第 2 版の D1・D2・D8 は仕様の変更で不要、D3 = Yes、D4 = 後回し、D5 = No、D6・D7 = 流用。

後の選択肢（この WS ではやらない）: `safe=1` の kernel parameter と `kern.boot.safe`、Ctrl で `i915.start=manual` を足す、PC-98 の key、S2 で検出した Ctrl の video mode の変更。

## レビュー

### 第 1 版へのレビュー（第 2 版で反映）

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

### 第 3 版へのレビュー（反映済み）

design-reviewer（2026-10-05 夜、第 3 版に対して。source は変えず、build も QEMU も流していない）の指摘と反映:

| # | 指摘（重大度） | 反映 |
| --- | --- | --- |
| 1 | QMP `input-send-event` の `device` は入力装置でなく表示の id。`kbd0` では error になり C1〜C3 が必ず FAIL（High。`plan/tools/keiland-linux/guest.py` の `display=video0` が前例） | `send-key`（既定の handler、`device` なし）に変更。検出されなければ `-device usb-kbd` を外して i8042 で 1 回だけ再試行（§3.5・§9.2） |
| 2 | `-no-reboot` の下では QMP `system_reset` も shutdown で QEMU が終わる。rw の root の hard reset も良くない（High〜Medium、推測） | cell ごとに image の複写から QEMU を起動し直す（同時に 1 つ）。`system_reset` は使わない（§9.2） |
| 3 | 一度だけ送る `ctrl` down は OVMF の xHCI の列挙と kernel の USB の reset で usb-hid の修飾の状態が消える（Medium-High、推測） | `send-key` の `ctrl`+`spc`（hold 50 ms）を周期ごとに送る（修飾 key も毎回押し直す）（§9.2） |
| 4 | S2 で `quiet_console = 0` にすると後続の約 10 行の診断が logo の上に残る（Shift だけの時）。S2 は QEMU の cell で狙えず、`bootx64.c` は host 試験の対象でもない（Medium） | S2 は `quiet_console` を触らない（告知は logo があれば debug port だけ）。S2 の部品は O6・K2、glue は review。限界を p003 に記録。S2 を外す判断は Q1（3 点は Q1 の指示で流用）（§4.2） |
| 5 | `A64 PARAMS OVERRIDE` を SetMode の前に出すと消える。GOP の直接の SetMode の後に EDK2 の ConOut が描けるかは根拠が無い。Shift だけなら logo が覆う。告知がどの場合も画面に残らない可能性（Medium、推測） | OVERRIDE を告知と同じく SetMode の後に。告知は best effort と明記し判定に使わない。C1 の PNG で観察して記録。残らなければ loader が字を描く案をユーザーに諮る。ユーザーへ知らせる事項に（§4.1・§11） |
| 6 | 「修飾 key+任意の key」は誤り: EDK2 は Shift で字が変わる印字 key の event で Shift の bit を消す見込み（Medium-Low、推測） | Space に限って書く（§3.3）。`from_state` は変えない |
| 7 | PNG の判定の基準が未定義。`read_text()` の原点に 80x30 の中央（1080 なら y=300）が無い（Low-Medium） | 基準を数値で定義（既知の語か 10 文字以上の行 3 行、中央の黒でない pixel 10 %）。script が 80x30 の中央の原点を足して `read_cells()` を呼ぶ（§9.2） |
| 8 | kernel の parser まで通す端から端の host 試験が無い（`src/kern/boot.c` は `plan/ws118/tests/host-boot-i915-test.sh` で host で compile した前例あり）（Low-Medium） | O11 を追加（§9.1） |
| 9 | p002 の build に p003 で作る config を使っている。p004 が残ると WS を完了にできない（Low） | `config-amd64-keys.mk` を p002 へ。p004 は WS の完了の時に Future Work か別 WS へ移す、と明記（§10） |
| 10 | mock の前例の file 名の誤り（`uefi-zedbsd-load-options-wrapper.c` が正）。入れ子の条件演算子（coding-style §6）。`.bss` でなく stack（Low） | 訂正（§7.3・§8・§9.1） |
| 11 | `apply` の検査と `keys == 0` の順、未知の bit、詰めた後の古い text、S2 の -1 の後始末（Low） | 検査を先に、未知の bit は無視、新しい長さから元の長さまで 0 で埋める、S1 は `fail_discovered`・S2 は `fail_kernel_load`（§2.2・§2.3・§4・§8） |
| 12 | `docs/reference/kernel-boot-parameters.md` §9 の「行ごとに 1 token」「4 経路で同じ意味」「LoadOptions は足さない」の断定が第 3 版と矛盾（Low） | docs の計画に §9 の但し書きを追加（§1・§10） |
| 13 | `usb-kbd port=2` が guest.py の `usb-net port=2` と衝突。LED の副作用は QEMU で観測できない（Low） | guest.py の割り当て（usb-net 2、usb-kbd 3）。p003 で kernel の keyboard driver の source を読んで記録（§9.2・§10） |
| 14 | EXPOSED の下の ConSplitter は key が無くても空の event で SUCCESS を返し続ける見込み。Dell の Fastboot Minimal・stuck key の警告（推測） | K2 に「常に SUCCESS で Ctrl の立った空の event」を追加、上限は必須と明記。docs の一般論の注意に（§9.1・§10） |
| 15 | 「判断は無い」だが、Ctrl+Space の手順と告知の見えない可能性はユーザーが確認していない。Ctrl だけでは i915 が console を消しうる | 判断ではなく「知らせる事項」として §11 と Q1 への報告に。手順の流用は Q1 の指示。実機の受け入れは両方押した時で判定（§9.3・§13-20） |

レビューが正しいと確かめた点（抜粋）: record の書き換えのアルゴリズム（token は 1 個の空白区切り、`name=value`、`boot0:` は値の側）、O8 の算術、冪等性、O7 の期待値。kernel は既知の名前の重複を `EEXIST`、`kmsg=console`・`login=console` を受け、`logo` は未知で無視。既定（kmsg 無し→表示、login 無し→sessiond は console）。sessiond・init・`greeter.service`（`replaces=getty_console`、引数なし）の変更は不要。loader が record から読むのは logo・kmsg=quiet・video・kernel_phys だけ。`video_select` の希望は失敗しても止まらない。640x480 に 80x30 の早期 console が入る。`quiet_boot` の使用箇所は 3 つ。UEFI の宣言（修飾 key の bit、toggle、GUID、`EFI_NOT_READY`、member の順）。handoff・HAL・UAPI の変更は不要。parser を変えないので BIOS loader は不変。

## 実行の記録

- 2026-10-05 夜: 設計の担当が `bootloader/uefi/`（bootx64.c・zedbsd-config.c/.h・video.c・logo.c・transition.S・uefi.h）、`bootloader/pcat/bootzbsd.S`・`pc98/bootzbsd.S` の parser の呼び出し、`bootloader/common/logo-path.c`、`include/kern/boot.h`・`src/kern/boot.c`（未知の名前の扱い）、`src/kern/panic.c`・`src/drivers/platform/pcat/graphics/text.c`・`src/hal/amd64/bsp-pcat/cons.c`（quiet と reveal）、`platform/amd64/vmunix.mk`（cfg の生成）、`plan/tools/boot-test.sh`・`qmp.py`・`guest/`、WS013 の試験の索引を読んで design.md の第 1 版を書いた。design-reviewer のレビューを受けて第 2 版に改めた。source は変えていない。build・試験は未実施（設計だけ）。commit はしていない（Q1 が行う）。
- 残課題: ユーザーの判断 D1〜D8。p002〜p005 の Queue 化は Q1。kernel の keyboard driver が LED を自分で設定するか（`SetState` の副作用の確認、p003 で確かめる）。
- 2026-10-05 夜（続き）: ユーザーの仕様の変更と決定（下の 2 節）を受けて、設計の担当が design.md を**第 3 版**に書き直した。読み直した source: `bootloader/uefi/bootx64.c`（全文）・`zedbsd-config.c/.h`・`video.c`・`logo.c`・`transition.S`・`include/uefi.h`、`bootloader/common/logo-path.c/.h`、`bootloader/include/boot-parameter-handoff.h`・`include/kern/boot.h`（record の struct）、`src/kern/boot.c`（`EEXIST`・`parameter_word`）、`src/kern/sysctl.c`（`kern.boot.*` の名前）、`userland/desktop/sessiond/main.c`（`kern.boot.login`）、`src/hal/amd64/bsp-pcat/cons.c`・`text.c`（quiet）、`platform/amd64/vmunix.mk`（UEFI の object と link の rule、cfg の生成）、`plan/tools/boot-test.sh`・`boot-test.py`・`qmp.py`・`guest/test-image.sh`・`guest/config-amd64-ssh.mk`、WS013 の mock の試験。第 2 版の `safe.*` の config の行・parser の mode・空測り・1 秒の Stall・`safe_configuration` を削除し、record の後処理（`bootloader/common/boot-override.c`）と Shift の検出を足した。source は変えていない。build・試験は未実施。commit はしていない。
- 2026-10-05 夜（続き）: design-reviewer の第 3 版へのレビュー（上の表、15 件）を design.md に反映した。ユーザーの判断を新たに要する指摘は無かった。
- 残課題（第 3 版）: p002・p003・p005 の Queue 化は Q1（ユーザーの実装の指示あり、下の節）。ユーザーへ知らせる事項 2 つ（design.md §11: Ctrl+Space の手順、告知は best effort）。kernel の keyboard driver が LED を自分で設定するか（`SetState` の副作用、p003 で source を読んで確かめる）。S2 を残すか外すかは Q1（design.md §4.2）。

## 2026-10-05 夜 ユーザーの仕様の変更（第 2 版を置き換える）

ユーザー:「safe.login=console これですが、おおむねグラフィックドライバでフリーズするので、コンソールログインにしてもあまりうれしくないなあ。仕様を変えましょう。Ctrlキーが押されている→カーネルメッセージがコンソールになる。デフォルトではquietだけどconsoleになる。Shiftキーが押されている→ログインがコンソールになってグラフィカルセッションが開始しない。よってブートコンフィグファイルの仕様変更は不要です。ブートローダのみ変更します。」

- **Ctrl**: loader が kernel に渡す `kmsg=` を `console` にする（config が `kmsg=quiet` でも）。
- **Shift**: loader が `login=` を `console` にする（graphical の session を始めない）。
- 両方を同時に押せば両方。boot の config の file の形式は変えない（`safe.*` の行は無し。第 2 版の D1・D2・D8 は不要になる）。変えるのは bootloader だけ。
- 第 2 版の他の部分（Ex protocol の KeyShiftState の 3 点の標本、待ちなし、告知、Ctrl+Space の手順、試験の形）は流用し、第 3 版で書き直す。logo を Ctrl の時に落とすか、640x480 を希望するか（D3）、1 秒止めるか（D5）は第 3 版で改めて判断の項目にする。

## 2026-10-05 夜 ユーザーの決定（第 3 版の判断）

- Ctrl の時に logo を落とす: **Yes**
- Ctrl の時に 640x480 を希望する: **Yes**
- 1 秒止める: **No**（告知は出すが待たない）
- BIOS（PC/AT）は第 2 版の D4 のとおり後回し、検出は Ex protocol だけ（D6）、3 点の標本・待ちなし（D7）は流用（Q1、ユーザーの変更と矛盾しない範囲）。

## 2026-10-05 夜 ユーザーの実装の指示

「OKです。ブートローダの仕様変更を実装してください。BIOSは後日でよいです。」→ 第 3 版の設計（レビューの反映の後）で UEFI の bootloader の実装（p002・p003 と p005 の規約の見直し）を Queue に入れてよい。BIOS（p004）は後日。

## 2026-10-05 夜 Q1 の技術の決定

- S2（kernel の読み込みの後の 3 点目の標本）は**外す**（QEMU で狙えず得る物が小さい。S1 で書き換えた後の S2 は書き換えの順を複雑にする）。標本は S0（入口）と S1（config の parse の直後）の 2 点。
- 第 3 版の設計で p001 を cleared にし、p002・p003・p005 を P1 に投入（ユーザーの実装の指示）。
