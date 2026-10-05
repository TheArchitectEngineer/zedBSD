# WS174 設計: 起動時の Ctrl / Shift で boot の選択を変える（第 3 版）

Parent: [ws174-p001](phase.md) / [WS174](../ws.md)
版: 第 3 版（2026-10-05 夜。design-reviewer の第 3 版へのレビュー（phase.md の「レビュー」節）を反映済み。ユーザーの仕様の変更「Ctrl = kmsg を console、Shift = login を console、config の形式は変えず bootloader だけ」と、同夜の決定「logo を落とす: Yes、640x480 を希望: Yes、1 秒止める: No」を反映。第 2 版の `safe.*` の config の行・parser の mode・空測りは全部削除。第 2 版の key の検出（Ex protocol、S0〜S2、待ちなし、Ctrl+Space の手順）と試験の形は流用）

## 0. 範囲とユーザーの仕様

- 由来: ユーザー（2026-10-05 夜）「ブート時にctrlキーが推されていたらsafe boot optionsの設定に切り替えるように…設計だけできますか？」（[BUG-202](../../bugs/BUG-202.md): 実機 5330 の graphical boot で kernel の起動の途中の停止が画面で読めない）。
- 仕様の変更（同夜、ユーザー）「Ctrlキーが押されている→カーネルメッセージがコンソールになる。デフォルトではquietだけどconsoleになる。Shiftキーが押されている→ログインがコンソールになってグラフィカルセッションが開始しない。よってブートコンフィグファイルの仕様変更は不要です。ブートローダのみ変更します。」
- 決定（同夜、ユーザー）: Ctrl の時に logo を落とす **Yes**。Ctrl の時に GOP に 640x480 を希望する **Yes**（無ければ現在の mode のまま、止まらない）。告知の後に 1 秒止める **No**（告知は出すが待たない）。

従って UEFI loader の振る舞いは次のとおり。

| 押されている key | loader が kernel に渡す parameter record の変更 | 画面 |
| --- | --- | --- |
| Ctrl（左右どちらでも） | `kmsg=` の token を全部落とし `kmsg=console` を足す。`logo=` の token を落とす（logo を描かない） | GOP に 640x480 を希望。告知 `Boot: kernel messages (Ctrl)` |
| Shift（左右どちらでも） | `login=` の token を全部落とし `login=console` を足す | 告知 `Boot: console login (Shift)`（logo は config のまま） |
| 両方 | 両方 | 両方の告知 |
| 無し | 変更なし（record は byte 単位で今と同じ） | 今と同じ。待ち時間も足さない |

- **boot の config の file（`zedbsd.cfg`）の形式は変えない。** `safe.*` の行は無い。build の cfg の生成（`platform/amd64/vmunix.mk`）も変えない。
- **変えるのは UEFI loader（`bootloader/uefi/`）と、loader 共通の小さな module（`bootloader/common/`）だけ。** kernel・HAL・UAPI・handoff・init・sessiond は変えない（§6 で確認）。
- BIOS（PC/AT）は後回し（第 2 版の D4 のまま。§3.7）。PC-98 は Future Work。
- Shift の効き方は既存の仕組みのまま: kernel は `login=console` を `sysctl kern.boot.login` として報告するだけで（`src/kern/boot.c` 390〜406 行が `console` を受ける）、`sessiond`（init の service `greeter`、`getty_console` を置き換える）が `kern.boot.login` を読み `graphical` でなければ `SESSIOND CONSOLE reason=boot-parameters` を記録して終わり（`userland/desktop/sessiond/main.c` 132〜140 行・363〜387 行）、init が `getty_console` を始める。**sessiond と init の変更は無い。**

## 1. 責務と境界

| 構成要素 | 変える | 変えない |
| --- | --- | --- |
| loader 共通 `bootloader/common/boot-override.c/.h`（新 file） | parameter record の token の書き換え（純粋な関数、host で試験） | — |
| UEFI loader `bootloader/uefi/boot-keys.c/.h`（新 file） | Ex protocol による Ctrl・Shift の検出（open / sample / from_state） | — |
| UEFI loader `bootloader/uefi/bootx64.c` | S0・S1・S2 の標本、record の書き換えの適用、640x480 の希望、告知、`quiet_boot` の再計算、`A64 PARAMS OVERRIDE` | handoff の layout（ZBL6 v7）、memory map、ELF の load、volume の発見、config の parse |
| UEFI の宣言 `bootloader/uefi/include/uefi.h` | Simple Text Input Ex protocol の型と GUID、`EFI_NOT_READY`、修飾 key の bit | 他（`ConIn` は使わないので型を付けない。`Stall` も使わない） |
| build `platform/amd64/vmunix.mk` | `boot-override.o`・`boot-keys.o` の rule と `BOOTX64.EFI` の link | cfg の生成（`AMD64_NATIVE_UEFI_ZEDBSD_CONFIG`・`AMD64_BIOS_ZEDBSD_CONFIG`）、image の形 |
| parser `bootloader/uefi/zedbsd-config.c/.h` | **変えない**（3 つの BIOS loader と共有。object の大きさも不変） | — |
| BIOS loader `bootloader/pcat/bootzbsd.S` | （別 Phase p004、後回し）int 16h AH=02h の flag で同じ `zbl_boot_override_apply()` を呼ぶ | — |
| kernel・HAL・UAPI・handoff・init・sessiond | **変えない** | — |
| docs | `docs/reference/kernel-boot-parameters.md`（§7c を新設、§7a・7b に 1 行ずつ、§9 の「行ごとに 1 token」「4 経路で同じ意味」「LoadOptions は足さない」の断定に key の書き換えの但し書き）、`bootloader/uefi/README.md`、`docs/howto/boot-and-storage.md`（Failure diagnosis）、`bootloader/README.md`（BIOS loader は key を読まない旨）。**`docs/` から `plan/` と Bug へは link しない**（docs は目標の設計を断定で書く） | — |

kernel は key を知らない。loader が渡す record の token が違うだけで、kernel と sessiond の既存の動作が console の boot・console の login になる。

## 2. parameter record の書き換え（`bootloader/common/boot-override.c`）

### 2.1 前提（事実）

- record は `struct kern_boot_parameter_record`（`include/kern/boot.h` 183 行: header 16 byte + `text[3072]`、`length` は NUL を含まない長さ、上限 `KERN_BOOT_PARAMETERS_TEXT_MAX` = 3071）。loader の parser が作る text は **1 個の空白で区切った token の列**で、先頭・末尾に空白は無く、空白が連続しない（`builder_separator()`・`builder_line()`、`zedbsd-config.c` 288〜336 行）。各 token は config の 1 行そのもの（`kernel=` を除く）か、合成した `boot0=UUID=…` か、`boot0:` で修飾した行。config の行は必ず `=` を持ち、name も value も空でない（`next_line()` 116〜118 行）ので、**どの token も `name=value` の形**で、name は最初の `=` までである（value の中の `=`、例 `rootpart=PARTLABEL=zedBSD-root`、は name に入らない）。
- kernel の parser は既知の name の重複を `EEXIST` で拒む（`src/kern/boot.c` 274〜277 行）。従って `kmsg=console` を**足すだけ**では、config に `kmsg=quiet` があると kernel が起動を止める。既存の token を**落としてから**足す。
- kernel は `kmsg=` の無い record を `console`、`login=` の無い record を console の login として扱う（`docs/reference/kernel-boot-parameters.md` §7a・7b。`sessiond` は `kern.boot.login` が `graphical` 以外なら console、`main.c` 380〜383 行）。
- loader 自身が record から読む token: `logo=`（`zbl_logo_path()`、`bootloader/common/logo-path.c`）、`kmsg=quiet`（`zbl_parameter_present()`）、`video=`（`video.c`）、`kernel_phys=`（`bootx64.c` 930 行）。書き換えは `logo`・`kmsg`・`login` の name だけに触れるので、`video=`・`kernel_phys=` の読み方は変わらない。

### 2.2 interface

```c
/* bootloader/common/boot-override.h */
#define ZBL_BOOT_OVERRIDE_KMSG	0x1U	/* Ctrl: kmsg=console and no logo */
#define ZBL_BOOT_OVERRIDE_LOGIN	0x2U	/* Shift: login=console */

/*
 * Rewrites an assembled parameter record for the boot keys.  Returns -1
 * without touching a record that is not a terminated text within
 * KERN_BOOT_PARAMETERS_TEXT_MAX; otherwise returns 0, leaving the record
 * alone when no known key bit is set (unknown bits are ignored) and
 * rewriting it when one is.  The bytes after the new terminator up to the
 * old length are zeroed.  Applying the same keys twice gives the same record.
 */
int zbl_boot_override_apply(struct kern_boot_parameter_record *record, unsigned keys);
```

依存は `bootloader/include/boot-parameter-handoff.h`（→ `include/kern/boot.h`）と `<stddef.h>` だけ。UEFI の型に依存しないので host で compile でき、p004 の BIOS loader でも i386 で compile して使える（`common-logo-path.i386.o` と同じ形）。

### 2.3 手順

1. 検査（最初に行う）: `record->length > KERN_BOOT_PARAMETERS_TEXT_MAX` か `record->text[record->length] != '\0'` なら -1（parser が作った record では起きない。壊れた record に書き込まないための guard）。次に `keys & (KMSG | LOGIN)` が 0 なら 0（知らない bit は無視する）。
2. 落とす name の集合: `keys & KMSG` なら `kmsg` と `logo`、`keys & LOGIN` なら `login`。
3. 1 回の走査で詰める: token ごとに name（先頭から最初の `=` の手前。`=` が無い token は name 無しとして残す）を集合と**全体一致**で比べ、一致すれば token とその区切りの空白を飛ばし、残す token は書き込み位置へ写す（同じ buffer の前へ写すだけなので重なりは安全）。写した後の text は 1 個の空白区切り・先頭末尾の空白なしを保つ（最初に残した token の前には空白を置かない）。
4. 足す: `keys & KMSG` なら `kmsg=console`、`keys & LOGIN` なら `login=console` を、この順で、空白の区切りを挟んで末尾に足す。**上限 3071 に収まらない時はその token を足さずに飛ばす**（kernel の既定が console なので意味は同じ。自己記述が無くなるだけ。O8 で確かめる。boot は止めない）。
5. `record->text[length] = '\0'`、新しい長さから元の長さまでの byte を 0 で埋め（詰めた後に残る古い text を消す。`build_bootstrap()` は `sizeof` で写すので handoff にも残り、HAL は `length+1` までしか写さないが、決定性と O5・O6 の「同じ」の定義のため）、`record->length = length`。`magic`・`version`・`size`・`flags`・`reserved` は触らない。

結果の例（build の既定の graphical の cfg、`boot0=UUID=XXXX-XXXX rootpart=PARTLABEL=zedBSD-root swap0=PARTLABEL=zedBSD-swap logo=logo.ppm login=graphical kmsg=quiet`）:

| keys | record |
| --- | --- |
| KMSG（Ctrl） | `boot0=… rootpart=… swap0=… login=graphical kmsg=console` |
| LOGIN（Shift） | `boot0=… rootpart=… swap0=… logo=logo.ppm kmsg=quiet login=console` |
| 両方 | `boot0=… rootpart=… swap0=… kmsg=console login=console` |

text の cfg（`ZEDBSD_GRAPHICAL_BOOT=n`、logo/login/kmsg の行なし）に両方: `… video=640x480 kmsg=console login=console`（足すだけ。boot は今と同じ意味）。Guardrail の GPU 開発の config（`ZEDBSD_BOOT_EXTRA_LINES="display=edp login=graphical"`）に Shift: `display=edp` は残り `login=graphical` が `login=console` に替わる（由来を問わず name で判定）。

### 2.4 選ばなかった案

- **parser に mode を足す（第 2 版）**: config の形式を変えないなら parser を触る理由が無い。parser は 3 つの BIOS loader と共有で、大きさの制約（PC-98 `stage2.ld` の `0x8000`）がある。record の後処理なら parser の object は byte 単位で不変。
- **kernel に `safe=1` や handoff の flag を渡して kernel 側で読み替える**: ユーザーの「ブートローダのみ変更」に反する。kernel・handoff・UAPI の変更が要る。
- **足すだけ（落とさない）**: kernel が `EEXIST` で止まる（§2.1）。
- **`kmsg=quiet` の token だけを値まで一致で落とす（name でなく）**: 不正な値（`kmsg=bogus`）の config は normal の boot で kernel が止める物で、Ctrl の時だけ通す理由も止める理由も無い。name で落とす方が規則が単純で、結果は常に `kmsg=console` 1 個になる。config の重複（`kmsg=` が 2 行、kernel が拒む）も Ctrl の時だけ 1 個に畳まれるが、不正な config の振る舞いの差であり document で足りる。

## 3. UEFI loader の key の検出（`bootloader/uefi/boot-keys.c`）

第 2 版の §3 を Shift に広げて流用する。

### 3.1 firmware の口（`uefi.h` に足す宣言）

UEFI 仕様 2.x の事実（GUID・layout・定数の値だけを自分の書き方で書く。仕様書・EDK2 の文は写さない）:

```c
typedef struct { uint16_t ScanCode; CHAR16 UnicodeChar; } EFI_INPUT_KEY;	/* 4 byte */
typedef struct { UINT32 KeyShiftState; uint8_t KeyToggleState; } EFI_KEY_STATE;
typedef struct { EFI_INPUT_KEY Key; EFI_KEY_STATE KeyState; } EFI_KEY_DATA;

typedef struct efi_simple_text_input_ex_protocol {
	EFI_STATUS (EFIAPI *Reset)(struct efi_simple_text_input_ex_protocol *, BOOLEAN);
	EFI_STATUS (EFIAPI *ReadKeyStrokeEx)(struct efi_simple_text_input_ex_protocol *, EFI_KEY_DATA *);
	EFI_EVENT WaitForKeyEx;
	EFI_STATUS (EFIAPI *SetState)(struct efi_simple_text_input_ex_protocol *, uint8_t *);
	void *RegisterKeyNotify;
	void *UnregisterKeyNotify;
} EFI_SIMPLE_TEXT_INPUT_EX_PROTOCOL;

#define EFI_NOT_READY			(0x8000000000000006ULL)
#define EFI_SHIFT_STATE_VALID		0x80000000U
#define EFI_RIGHT_SHIFT_PRESSED		0x00000001U
#define EFI_LEFT_SHIFT_PRESSED		0x00000002U
#define EFI_RIGHT_CONTROL_PRESSED	0x00000004U
#define EFI_LEFT_CONTROL_PRESSED	0x00000008U
#define EFI_TOGGLE_STATE_VALID		0x80U
#define EFI_KEY_STATE_EXPOSED		0x40U
/* EFI_SIMPLE_TEXT_INPUT_EX_PROTOCOL_GUID dd9e7534-7762-4698-8c14-f58517a625aa */
```

`EFI_SYSTEM_TABLE`・`EFI_BOOT_SERVICES` の slot は変えない（`ConsoleInHandle` は既に `EFI_HANDLE`、`HandleProtocol`・`LocateProtocol` は型付き）。`Stall` は使わないので型を付けない（1 秒の停止は No）。

### 3.2 方法の選択（第 2 版の D6 のまま）

`EFI_SIMPLE_TEXT_INPUT_EX_PROTOCOL.ReadKeyStrokeEx` の `KeyState.KeyShiftState` だけを使う。simple の `ReadKeyStroke` の制御文字（Ctrl+文字）は曖昧で、Shift は文字の大小でしか分からず誤検出するので使わない（退避にもしない）。Ex の無い firmware（UEFI 2.1 より前）では key の機能は無い、と document する。

### 3.3 interface と手順

```c
/* bootloader/uefi/boot-keys.h */
struct zbl_uefi_boot_keys {
	EFI_SIMPLE_TEXT_INPUT_EX_PROTOCOL *input;	/* 0: the firmware has no Ex protocol */
	int exposed;					/* SetState(EXPOSED) succeeded */
	unsigned held;					/* ZBL_BOOT_OVERRIDE_* bits seen so far */
};

/* Finds the protocol and asks for exposed modifier keys.  Never stops the boot. */
void zbl_uefi_boot_keys_open(struct zbl_uefi_boot_keys *keys, EFI_SYSTEM_TABLE *system);
/* Drains the key queue.  Returns every bit seen in this and earlier samples. */
unsigned zbl_uefi_boot_keys_sample(struct zbl_uefi_boot_keys *keys);
/* The bits of one key event.  Pure; the host test K1's subject. */
unsigned zbl_uefi_boot_keys_from_state(const EFI_KEY_DATA *data);
```

1. `open`: `system->ConsoleInHandle` が 0 でなければ `boot->HandleProtocol(ConsoleInHandle, &EX_GUID, &input)`、失敗なら `boot->LocateProtocol(&EX_GUID, 0, &input)`。どちらも失敗なら `input = 0`。得られたら `input->SetState(input, &toggle)`（`toggle = EFI_TOGGLE_STATE_VALID | EFI_KEY_STATE_EXPOSED`）。失敗は無視（`exposed = 0`: 修飾 key 単独の押下は読めず、修飾 key+他の key だけが効く）。副作用: `SetState` は NumLock・CapsLock・ScrollLock の bit も書くので firmware の LED の状態が消える（起動時は NumLock off が普通。kernel の keyboard driver が自分で LED を設定するかは**未確認**、p003 で確かめる）。費用は LED の更新（USB の control transfer か PS/2 の command）で ms の桁と見込む（計っていない）。
2. `sample`: `input` が 0 なら `held` を返す。`ReadKeyStrokeEx` を **最大 32 回**、`EFI_NOT_READY`（または他の失敗）まで繰り返し、各 `EFI_KEY_DATA` の `from_state()` の bit を `held` に足す。上限は、成功を返し続ける壊れた firmware で止まらないため。
3. `from_state`: guard の `if`: `KeyShiftState & EFI_SHIFT_STATE_VALID` が 0 なら 0。それ以外は、Ctrl（左右）の bit があれば `ZBL_BOOT_OVERRIDE_KMSG`、Shift（左右）の bit があれば `ZBL_BOOT_OVERRIDE_LOGIN` を立てて返す。`Key` の中身（空でも文字でも）は見ない。従って **修飾 key 単独（EXPOSED が効く firmware）も、修飾 key+Space も**効く。「任意の key」とは書かない: EDK2 系の keyboard driver は Shift で字が変わる印字 key（英字・数字・記号）の event では `KeyShiftState` から Shift の bit を消す（**推測、EDK2 の記憶**）ので、Shift+英字は検出できない見込み。Space はこの属性を持たないと見込む。docs・K1 の説明は Space で書く。
4. 読んだ event は捨てる（kernel の keyboard driver は自分で初期化するので、loader の間に打たれた key が kernel に届かないのは今と同じ）。

### 3.4 いつ標本を取るか（起動を遅らせない。第 2 版の D7 のまま）

待ち時間は足さない。loader の実行の 3 点で `sample()` する。

- **S0**: `efi_main` の入口、`A64 UEFI ENTRY` の直後（GOP の前）。`open`（EXPOSED をできるだけ早く頼む）と最初の `sample`。
- **S1**: config を読んで parse した直後、`zbl_logo_path()`・`zbl_uefi_video_select()`・`show_logo()` の**前**（`bootx64.c` 1446 行の前）。ここまでに見た bit を record に適用する（§4.1）。
- **S2**: kernel の ELF を読み終えて volume を閉じた直後、`build_bootstrap()` の**前**（1531〜1543 行の間）。S1 の後に増えた bit があれば record にもう一度適用し（冪等）、`quiet_boot` を再計算し、告知を出す。video と logo は既に決まっているのでそのまま（Ctrl なら kernel の console が `kmsg=console` で画面を黒く消して描く、`src/hal/amd64/bsp-pcat/cons.c` 387〜391 行。640x480 の希望は S1 で決まった時だけ）。

**電源投入からの押しっぱなしだけで検出できるかは firmware 次第で未確認**（EDK2 系の keyboard driver は EXPOSED の設定前の修飾 key 単独の押下を event にせず、押しっぱなしを自動で繰り返さない、と推測。記憶に基づく）。従って docs の主の手順は「電源を入れたら **Ctrl（または Shift、または両方）を押したまま、Space を繰り返し叩く**（zedBSD の logo が出るか、kernel の message が流れ始めるまで）」。修飾 key+Space の event は EXPOSED の有無によらず queue に入り、S0〜S2 のどれかで読まれる。修飾 key 単独で効く firmware があればそれは firmware の性質として記録する（§9.3）。

### 3.5 firmware の癖への備え（第 2 版のまま、Shift を含める）

- USB keyboard が loader の時点で未初期化: BDS が console を connect してから loader を起動するのが普通で、`ConsoleInHandle` の Ex は ConSplitter 経由。無ければ `input = 0` で普通に起動する。
- BDS や hotkey の handler が `StartImage` の前に ConIn を flush するかは firmware 次第で**未確認**。S0〜S2 の窓に打てば読める。
- 誤検出の害: Ctrl なら text の boot、Shift なら console の login になるだけ。告知で分かる。
- Shift は firmware の hotkey（例: 一部の firmware の boot menu）と重なる可能性があるが、loader が動いている時点では firmware の hotkey の窓は終わっている。POST の間に押しっぱなしにする手順は書かない（手順は「loader が動く間に打つ」）。
- QEMU/OVMF（`boot-test.sh`・`guest.py` は `-device usb-kbd` を xHCI に付ける。q35 の i8042 もある）: `ConsoleInHandle` の Ex がどちらの keyboard の event を流すかは**未確認**。QMP の `send-key`・`input-send-event` の `device` は入力装置でなく**表示**の装置 id なので、key の送り先を usb-kbd と指定する手は無い（`plan/tools/keiland-linux/guest.py` 67・93 行の `display=video0` の使い方が前例）。試験は `send-key`（既定の handler）で送り、どちらの keyboard に届いても OVMF の ConSplitter が Ex に流す前提で、検出されなければ `-device usb-kbd` を外して i8042 だけにして 1 回だけ再試行する（§9.2）。

### 3.6 Ctrl の時の video mode（ユーザーの決定: Yes）

`zbl_uefi_video_select()` の第 5・6 引数は「希望」で、firmware に無くても現在の mode のまま成功する（`video.c` 45〜53 行「only a wish」）。`video=` の明示だけが「必ず満たす」要求で、失敗で boot が止まる。S1 で Ctrl を見たら **640x480 を希望**として渡す（`logo=` は落ちているので 1920x1080 の希望は出ない。`video=` の行があればそちらが勝つ）。理由: HAL の早期 console は 80 桁の固定の文字の面を framebuffer の中央に置く（`cons.c` 364〜366 行）ので、1920x1080 のままだと文字が中央に小さく写り、写真で読みにくい。640x480 なら text の image と同じ見え方になる。希望なので無い firmware では現在の mode のまま、boot は止まらない。Shift だけの時は今と同じ（logo があれば 1920x1080 の希望）。

### 3.7 BIOS（PC/AT）の経路（後回し、p004）

`bootloader/pcat/bootzbsd.S` は real mode で `load_kern_configuration` から parser を呼ぶ（922〜935 行、`.code16gcc`）。key は **int 16h AH=02h** の AL（bit 0 = 右 Shift、bit 1 = 左 Shift、bit 2 = Ctrl が押されている。BIOS が BDA 0040:0017 に押下中の状態を保つので、押しっぱなしで読める）。parser の後に `zbl_boot_override_apply(&record, keys)` を呼び（`boot-override.c` を `common-logo-path.i386.o` と同じ形で i386 に compile し、helper の object に加える）、`puts` で告知。amd64 BIOS（同じ `BOOTZBSD.EXE`）も同じ。PC-98 は `int 18h AH=02h` の key 状態の bitmap で読めるはずだが（**bit の位置は未確認**）、PC-98 には graphical boot が無く動機が薄い。p004 は object の大きさ（3 つの loader の `stage2_end` と ld の上限）を記録する。

## 4. loader の流れ（`bootx64.c`）と告知

### 4.1 S1 の順序

```text
load_selected_configuration()          -- 今のまま。A64 PARAMS に parse した record
S1: held = zbl_uefi_boot_keys_sample()
    apply_override(held)               -- zbl_boot_override_apply(&configuration.parameter_record, held)
                                          -1 なら fail_discovered("Override parameters", EFI_INVALID_PARAMETER)（volume を閉じて止める、今の S1 の失敗と同じ形）
logo_named = zbl_logo_path(record)     -- Ctrl なら logo= が落ちているので 0
zbl_uefi_video_select(..., wish)       -- wish: Ctrl なら 640x480、それ以外は logo_named なら 1920x1080、無ければ 0（if/else で選ぶ）
framebuffer_from_gop()                 -- 今のまま
notice(held)                           -- SetMode の後に出す（SetMode は画面を消す）。Ctrl: "Boot: kernel messages (Ctrl)\n"、Shift: "Boot: console login (Shift)\n"、
                                          続けて A64 PARAMS OVERRIDE <書き換え後の record>
show_logo()                            -- Shift だけなら今のまま logo が出て告知を覆う（待たない、ユーザーの決定）
quiet_boot = zbl_parameter_present(record, "kmsg=quiet")  -- 書き換え後の record を読むので Ctrl なら 0
```

- 告知と `A64 PARAMS OVERRIDE` は `console_ascii()`（ConOut と debug port 0xe9）。S1 では `quiet_console` は 0 なので ConOut に向けて出す。**画面に残るかは best effort**: (a) Ctrl で GOP を直接 `SetMode` した後に firmware の ConOut（EDK2 の GraphicsConsole）が新しい mode で描けるかは**未確認**（text の mode の配置を古い解像度のまま持ち、小さい mode では Blt が失敗して何も描かない可能性がある、**推測**。既存の text image の `video=640x480` の後の `A64 …` の行が画面に出るかの記録も無い）。(b) Ctrl なら kernel の console が数百 ms 後に画面を消す。(c) Shift だけなら logo が覆う。従って告知は検出の確認の補助で、**判定は告知でなく kernel の message（Ctrl）と `kern.boot.login`（Shift）で行う**。p003 は C1 の PNG に告知が写るかを**記録**し（判定ではない）、写らなければ loader が framebuffer に自分で字を描く案などをユーザーに諮る（この WS の範囲外。font を持たない loader には小さくない追加）。debug port には常に出る。

### 4.2 S2

```text
close_file_pair(kernel, root); "A64 UEFI ELF"
S2: held = zbl_uefi_boot_keys_sample()
    if (held != applied) {
        apply_override(held)              -- 冪等。kernel_phys= は触らないので place_kernel の結果はそのまま
                                          -- -1 なら fail_kernel_load(..., 0, "Override parameters", EFI_INVALID_PARAMETER, kernel_address, kernel_pages, 1)
                                          --（確保済みの kernel の page を返す、1528 行と同じ形。parser の record では起きない）
        quiet_boot = zbl_parameter_present(record, "kmsg=quiet")
        notice(held)                      -- quiet_console は触らない: logo が出ていれば debug port だけ、出ていなければ ConOut にも
    }
build_bootstrap(..., &configuration.parameter_record, ...)   -- 書き換え後の record を写す
```

- `quiet_boot` を読むのは 1673・1686・1691 行だけなので再計算で足りる（stage の block と `zbl_transition_start` の選択が変わる）。
- S2 は `quiet_console` を変えない。変えると、後に続く約 10 行の診断（`A64 RSDP`〜`A64 UEFI READY`、1554〜1650 行）が logo の上に描かれ、Shift だけの quiet boot では splash の間ずっと残る（レビューの指摘）。遅い Ctrl の告知は debug port だけになるが、kernel の console が画面を消して message を描くので検出は画面で分かる。遅い Shift だけは画面では分からず、`kern.boot.login` と debug port で分かる。
- S2 で Ctrl を見ても video mode は変えない（SetMode は logo を消し、framebuffer の mapping を作り直す必要がある。遅い Ctrl は logo の mode のまま kernel の console が描く。文字が小さいだけで読める）。
- **S2 の経路は QEMU の cell で狙えない**（kernel の ELF は約 2.4 MB で S1〜S2 の窓は短く、外から時刻を合わせる手段が無い）。S2 の部品（`apply` の冪等性 = O6、`sample` の累積 = K2）は host で確かめ、S2 の 4 行の glue は p003 の review と p005 の規約の見直しで見る。この限界は p003 の記録に書く。S2 を残すのは Q1 の指示（第 2 版の 3 点を流用）による。得る物（S1 の後の短い窓）は小さいので、実装で複雑になるなら S2 を外す判断は Q1 に委ねる。

### 4.3 kernel・init への伝達

- kernel は record の `kmsg=console`・`login=console` を今の parser で受ける。`sysctl kern.boot.login` が `console` を返す。`sessiond` は終わり、init が `getty_console` を始める（§0）。
- **新しい kernel parameter・handoff の flag・sysctl は足さない。**

## 5. 画面に残る物（BUG-202 の期待。第 2 版の §5 を流用）

事実（source）:

- `kmsg=console` のとき HAL の早期 console は framebuffer を黒く消して描く（`cons.c` 376〜398 行。`kmsg=quiet` の時だけ splash を始めて `console_suspended = 1`）。kernel の text console は `text_hidden = kern_log_quiet()` = 0 で最初から表示（`src/drivers/platform/pcat/graphics/text.c` 542 行）。
- panic と supervisor の fault は quiet な graphical boot でも `kern_text_reveal_fatal()` で console を出す（BUG-158 の直し）。

従って BUG-202 の「何も出ない」停止は、text console の登録前の停止（HAL の早期 console が `console_suspended = 1` の間）・panic しない hang・reset のどれかと見られ、Ctrl の boot が直すのは「kernel の最後の message が読めない」こと。**実機の判定は「kernel の最後の message が画面に残る」**（§9.3）。残る限界（document に書く、変えない）: HAL の早期 console の前の停止はどの設定でも見えない。Ctrl だけ（Shift なし）なら `login=graphical` のままで greeter が始まるので、i915 の probe・greeter の表示で console が消えるのは今までどおり。両方押せば console の login。

## 6. handoff・UAPI・HAL の変更の要否（確認）

- handoff: `build_bootstrap()` は record を `sizeof` で写す（1373 行）。text が違うだけで ZBL6 v7 の layout は不変。flag も足さない。
- UAPI: `kern.boot.login` は既存。新しい sysctl は無い。
- HAL: `src/hal/x86/boot-parameters.c` は record を写すだけ。`cons.c` は `kmsg=quiet` の token を見るだけ。**`include/hal/hal.h` も `src/hal/` も変えない。承認の要る差分は無い。**
- kernel・init・sessiond: 変えない（§0）。

## 7. 内部 interface と build（実装 Phase への指示）

### 7.1 `bootloader/common/boot-override.c/.h`

§2.2 の macro と関数。内部は token の走査の static 関数（`override_token_end()`・`override_name_dropped()`）と詰めの loop。`plan/coding-style.md` の形（複数行の header §13、forward declaration の block、意味の段落の comment、guard の `if`、`for` の外の宣言）。`logo-path.c` の `zbl_parameter_present()` と同じ token の歩き方。

### 7.2 `bootloader/uefi/boot-keys.c/.h`

§3.3 の struct と 3 関数。`uefi.h` と `bootloader/common/boot-override.h`（bit の macro）だけに依存し、`bootx64.c` の static には触らない。file scope の static は置かない（host 試験で状態を作り直せるように）。

### 7.3 `bootloader/uefi/bootx64.c`

- `struct loader_context` に `struct zbl_uefi_boot_keys keys;` と `unsigned override_applied;`。
- static 関数 `apply_override(struct loader_context *, struct zbl_uefi_kern_config *, unsigned held)`（書き換え・`A64 PARAMS OVERRIDE`・`override_applied` の更新）と `notice_override(struct loader_context *, unsigned held)`（告知の 2 行）。
- S0・S1・S2 を §3.4・§4 の位置に。`zbl_uefi_video_select()` の希望の引数は `wish_width`・`wish_height` の local を if / else if / else で選ぶ（Ctrl なら 640x480、else logo_named なら 1920x1080、else 0。入れ子の条件演算子は coding-style §6 に反するので使わない）。`#define TEXT_MODE_WIDTH 640U`・`TEXT_MODE_HEIGHT 480U` を `SPLASH_MODE_*` の隣に。
- `configuration` は今のまま 1 つ（第 2 版の `safe_configuration` は不要。書き換えは in place）。

### 7.4 build の rule（`platform/amd64/vmunix.mk`）

- `$(BUILD)/uefi/common-boot-override.o: bootloader/common/boot-override.c bootloader/common/boot-override.h include/kern/boot.h`（`common-logo-path.o` の rule と同じ形、`-I.`）。
- `$(BUILD)/uefi/boot-keys.o: $(UEFI_LOADER)/boot-keys.c $(UEFI_LOADER)/boot-keys.h $(UEFI_LOADER)/include/uefi.h bootloader/common/boot-override.h`。
- `$(BUILD)/uefi/bootx64.o` の依存に `boot-keys.h`・`boot-override.h` を足し、`$(BUILD)/uefi/BOOTX64.EFI`（618 行）の object の列に 2 つを足す。
- BIOS の helper（`AMD64_BOOTZBSD_HELPERS` 等）は p004 まで触らない。cfg の生成の rule は触らない。

## 8. 失敗と回復・並行性・資源

- key の検出の失敗はどれも boot を止めない: protocol が無い・`SetState` 失敗・`ReadKeyStrokeEx` の error → key 無しの boot。
- `zbl_boot_override_apply()` の -1（record の不整合）は parser が作った record では起きない。起きたら S1 では `fail_discovered()`、S2 では `fail_kernel_load()` で、その時点の資源の後始末の慣例どおりに見える形で止める（host 試験 O9 が -1 を確かめる）。足す token が入らない時は飛ばすだけ（O8）。
- 並行性: loader は single thread、TPL は application。`WaitForEvent` は使わない（待たない）。
- 資源: 追加の pool allocation は無い。record は in place。増えるのは `efi_main` の stack の `struct loader_context` の member（`struct zbl_uefi_boot_keys` 24 byte と `unsigned`）だけ。
- BIOS loader: p002・p003 は BIOS の object を変えないので大きさは不変（p002 の受け入れで `git diff --stat` に `bootloader/pcat`・`pc98`・`bios` が無いことを記録）。p004 で `boot-override.c` を i386 に加える時に 3 つの loader の `stage2_end` を前後で記録する。
- 割込み・DMA: 無関係。

## 9. 試験計画

判定は **QMP の screendump の PNG と、guest への SSH の問い合わせ**で行い、QEMU の console log・serial log では判定しない。実装の担当は QEMU を起動せず、Q1 経由で T1 に依頼する。

### 9.1 host 試験（`plan/ws174/tests/`、p002 の受け入れ）

`run-boot-override-host-test.sh` が `bootloader/common/boot-override.c` と `boot-override-host-test.c` を host の cc で compile して流す（通常と `-fsanitize=address,undefined` の 2 回。`-I.` で `include/kern/boot.h` を読む）。`run-boot-keys-host-test.sh` が `bootloader/uefi/boot-keys.c` と `boot-keys-host-test.c` を同じく流す（mock の system table。`EFIAPI`（`ms_abi`）の関数 pointer を host で呼ぶ手法は WS013 の `plan/ws013/tests/uefi-zedbsd-load-options-wrapper.c` が前例）。`run-boot-override-kernel-host-test.sh`（O11）は `src/kern/boot.c` を host の clang で compile する（前例 `plan/ws118/tests/host-boot-i915-test.sh`）。

| 番号 | 入力 | 期待 |
| --- | --- | --- |
| O1 Ctrl | graphical の record（§2.3 の例） | `… login=graphical kmsg=console`（`logo=`・`kmsg=quiet` が落ち、末尾に `kmsg=console`） |
| O2 Shift | 同上 | `… logo=logo.ppm kmsg=quiet login=console` |
| O3 両方 | 同上 | `… kmsg=console login=console` |
| O4 text | text の record（`… video=640x480`、logo/login/kmsg なし）に両方 | `… video=640x480 kmsg=console login=console` |
| O5 無し | keys=0 | 返り値 0、record は byte 単位で同じ（header を含む） |
| O6 冪等 | O3 の結果にもう一度両方 | O3 と同じ |
| O7 名前 | `display=edp login=graphical logout=x login2=y rootpart=PARTLABEL=kmsg=quiet` に両方 | `display=edp logout=x login2=y rootpart=PARTLABEL=kmsg=quiet kmsg=console login=console`（name の全体一致。value の中の `kmsg=` は触らない） |
| O8 境界 | 長さ 3071 で `kmsg=quiet` を含む record に Ctrl → 落として 3060、`kmsg=console` は 3073 で入らない。長さ 3069 なら 3071 で入る | 前者は `kmsg=` 無しで `length` 3060、返り値 0。後者は末尾に `kmsg=console`、`length` 3071 |
| O9 不正 | `length` 3072、`text[length] != 0` | -1、record は触られない |
| O10 先頭・末尾 | `kmsg=quiet` だけの record に Ctrl（最初の token が落ちる）、`a=1 logo=x` に Ctrl（最後が落ちる） | `kmsg=console` / `a=1 kmsg=console`（空白の重複・先頭末尾の空白なし） |
| O11 端から端 | 実際の cfg 3 種（build の graphical、text、GPU 開発の `display=edp login=graphical`）を `zbl_uefi_kern_config_parse()` → `zbl_boot_override_apply()`（keys 0・KMSG・LOGIN・両方）→ kernel の `kern_boot_parameters_parse()` に通す | kernel の parser が 0 を返す（`EEXIST`・`EINVAL` が無い）。`kmsg`・`login` の値が表のとおり、`logo` は unknown に数えられない（Ctrl）か 1 個（他） |
| K1 判定 | `EFI_KEY_DATA` の表: VALID+LCTRL、VALID+RCTRL、VALID+LSHIFT、VALID+RSHIFT、VALID+LCTRL+RSHIFT、VALID のみ、LCTRL だが VALID 無し、全 0 | KMSG、KMSG、LOGIN、LOGIN、KMSG\|LOGIN、0、0、0 |
| K2 走査 | mock: `ConsoleInHandle` 0 と `LocateProtocol` 失敗 / `SetState` 失敗 / 3 つ目の event に Ctrl / 1 回目の sample で Ctrl、2 回目で Shift / 常に SUCCESS で空の event（EXPOSED の下の EDK2 の ConSplitter は key が無くても空の key と現在の修飾の状態で SUCCESS を返し続ける見込み、**推測**。「壊れた firmware」でなく普通の挙動かもしれないので上限は必須） / 常に SUCCESS で Ctrl の立った空の event | 0 / `exposed=0` で sample は動く / KMSG / 2 回目の返りが KMSG\|LOGIN / 32 回で止まり 0 / 32 回で止まり KMSG |

### 9.2 QEMU（T1、p003 の受け入れ。1 件の依頼、QEMU は同時に 1 つ）

image: `plan/ws174/tests/config-amd64-keys.mk`（p002 で作る）は `plan/tools/guest/config-amd64-ssh.mk` を include し、`ZEDBSD_GRAPHICAL_BOOT := y`・`ZEDBSD_BOOT_KERNEL_MESSAGES := n` を置く（build の既定の graphical の cfg: `logo=logo.ppm login=graphical kmsg=quiet`）。`plan/tools/guest/test-image.sh plan/ws174/tests/config-amd64-keys.mk build/ws174-keys` で build。

新しい script `plan/ws174/tests/run-boot-keys-qemu.sh` が cell を**順に**流す。cell ごとに image の複写から QEMU を起動し直す（`-no-reboot` の下では QMP の `system_reset` も shutdown として扱われて QEMU が終わるため、1 つの process で reset を挟む形は採らない。rw の root を hard reset で何度も跨がない利点もある）。同時に動く QEMU は 1 つ。QEMU の形は `plan/tools/guest/guest.py` と同じ: q35、OVMF、KVM、`-device qemu-xhci,id=xhci`、`usb-storage port=1`、`usb-net port=2`（SSH の転送）、`usb-kbd port=3`（`boot-test.sh` の `usb-kbd port=2` は `usb-net` と衝突するので guest.py の割り当てに合わせる）、`-vga std -display none -no-reboot`、QMP socket。

key は QMP `send-key` で送る: `{"keys":[{"type":"qcode","data":"ctrl"},{"type":"qcode","data":"spc"}],"hold-time":50}` が「Ctrl を押したまま Space を 1 回叩く」1 周期（押して 50 ms で全部離す。Shift は `shift`、両方は `ctrl`・`shift`・`spc`）。周期ごとに修飾 key も押し直す: 一度だけ down にしておく形は、OVMF の xHCI の列挙と kernel の USB の reset で QEMU の usb-hid の修飾の状態が消える見込み（**推測**）なので採らない。`device` は指定しない（§3.5）。

| cell | 操作 | 判定 |
| --- | --- | --- |
| C0 none | key を送らない | (a) logo の screendump がある。(b) login prompt まで達する（`boot-test.py` の判定）。(c) SSH で `sysctl kern.boot.login` が `graphical` |
| C1 Ctrl | 起動の直後から `ctrl`+`spc` の周期を約 100 ms ごとに送る。やめる条件: 1 秒ごとの screendump に kernel の text が写ってから 2 秒後、または 60 秒 | (a) logo の無い、kernel の text が写った screendump が 1 枚以上ある。(b) login prompt。(c) `kern.boot.login` が `graphical`（Ctrl は login を変えない） |
| C2 Shift | 同じく `shift`+`spc`。やめる条件: screendump に logo か text が写ってから 2 秒後、または 60 秒 | (a) logo が写る。(b) login prompt。(c) `kern.boot.login` が **`console`** |
| C3 Ctrl+Shift | `ctrl`+`shift`+`spc` | (a) logo の無い kernel の text。(b) login prompt。(c) `kern.boot.login` が `console` |
| C4 late | key を送らずに起動し、logo が写ってから 5 秒後（kernel の中、getty の前）に `ctrl`+`spc` と `shift`+`spc` を 2 秒送る | C0 と同じ（logo、`graphical`）。loader の後の key が効かないことの確認 |

判定の具体: 「kernel の text が写る」= `boot-test.py` の `read_text()` が、`A64`・`zedBSD`・`hal`・`init` のどれかを含む行か、10 文字以上の行を 3 行以上返す。「logo が写る」= `read_text()` が行を返さず、画面の中央の 1/3 の領域の黒でない pixel の割合が 10 % 以上（splash は黒地に絵）。閾値は script に置き、T1 は判定に使った PNG を全部残す。`read_text()` の原点は (0,0)・80x25 の中央・余白の 3 つで、HAL の早期 console の 80x30 の中央（1080 なら y=300）は含まない。C1・C3 で OVMF が 640x480 の希望を受ければ原点は (0,0) で読める。受けなければ（1280x800 や 1920x1080 のまま）早期 console の行は読めないことがあるので、script は 80x30 の中央の原点を自分で足して `read_cells()` を呼ぶ（`boot-test.py` は変えない）。

- framebuffer の QEMU には `/dev/gpu0` が無いので、`login=graphical` でも `sessiond` が終わり getty になる。従って login prompt は全 cell で出る。差は (a) の PNG と (c) の sysctl。
- C1・C3 の screendump が 640x480 なら、OVMF が 640x480 を持ち希望が通った事実として記録する（判定の条件にはしない）。C1 の最初の数枚に告知 `Boot: kernel messages (Ctrl)` が写るかも**記録**する（§4.1、判定ではない）。
- C1・C3 の PASS は「loader の実行中の修飾 key+Space の event が検出される」ことの証拠であって、「電源投入から押しっぱなしで効く」ことの証拠では**ない**（§3.4）。S2 の経路は cell で狙えない（§4.2）。T1 に余裕があれば、C1 の前に「`ctrl` だけ（Space なし、`send-key` の `ctrl`）」を 1 回だけ流し、効けば OVMF の keyboard driver が EXPOSED に対応する事実として記録する（cell ではなく参考。FAIL でも p003 の判定に入れない）。
- C1〜C3 の全部で検出されなければ、`-device usb-kbd` を外して（key は i8042 へ）C1 だけを 1 回再試行し、その結果を分けて記録する（§3.5）。
- 判定は Q1（protocol の T1 の運用）。C1〜C3 のどれかが FAIL なら p003 は uncleared。

### 9.3 実機（ユーザー、5330。WS の受け入れ）

image は 9.2 と同じ config（または UAT の config）で build し USB に書く。手順: 電源 → **Ctrl と Shift を押したまま Space を繰り返し叩く** →（firmware の ConOut が描ければ）loader の告知 2 行と `A64 PARAMS OVERRIDE` が一瞬見え、kernel の message が流れる → 止まった画面を写真に。判定は「**kernel の最後の message が画面に残る**」。Ctrl だけ・Shift だけ、修飾 key 単独（Space なし）で効くかは別に試し、firmware の事実として記録する。実機の証拠は QEMU と分けて phase.md に書く。この確認は WS の受け入れの項目で、どの実装 Phase の cleared にも含めない（担当はユーザーと Q1）。

## 10. 実装 Phase の分け方と受け入れ条件

| Phase | 内容 | 受け入れ条件 | 依存 |
| --- | --- | --- | --- |
| **ws174-p002** module と host 試験 | `bootloader/common/boot-override.c/.h`、`bootloader/uefi/boot-keys.c/.h`、`uefi.h` の宣言、`vmunix.mk` の 2 つの object の rule と link（呼び手はまだ無い）。`plan/ws174/tests/config-amd64-keys.mk`。host 試験 O1〜O11・K1・K2 と run script | host 試験 PASS（通常 + ASan/UBSan、`plan/ws174/tests/` に記録）。amd64 の `make -j16 ZEDBSD_CONFIG=plan/ws174/tests/config-amd64-keys.mk BUILD=build/ws174-keys`（`BOOTX64.EFI` の target）で warning 0。`git diff --stat` に `bootloader/pcat`・`pc98`・`bios`・`zedbsd-config.*`・cfg の生成の rule が無い（BIOS loader と config の形式は不変） | — |
| **ws174-p003** UEFI loader・docs・QEMU | `bootx64.c` の S0〜S2・`apply_override`・`notice_override`・640x480 の希望・`A64 PARAMS OVERRIDE`。docs 4 件（§1 の表、`kernel-boot-parameters.md` は §7a・7b・7c・9。`plan/`・Bug への link を書かない、断定で書く。Dell の Fastboot の Minimal で USB keyboard が初期化されないことがある・電源投入から押し続けると POST の stuck key の警告が出る firmware がある、という注意を**一般論として**書く）。`run-boot-keys-qemu.sh`。kernel の keyboard driver（`src/drivers/`）が LED を自分で設定するかを source で確かめて記録（QEMU では観測できない）。T1 の依頼を Q1 に送る（image の作り方・cell・判定） | build warning 0。`check-amd64-native-image.py` PASS（cfg は不変）。docs の規則。T1 の C0〜C4 が 9.2 の判定で PASS（Q1 が判定するまで in-progress）。C1 の PNG に告知が写るかと 640x480 の有無の記録。S2 が cell で狙えない限界の記録 | p002 |
| **ws174-p004** BIOS PC/AT（後日。ユーザー「BIOSは後日でよいです」。WS の完了の時にまだ未着手なら Future Work か別の WS へ移して p001〜p003・p005 で WS を閉じる） | `bootzbsd.S` の int 16h AH=02h と `zbl_boot_override_apply()` の呼び出し、`msg_boot_keys`、`boot-override.c` の i386 の object、`bootloader/README.md`。QEMU（SeaBIOS、hybrid の image）で C1〜C3 相当（BIOS は状態を読むので key を down のままにする） | pcat・amd64 BIOS・pc98 の build warning 0、3 つの `stage2_end` の記録、T1 の BIOS cell PASS | p002 |
| **ws174-p005** 規約の全文の見直し | WS の変えた C（新しい関数と変えた関数）を `plan/coding-style.md` の全文と照らす | 指摘 0 か記録された例外 | p003（p004 をやるならその後） |

WS の受け入れ（kernel の最後の message が実機の画面に残る）は 9.3 で判定し、ws.md に実機の確認の行を持つ。QEMU で panic を起こす試験は作らない。

## 11. 判断の項目

ユーザーの判断は 2026-10-05 夜に出そろった（§0）。第 3 版で残る**判断**は無い。Q1 を通じてユーザーに**知らせる**事項が 2 つ: (1) 仕様の「Ctrl が押されている」に対し、docs の主の手順は「Ctrl（Shift）を押したまま Space を繰り返し叩く」（第 2 版の D7、Q1 が流用を指示。firmware が押しっぱなしを event にしない見込みのため）。(2) 告知は best effort で、画面に残らない場合がある（§4.1。Ctrl は kernel の message で、Shift は login の形で分かる）。第 2 版の D1・D2・D8（config の形）は仕様の変更で不要、D3（640x480）= Yes、D4（BIOS）= 後回し、D5（1 秒）= No、D6（Ex だけ）・D7（3 点・待ちなし・Ctrl+Space の手順）= 流用。

後の選択肢（この WS では**やらない**）: kernel parameter `safe=1` と `kern.boot.safe`、Ctrl で `i915.start=manual` を足す（i915 が scanout を取って console が消える場合）、PC-98 の key、S2 で検出した Ctrl の video mode の変更。

## 12. ライセンスと転記

- 新しい code（`boot-override.c/.h`、`boot-keys.c/.h`、試験）は coding-style §13 の header（`zedBSD` / `Copyright (C) 2026 Awe Morris` / `SPDX-License-Identifier: Zlib`）。
- `uefi.h` に足す宣言は UEFI 仕様の**事実**（GUID の値、構造体の member の順、定数）だけを自分の書き方で書く。UEFI 仕様の文章・EDK2（BSD-2-Clause-Patent）の code や comment は写さない。EDK2 の keyboard driver の振る舞いは推測の根拠として記憶で参照しただけで、code は読んでいない（§3.4・§3.5 に推測と明記）。
- BIOS の int 16h AH=02h の意味は公開の BIOS interface の事実。

## 13. 敵対的な自己レビューと未確認の事項

1. **足すだけだと kernel が `EEXIST` で止まる** → 落としてから足す（§2.1・§2.3、O1〜O3）。
2. **電源投入からの押しっぱなしは firmware 次第**（EDK2 系は修飾 key 単独の早い押下を捨て、自動で繰り返さない、と推測） → S0 で EXPOSED を早く設定、3 点、主の手順は修飾 key+Space の繰り返し。QEMU の PASS を押しっぱなしの証拠にしない。5330 の firmware が EXPOSED を受けるか、BDS が ConIn を flush するかは**未確認**（9.3 で記録）。
3. **`SetState(EXPOSED)` が NumLock を消す** → document。kernel の keyboard driver の LED の扱いは未確認（p003 の残課題）。
4. **壊れた firmware が `ReadKeyStrokeEx` で SUCCESS を返し続ける** → 32 回で止める（K2）。
5. **告知が SetMode や logo や kernel の console で消える** → 告知は SetMode の後に出す。Shift だけの時は logo が覆う（待たないのはユーザーの決定。debug port と `A64 PARAMS OVERRIDE` に残る）。Ctrl の時は kernel の console が消すまでの間だけ見える。写真の判定は告知でなく kernel の message で行う。
6. **S2 で Ctrl を見た時 `logo_shown` で `quiet_console = 1` のまま** → 告知の前に 0 に戻す。`quiet_boot` を再計算する。
7. **S2 の書き換えが loader の既読の token を変える** → S1 と S2 の間に record から読むのは `kernel_phys=`（`kernel_placement_parse`）だけで、書き換えは `kmsg`・`logo`・`login` の name しか触らない。`video=` は S1 の前に書き換え済みの record を読む。
8. **record の長さの上限** → 落とす方が長い（`logo=logo.ppm login=graphical kmsg=quiet` 36 byte に対し足すのは 26 byte）ので普通は短くなる。入らない時は足さない（O8）。
9. **value の中の `kmsg=`・`login=` を誤って落とす** → name は最初の `=` までの全体一致（O7）。
10. **試験が console log に頼る** → PNG と SSH の sysctl。`kern.boot.kmsg` の sysctl は無いので Ctrl の判定は PNG の kernel の text（early console から init まで数秒以上見えるので 1 秒ごとの screendump で捉えられる）。
11. **late の key が getty に文字を入れる** → C4 は logo の 5 秒後（kernel の keyboard driver の前後、getty の前）に 2 秒だけ送る。C1〜C3 も文字が見えてから 2 秒で止める。SSH の判定は影響を受けない。
12. **OVMF の `ConsoleInHandle` の Ex が usb-kbd と i8042 のどちらの event を流すか未確認** → `send-key` は既定の handler に届く（送り先の指定はできない、§3.5）。どちらに届いても ConSplitter が Ex に流す前提。C1〜C3 の全部で検出されなければ `-device usb-kbd` を外して i8042 だけで C1 を 1 回再試行する手順を script に残す。
13. **Ctrl だけでは greeter が始まり i915 が console を消す** → ユーザーの定義どおり（Ctrl = message、Shift = login）。実機の手順は両方押す（9.3）。
14. **BIOS loader の大きさ** → p002・p003 は BIOS の object を触らない。p004 で記録する。
15. **docs が `plan/` に link する** → p003 の受け入れに規則を置く。
16. **告知が画面に残らない見込み**（GOP の直接の SetMode の後の ConOut、logo、kernel の console） → best effort と明記し判定に使わない。C1 の PNG で観察して記録。残らなければ loader が字を描く案をユーザーに諮る（§4.1）。
17. **S2 が試験で狙えない・S2 で `quiet_console` を触ると診断の行が logo に残る** → S2 は `quiet_console` を触らず、部品は host で確かめ、glue は review で見る。外す判断は Q1（§4.2）。
18. **Shift+英字は firmware が Shift の bit を消す見込み** → 手順と docs は Space に限る（§3.3）。
19. **QMP の `device` は表示の id・`-no-reboot` の下の `system_reset` は終了・一度きりの修飾 key の down は USB の reset で消える・usb-kbd の port が usb-net と衝突** → `send-key` の周期、cell ごとに QEMU を起動、guest.py の port の割り当て（§9.2）。
20. **Ctrl だけでは `login=graphical` のままで greeter が始まり、i915 が scanout を取れば最後の message が消える** → ユーザーの定義どおり。実機の手順は両方押す（§9.3）。WS の受け入れは両方押した時で判定する。
