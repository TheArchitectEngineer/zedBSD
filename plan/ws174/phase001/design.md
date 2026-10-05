# WS174 設計: 起動時の Ctrl で safe boot options に切り替える

Parent: [ws174-p001](phase.md) / [WS174](../ws.md)
版: 第 1 版（2026-10-05 夜、設計の担当。レビューの反映は phase.md の「レビュー」節）

## 0. 範囲とユーザーの定義

- ユーザー（2026-10-05 夜）「ブート時にctrlキーが推されていたらsafe boot optionsの設定に切り替えるように、ブートローダとブートコンフィグファイルを変更したいです。設計だけできますか？」
- ユーザーの明確化（同夜、Q1 経由）「safe boot optionsとはグラフィカルブートにせずカーネルのメッセージをコンソールに出力するオプションを選択することです。」

従って **safe boot とは次の 2 つだけ**である。

1. graphical boot をしない: loader の logo（splash）を出さず、`login=graphical` を使わない（console の getty）。
2. kernel の message を console に出す: `kmsg=quiet` を使わない。

これは build の `ZEDBSD_GRAPHICAL_BOOT=n ZEDBSD_BOOT_KERNEL_MESSAGES=y` が作る config（例: `build/uat-0505g-text/zedbsd-native-uefi-graphical-n-kmsg-y.cfg` = `kernel=`、`video=640x480`、`rootpart=`、`swap0=`。`logo=`・`login=`・`kmsg=quiet` が無い）と同じ選択である。i915 や ACPI を止めるなどの追加は safe boot に**含めない**（§11 の「後の選択肢」に挙げるだけ）。

動機は [BUG-202](../../bugs/BUG-202.md): 実機 5330 で graphical boot の image が kernel の起動の途中で止まるが、logo と `kmsg=quiet` のため画面で読めない。

## 1. 責務と境界

| 構成要素 | 変える | 変えない |
| --- | --- | --- |
| UEFI loader `bootloader/uefi/bootx64.c` | Ctrl の検出、safe の parameter record の選択、画面の告知、`kmsg=quiet` の有無の再判定 | handoff の layout（ZBL6 v7）、memory map、ELF の load、volume の発見 |
| 共通 parser `bootloader/uefi/zedbsd-config.c/.h` | `safe.NAME=VALUE` 行の認識、mode（normal / safe）付きの入口 | 既存の入口 `zbl_uefi_kern_config_parse()` の意味（normal と同じ結果）、`struct zbl_uefi_kern_config` の大きさ（asm が `ZBL_KERN_CONFIG_RESULT_SIZE` で領域を取る） |
| UEFI の宣言 `bootloader/uefi/include/uefi.h` | `ConIn` の型、Simple Text Input (Ex) protocol、`Stall` の型 | 他 |
| build `platform/amd64/vmunix.mk` | 生成する zedbsd.cfg に safe の行を足す | layout・image の形 |
| BIOS loader `bootloader/pcat/bootzbsd.S` | （別 Phase、ユーザーの判断）int 16h の shift flag で Ctrl を見て safe mode で parser を呼ぶ | — |
| kernel・HAL・UAPI・init・sessiond | **変えない** | — |
| docs | `bootloader/uefi/README.md`・`bootloader/README.md`・`docs/reference/kernel-boot-parameters.md`・`docs/howto/boot-and-storage.md` | — |

kernel は safe boot を知らない。loader が渡す record に `logo=`・`login=graphical`・`kmsg=quiet` が無く `kmsg=console login=console` がある、というだけで、kernel と `sessiond`（`kern.boot.login` を読む、`userland/desktop/sessiond/main.c:58`）の既存の動作がそのまま text の boot になる。**handoff・UAPI・HAL の変更は要らない**（§6 で確かめる）。

## 2. boot config の形式

### 2.1 現行の文法（`zedbsd-config.c` の事実）

- 1 行 `name=value`。文字は 0x21〜0x7e、`"` `'` `#` `;` `\` は不可（`unsupported_syntax()`）。`=` の無い行・空の name・空の value は `malformed-line` で **file 全体が拒否**される（`next_line()` 116〜118 行）。
- 行数 64、行 511 byte、file 4096 byte、最終 record 3071 byte。
- `kernel=` だけ loader が消費し、他の行は全部 kernel の parameter token になる（`builder_line()`）。`overlay-root`・`overlay-data`・`swapN` の相対 path は `boot0:` を付ける。
- 名前に `.` を含む既存の token: `display.mode=`・`i915.start=`・`i915.debug=`（`include/kern/boot.h`）。
- kernel は知らない名前を**数えて無視**する（`src/kern/boot.c` 250〜262 行 `record_unknown()`、最初の名前だけ 31 byte まで保持し診断に出す）。

### 2.2 選んだ形: `safe.NAME=VALUE`

```text
kernel=vmunix
rootpart=PARTLABEL=zedBSD-root
swap0=PARTLABEL=zedBSD-swap
logo=logo.ppm
login=graphical
kmsg=quiet
safe.kmsg=console
safe.login=console
```

- `safe.` で始まる名前の行は **loader だけの行**（`logo=` と同類）。normal の boot では record に**入れない**（kernel に渡さない）。safe の boot では `safe.` を外した `NAME=VALUE` として record に入れる。
- **後方互換**: 古い loader（3 つの BIOS loader を含む）はこの行を知らず、普通の行として kernel に `safe.kmsg=console` を渡す。kernel は未知の名前として無視する（診断 1 行、起動は普通に続く）。`[safe]` のような節の見出しは `=` が無いので古い loader が file ごと拒否して**起動できなくなる**ため選ばない。
- **既定（config に safe の行が無い時）**: loader の safe mode は、config の safe の行の有無に関わらず、normal の行のうち **`logo`・`login`・`kmsg`** の 3 つの名前を落とす。この 3 つは kernel・loader の既定が「logo なし・console の login・console に message」なので、落とすだけでユーザーの定義の safe boot になる。build が足す `safe.kmsg=console`・`safe.login=console` は、record を自己記述的にし（`dmesg`・`sysctl kern.boot.login` で見える）、file を読む人に何が起きるかを示すための明示であって、無くても同じ結果になる。古い zedbsd.cfg（safe の行の無い ESP）でも新しい loader なら Ctrl が効く。
- safe mode の record の組み立て（`zbl_uefi_kern_config_parse_mode(..., ZBL_KERN_CONFIG_SAFE)`）:
  1. `kernel=` は normal と同じく消費する（safe の kernel は無い）。
  2. `boot0=` の合成（選んだ FAT の UUID）は normal と同じ。
  3. normal の各行: 名前が `logo`・`login`・`kmsg` なら落とす。名前 N に対して `safe.N` の行があれば落とす（置き換え）。それ以外は normal と同じ処理（`boot0:` の修飾を含む）で出す。
  4. `safe.N=V` の各行を file の順に `N=V` として出す。`boot0:` の修飾の判定（`line_needs_boot0()`）は `safe.` を外した名前で行う（`safe.swap0=swapfile` → `swap0=boot0:swapfile`）。
  5. record の長さの上限（3071）は組み上がった record に対して判定する。
- normal mode でも safe の行を**検査**する（safe boot の時にだけ現れる誤りを作らないため）: `safe.kernel`・`safe.boot0`・`safe.safe.*`（二重の prefix）・名前が `safe.` だけ → `invalid-safe-name`。同じ `safe.N` が 2 つ → `duplicate-safe-name`。新しい result は enum の末尾に足す（`zbl_uefi_kern_config_result_name()` にも）。`safe.boot0` を拒むのは `have_boot0` の合成の判定を normal と同じに保つため。`safe.kernel` を拒むのは kernel の path が mode の前に決まるため。
- `safe=...`（prefix でなく名前そのもの）は普通の未知の名前として kernel に渡る（検査しない。kernel が無視する）。
- 行数 64 の上限は safe の行を含めて数える（変えない。生成する config は 8 行）。

### 2.3 build が作る config

`platform/amd64/vmunix.mk`:

- `$(AMD64_NATIVE_UEFI_ZEDBSD_CONFIG)`（2090 行〜）と `$(AMD64_BIOS_ZEDBSD_CONFIG)`（1985 行〜）の recipe の末尾に、`ZEDBSD_GRAPHICAL_BOOT`・`ZEDBSD_BOOT_KERNEL_MESSAGES` の値に関わらず `printf '%s\n' safe.kmsg=console safe.login=console >> $@.tmp` を足す。text の build（graphical=n、kmsg=y）では normal の行に logo/login/kmsg が無いので safe の行は無害（Ctrl を押しても同じ boot）。
- 生成名に format の印を足す（例 `...-kmsg-$(…)-safe1$(AMD64_BOOT_EXTRA_TAG).cfg`）: recipe の変更は make の依存に現れないので、既存の `BUILD` directory の古い cfg を作り直させるため。`platform/amd64/tools/check-amd64-native-image.py` は ESP の `/zedbsd.cfg` を生成 file と比べるので両方が一緒に変わる。
- 静的な `platform/amd64/zedbsd.cfg`（overlay の開発 image）・`platform/pcat/*.cfg`・`platform/pc98/*.cfg` は、BIOS の Phase（§10 p004）まで変えない。新しい parser は normal mode で safe の行を落とすだけなので、足しても害は無いが、Phase を分けるために触らない。
- `ZEDBSD_BOOT_EXTRA_LINES`（`display=edp` など）は normal の行として残り、safe mode でもそのまま kernel に渡る（ユーザーの定義の外なので触らない）。

### 2.4 互換の行列

| loader | zedbsd.cfg | normal の boot | Ctrl の boot |
| --- | --- | --- | --- |
| 旧 | 旧 | 今と同じ | 今と同じ（Ctrl は無視） |
| 旧 | 新（safe の行あり） | kernel が `safe.kmsg`・`safe.login` を未知の名前として無視（診断「unknown parameter」1 行）。起動は普通 | 同左 |
| 新 | 旧 | 今と同じ（record は byte 単位で同じ、§9 H1） | logo/login/kmsg を落とした record で起動（safe） |
| 新 | 新 | safe の行を落とした record（旧 loader + 旧 cfg と同じ） | 上に加えて `kmsg=console login=console` を付けた record |

## 3. UEFI loader の Ctrl の検出

### 3.1 使う firmware の口

`uefi.h` に足す宣言（UEFI 仕様 2.x の事実。GUID・layout・定数の値だけを書き、仕様書や EDK2 の文は写さない）:

```c
typedef struct { uint16_t ScanCode; CHAR16 UnicodeChar; } EFI_INPUT_KEY;
typedef struct { UINT32 KeyShiftState; uint8_t KeyToggleState; } EFI_KEY_STATE;
typedef struct { EFI_INPUT_KEY Key; EFI_KEY_STATE KeyState; } EFI_KEY_DATA;

typedef struct efi_simple_text_input_protocol {
	EFI_STATUS (EFIAPI *Reset)(struct efi_simple_text_input_protocol *, BOOLEAN);
	EFI_STATUS (EFIAPI *ReadKeyStroke)(struct efi_simple_text_input_protocol *, EFI_INPUT_KEY *);
	EFI_EVENT WaitForKey;
} EFI_SIMPLE_TEXT_INPUT_PROTOCOL;

typedef struct efi_simple_text_input_ex_protocol {
	EFI_STATUS (EFIAPI *Reset)(struct efi_simple_text_input_ex_protocol *, BOOLEAN);
	EFI_STATUS (EFIAPI *ReadKeyStrokeEx)(struct efi_simple_text_input_ex_protocol *, EFI_KEY_DATA *);
	EFI_EVENT WaitForKeyEx;
	EFI_STATUS (EFIAPI *SetState)(struct efi_simple_text_input_ex_protocol *, uint8_t *);
	void *RegisterKeyNotify;
	void *UnregisterKeyNotify;
} EFI_SIMPLE_TEXT_INPUT_EX_PROTOCOL;

#define EFI_SHIFT_STATE_VALID        0x80000000U
#define EFI_RIGHT_CONTROL_PRESSED    0x00000004U
#define EFI_LEFT_CONTROL_PRESSED     0x00000008U
#define EFI_TOGGLE_STATE_VALID       0x80U
#define EFI_KEY_STATE_EXPOSED        0x40U
#define EFI_NOT_READY                (0x8000000000000006ULL)
/* EFI_SIMPLE_TEXT_INPUT_EX_PROTOCOL_GUID dd9e7534-7762-4698-8c14-f58517a625aa */
typedef EFI_STATUS (EFIAPI *EFI_STALL)(UINTN microseconds);
```

`EFI_SYSTEM_TABLE.ConIn` は `EFI_SIMPLE_TEXT_INPUT_PROTOCOL *` に、`EFI_BOOT_SERVICES.Stall` は `EFI_STALL` に型を付ける（offset は変わらない。`uefi.h` の table は仕様の順で void* を並べているので、型を付けるだけ）。

### 3.2 2 つの方法の比較と選択

| 方法 | 分かること | 問題 |
| --- | --- | --- |
| A. `EFI_SIMPLE_TEXT_INPUT_EX_PROTOCOL.ReadKeyStrokeEx` の `KeyState.KeyShiftState` | 鍵の event に付く修飾 key の状態（Ctrl 左右）。`SetState(EFI_KEY_STATE_EXPOSED)` を受ける firmware（EDK2 系の PS/2・USB keyboard driver）では **Ctrl 単独の押下**も「key の無い event」として読める | event が queue に無いと `EFI_NOT_READY`。firmware が loader の前に押下の event を捨てていると、押しっぱなしでも queue は空（§3.4） |
| B. `EFI_SIMPLE_TEXT_INPUT_PROTOCOL.ReadKeyStroke` | 文字と scan code だけ | Ctrl 単独は event にならない。Ctrl+文字は制御文字（0x01〜0x1a）になるが Tab・BS・CR・LF と区別が付きにくい |

**A を選ぶ**。B は退避にもしない（UEFI 2.1（2007）以降の firmware は A を持ち、Ex が無い firmware では「Ctrl の safe boot は使えない」と document する。選ばなかった理由: B の制御文字の判定は曖昧で、誤って safe になる方が困る）。

### 3.3 標本を取る手順（`bootloader/uefi/safe-key.c`、新 file）

```c
/* 1: Ctrl が押されていた、0: 押されていない（protocol が無い・失敗も 0）。boot を止めない。 */
int zbl_uefi_safe_key_sample(EFI_SYSTEM_TABLE *system);
/* 純粋な判定。host 試験の対象。 */
int zbl_uefi_safe_key_from_state(const EFI_KEY_DATA *key);
```

1. `system->ConsoleInHandle` が 0 なら 0。`boot->HandleProtocol(ConsoleInHandle, &EX_GUID, &ex)` が失敗なら `boot->LocateProtocol(&EX_GUID, 0, &ex)` を試し、それも失敗なら 0。
2. 初回だけ `ex->SetState(ex, &toggle)`（`toggle = EFI_TOGGLE_STATE_VALID | EFI_KEY_STATE_EXPOSED`）。失敗は無視（EXPOSED を知らない firmware では Ctrl 単独は読めず、Ctrl+key だけが効く）。副作用: `SetState` は NumLock・CapsLock・ScrollLock の bit も書くので、firmware の LED の状態が消える（通常の起動時は NumLock off が普通。kernel の keyboard driver が自分の LED を設定するかは**未確認**。§11 の残課題）。
3. `ReadKeyStrokeEx` を **最大 32 回**、`EFI_NOT_READY`（または他の失敗）まで繰り返す（成功を返し続ける壊れた firmware で止まらないための上限）。どれかの `EFI_KEY_DATA` が `zbl_uefi_safe_key_from_state()` で真なら 1。
4. `zbl_uefi_safe_key_from_state()`: `(KeyShiftState & EFI_SHIFT_STATE_VALID) != 0` かつ `(KeyShiftState & (EFI_LEFT_CONTROL_PRESSED | EFI_RIGHT_CONTROL_PRESSED)) != 0`。Key の中身（空でも、文字でも）は見ない。従って **Ctrl 単独（EXPOSED が効く firmware）も、Ctrl+任意の key も** safe になる。
5. 読んだ event は捨てる（kernel の keyboard driver は自分で初期化するので、loader の間に打たれた key が kernel に届かないのは今と同じ）。

### 3.4 いつ標本を取るか（起動を遅らせない）

待ち時間は**足さない**（ユーザーの要求「no key の時に boot を遅らせない」）。代わりに loader の実行の間に 2 点で標本を取る。

- **S1**: config を読んで parse した直後、`zbl_uefi_video_select()` と `show_logo()` の**前**（`efi_main` の 1446 行の前）。ここで safe なら: parameter record は safe のもの、`logo_named` は 0 になるので video の希望は無し（GOP は firmware の現在の mode のまま）、logo を読まない、`quiet_boot` は 0。
- **S2**: kernel の ELF を読み終えて volume を閉じた直後、`build_bootstrap()` の**前**（1531〜1543 行の間）。S1 で normal だったが S2 で Ctrl を見たら: handoff に入れる record を safe のものに替え、`quiet_boot` を 0 にし（stage block を描き、`zbl_transition_start` から入る）、告知を出す（logo の上に ConOut で描く。`fail_status()` が logo の上に error を描くのと同じ経路）。video と logo は既に決まっているのでそのまま（kernel の console が `kmsg=console` で画面を消して描くので、logo は kernel の最初の message で消える）。

S2 がある理由: firmware は loader の前に起きた Ctrl の押下の event を捨てることがある（EDK2 系の keyboard driver は修飾 key 単独の押下を、EXPOSED が設定されていない間は event にしない、と理解している。**loader の前の状態は確かめようが無いので推測**）。S1 と S2 の間は kernel file の読み出し（USB stick で 1 秒前後、NVMe で 100 ms 程度。**計っていない**）で、ユーザーが Ctrl を押し直す（または Ctrl を押したまま Space を叩く）窓になる。両方で読む費用は数十 µs。

`safe.*` の override のうち loader 自身が読む token（`video=`・`kernel_phys=`・`logo=`）は **S1 で safe になった時だけ**効く（S2 では既に使った後）。build はそれらの safe 行を作らないので実害は無い。document に書く。

ユーザーへの手順（docs/howto に書く）: 「電源を入れたら Ctrl を押したままにする。zedBSD の logo が出ずに kernel の message が流れれば safe boot。logo が出てしまったら、Ctrl を押したまま Space を一度叩く（loader が kernel を読んでいる間に間に合えば、kernel の message が console に出る）。」

### 3.5 firmware の癖への備え

- USB keyboard が loader の時点で未初期化: firmware の BDS が console を connect してから loader を起動するのが普通で、`ConIn` は ConSplitter 経由。ConIn が 0 または Ex が無ければ 0 を返して普通に起動する。
- laptop の内蔵 keyboard（5330 は i8042 の PS/2 と推測、**未確認**）: EDK2 の PS/2 driver は partial keystroke（EXPOSED）に対応していると理解している（推測）。対応していなくても Ctrl+Space の経路は効く。
- 誤検出（firmware が Ctrl の bit を立てっぱなしにする等）の害は text の boot になることだけ。告知で分かる。
- QEMU/OVMF（`boot-test.sh` は `-device usb-kbd` を xHCI に付ける）: OVMF の UsbKbDxe が EXPOSED に対応している前提で試験する（§9）。対応していなければ Ctrl+Space の cell で確かめる。

### 3.6 BIOS（PC/AT）の経路

`bootloader/pcat/bootzbsd.S` は real mode で `load_kern_configuration` から parser を呼ぶ（922〜935 行、`.code16gcc` で 5 つの引数を push）。Ctrl の検出は **int 16h AH=02h**（AL の bit 2 = Ctrl が押されている。BIOS の keyboard handler が BDA 0040:0017 に押下中の状態を保持するので、押しっぱなしで読める。event ではなく状態なので UEFI より素直）。safe なら `zbl_uefi_kern_config_parse_mode` を mode=SAFE で呼ぶ（引数を 1 つ増やす: push を 1 つ足し `add esp, 24`）。告知は `puts`（msg_safe_boot）。amd64 BIOS（同じ `BOOTZBSD.EXE`）も同じ。PC-98 は `int 18h AH=02h` の key 状態の bitmap で Ctrl を読めるはずだが（**bit の位置は未確認**）、PC-98 には graphical boot が無く動機が薄い。

**提案**: PC/AT BIOS は小さい別 Phase（p004）として用意し、やるかどうかはユーザーの判断（D4）。PC-98 は Future Work。p002 の parser の変更で BIOS loader の build が壊れないこと（wrapper で既存の入口を残す）は p002 の受け入れ条件に入れる。

## 4. 画面の告知と kernel・init への伝達

- 告知: `console_ascii(&context, "Safe boot (Ctrl): no logo, kmsg=console, login=console\n")`。S1 で safe なら `quiet_console` は 0 のままなので ConOut に普通に出る。S2 で logo の後なら `context.quiet_console = 0` にしてから出す（`fail_status()` と同じ）。debug port（0xe9）にも常に出る。
- safe mode だけ `boot->Stall(1000000)`（1 秒）を告知の後に入れて読めるようにする（normal の boot は遅れない）。D5。
- kernel は record の `kmsg=console`・`login=console` を今の parser で受ける（両方とも `parameter_word()` が受ける語）。`sysctl kern.boot.login` が `console` を返す。`sessiond` は `console` を見て終わり、init が `getty_console` を始める（`docs/reference/kernel-boot-parameters.md` §7b の既存の動作）。
- **新しい kernel parameter は足さない**（`safe=1` は後の選択肢 §11）。handoff の `flags` に bit を足すことも、HAL の `hal.h` も、UAPI も**変えない**。

## 5. safe mode で panic が読めること

事実（source）:

- `kmsg=console` のとき HAL の早期 console は framebuffer を黒く消して描く（`src/hal/amd64/bsp-pcat/cons.c` 376〜398 行。`kmsg=quiet` の時だけ `console_suspended=1` で描かない）。kernel の text console は `text_hidden = kern_log_quiet()` = 0 で最初から表示（`src/drivers/platform/pcat/graphics/text.c:542`）。
- panic（`__libc_panic`・`kern_fatal`、`src/kern/panic.c`）は理由を log に残し、`kern_text_reveal_fatal()` で console を出し（BUG-158: graphics mode が text を suspend していても、lock をこの CPU が持っていても描く、`text.c:936`）、message を `hal_putc` で出して `hal_halt()` で止まる。画面はそのまま残る。

従って safe mode では、**kernel の message が最初から画面にあり、panic の message がその下に出て止まる**。追加の実装は要らない。残る限界（document に書く、変えない）:

- HAL の早期 console が立ち上がる前の停止（`locore`〜`cons` の初期化の間）は画面に出ない。これはどの設定でも同じ。
- `login=console` でも i915 の driver は `i915.start=auto`（既定）で起動する。i915 の初期化が firmware の framebuffer の scanout を変えて text console が見えなくなる可能性は**未確認**（BUG-202 の text の image `build/uat-0505g-text` を実機で起動した結果がその証拠になる。見えなければ safe boot の範囲で `i915.start=manual` を足すかの判断（§11）に戻る）。
- panic の後に scroll はしない（最後の画面が残る）。message が多くて panic の原因の行が流れた場合は `kmsg=console` でも読めないが、panic の message 自体は必ず最後に出る。

## 6. handoff・UAPI・HAL の変更の要否

- handoff: parameter record の text だけが変わる。`struct zbl6_handoff_v7_uefi`・`kern_boot_parameter_record` の layout・version・flags は不変。
- UAPI: `kern.boot.login` は既存。新しい sysctl は無い。
- HAL: `src/hal/x86/boot-parameters.c` は record を写すだけ。`src/hal/amd64/bsp-pcat/cons.c` は `kmsg=quiet` の token を見るだけ。**hal.h も `src/hal/` も変えない。承認の要る差分は無い。**

## 7. 内部 interface（実装 Phase への指示）

### 7.1 `bootloader/uefi/zedbsd-config.h`

```c
enum zbl_kern_config_mode {
	ZBL_KERN_CONFIG_MODE_NORMAL = 0,
	ZBL_KERN_CONFIG_MODE_SAFE = 1
};
#define ZBL_KERN_CONFIG_SAFE_PREFIX "safe."
/* 末尾に足す result */
	ZBL_UEFI_KERN_CONFIG_INVALID_SAFE_NAME,
	ZBL_UEFI_KERN_CONFIG_DUPLICATE_SAFE_NAME

enum zbl_uefi_kern_config_result zbl_uefi_kern_config_parse_mode(
	struct zbl_uefi_kern_config *configuration,
	const void *source, size_t source_size,
	const char *selected_uuid, size_t selected_uuid_capacity,
	enum zbl_kern_config_mode mode);
/* 既存。parse_mode(..., NORMAL) と同じ。BIOS loader の asm の呼び出しを変えないために残す。 */
enum zbl_uefi_kern_config_result zbl_uefi_kern_config_parse(...);
```

`struct zbl_uefi_kern_config` は変えない（`_Static_assert` の大きさを保つ）。

### 7.2 parser の内部（`zedbsd-config.c`）

- `line_safe_name(const struct config_line *line, struct config_line *stripped)`: 名前が `safe.` で始まるなら、`safe.` を外した view（text+5、length-5、equal-5）を返す。
- 第 1 pass（`kernel`・`boot0` を数える既存の loop）に safe の行の検査を足す: stripped の名前が空・`kernel`・`boot0`・`safe.` 始まり → `INVALID_SAFE_NAME`。既出の safe の名前との一致 → `DUPLICATE_SAFE_NAME`。safe の名前の記憶は、file の中を再走査して比べる（64 行 × 64 行で十分小さい。配列を持たない）。
- 第 2 pass（record の組み立て）: `mode == SAFE` のとき (a) 名前が `logo`・`login`・`kmsg` の行を飛ばす、(b) 同じ名前の `safe.` 行が file のどこかにあれば飛ばす（再走査）、(c) `safe.` の行は飛ばす。`mode == NORMAL` のとき `safe.` の行を飛ばす。
- 第 3 pass（`mode == SAFE` だけ）: `safe.` の行を file の順に stripped の view で `builder_line()` に渡す（`boot0:` の修飾はそこで今の規則で付く）。
- 既存の `builder_line()`・`line_needs_boot0()` は `struct config_line` の view を取るので、stripped の view を渡すだけで再利用できる。

### 7.3 `bootloader/uefi/bootx64.c`

- `struct loader_context` に `int safe_boot;`。
- `static struct zbl_uefi_kern_config configuration;` に加えて `static struct zbl_uefi_kern_config safe_configuration;`（それぞれ 256 + 3088 byte の static、`.bss`。loader の image に収まる）。`load_selected_configuration()` は読み込んだ `config_buffer` から **両方を parse** する（file は 1 回だけ読む）。両方の結果が OK でなければ今と同じく `zedbsd.cfg rejected: <name>` で止める（safe の parse だけが失敗する組み合わせは §2.2 の検査で normal も失敗するので起きないが、両方を検査する）。
- `const struct zbl_uefi_kern_config *effective = &configuration;` を S1 で選び、以降の `zbl_logo_path`・`zbl_uefi_video_select`・`show_logo`・`zbl_parameter_present(kmsg=quiet)`・`kernel_placement_parse` は `effective` を読む。S2 で `effective` を替え、`quiet_boot` を再計算し、`build_bootstrap(..., &effective->parameter_record, ...)` に渡す。
- 告知と Stall（§4）。

### 7.4 `bootloader/uefi/safe-key.c/.h`（新 file）

§3.3 の 2 関数。`uefi.h` の宣言だけに依存し、`bootx64.c` の static には触らない（host 試験で mock の table を渡せるように、`EFI_SYSTEM_TABLE *` を引数に取る）。

### 7.5 build の rule

`platform/amd64/vmunix.mk` の `$(BUILD)/uefi/bootx64.o` の近く（555 行〜）に `safe-key.o` の rule と link の object を足す（UEFI loader の link の規則は `UEFI_LOADER` 変数の下にある。実装 Phase で `platform/amd64/vmunix.mk` の UEFI の節を読んで同じ形で足す）。BIOS の helper（`AMD64_BOOTZBSD_HELPERS`・`PCAT_BOOTZBSD_HELPERS`・`PC98_BOOTZBSD_HELPERS`）は `zedbsd-config.c` を i386 で compile しているので、新しい入口が増えても object は 1 つのまま。

## 8. 失敗と回復・並行性・資源

- 失敗はどれも boot を止めない: protocol が無い・`SetState` 失敗・`ReadKeyStrokeEx` の error → normal の boot。config の safe の行の誤りだけは（normal の boot でも）`zedbsd.cfg rejected: invalid-safe-name` 等で止める（今の「不正な config は見える形で止める」方針のまま）。
- 並行性: loader は single thread、boot services の中で TPL は application。event の待ちは使わない（`WaitForEvent` を使わない。待たないため）。
- 資源: 追加の pool allocation は無い。`safe_configuration` は static。`config_buffer` は今と同じ 1 回の読み出し。
- 割込み・DMA: 無関係。

## 9. 試験計画

判定は **QMP の screendump の PNG と、guest への SSH/serial の問い合わせ**で行い、QEMU の console log・serial log では判定しない（Guardrail）。実装の担当は QEMU を起動せず、T1 に依頼する。

### 9.1 host 試験（`plan/ws174/tests/`、p002・p003 の受け入れ）

`run-zedbsd-config-safe-host-test.sh` が `bootloader/uefi/zedbsd-config.c` と `zedbsd-config-safe-host-test.c` を host の cc で compile して流す（通常と `-fsanitize=address,undefined` の 2 回。WS013 p003 の削除された host 試験と同じ形。`zedbsd-config.c` は `<stddef.h>` と `kern/boot.h` だけに依存し host で compile できる）。

| 番号 | 入力 | 期待 |
| --- | --- | --- |
| H1 回帰 | safe の行の無い現行の 3 つの cfg（`platform/amd64/zedbsd-native-uefi.cfg` に graphical の行を足したもの、`zedbsd.cfg`、`zedbsd-native.cfg`） | `parse()` と `parse_mode(NORMAL)` の record が byte 単位で今の出力と同じ（期待値は試験に固定で書く） |
| H2 normal | 生成する cfg（§2.2 の例） | NORMAL: `boot0=UUID=… rootpart=… swap0=… logo=logo.ppm login=graphical kmsg=quiet`（safe の行が無い） |
| H3 safe | 同上 | SAFE: `boot0=UUID=… rootpart=… swap0=… kmsg=console login=console` |
| H4 既定 | safe の行の無い graphical の cfg | SAFE: logo/login/kmsg が落ちるだけ |
| H5 置き換え | `video=640x480` + `safe.video=1024x768` | SAFE: `video=1024x768` が 1 つだけ、末尾に |
| H6 修飾 | `safe.swap0=swapfile`、`safe.overlay-root=rootfs.img`、`safe.swap1=/dev/sda5` | SAFE: `swap0=boot0:swapfile overlay-root=boot0:rootfs.img swap1=/dev/sda5` |
| H7 拒否 | `safe.kernel=x`、`safe.boot0=UUID=…`、`safe.=x`、`safe.safe.kmsg=console`、`safe.kmsg` の重複 | 両 mode で `invalid-safe-name` / `duplicate-safe-name` |
| H8 境界 | 64 行（safe の行を含む）、3071 byte の record（safe mode で越える・越えない） | 既存の `too-many-lines`・`parameters-too-long` が mode ごとの record に対して出る |
| H9 CRLF | H2 の CRLF 版 | H2・H3 と同じ |
| K1 判定 | `EFI_KEY_DATA` の表: VALID+LEFT_CTRL、VALID+RIGHT_CTRL、VALID のみ、LEFT_CTRL だが VALID 無し、空 | `zbl_uefi_safe_key_from_state()` が 1,1,0,0,0 |
| K2 走査 | mock の system table（`ms_abi` の関数 pointer。WS013 の `uefi-volume-discovery-test.c` と同じ手法）: Ex 無し / `SetState` 失敗 / 3 つ目の event に Ctrl / 常に SUCCESS で空の event を返す | 0 / 動く / 1 / 32 回で止まり 0 |

### 9.2 QEMU（T1 に依頼、p003 の受け入れ）

image: `plan/ws174/tests/config-amd64-safe.mk`（`plan/ws159/tests/config-amd64-uat.mk` を include、graphical=y・kmsg=n、SSH の harness 付き）を `plan/tools/guest/test-image.sh` で build。生成された cfg に safe の 2 行があることを build の後に `grep` で確かめる。

QEMU の起動は `boot-test.sh` と同じ形（q35、OVMF、`-device qemu-xhci` + `usb-kbd`、QMP socket）。新しい script `plan/ws174/tests/run-safe-key-qemu.sh`:

| cell | 操作 | 判定 |
| --- | --- | --- |
| Q1 safe (Ctrl 単独) | QEMU 起動の直後から 8 秒間、QMP `input-send-event` で `ctrl` の down を 1.5 秒・up を 0.1 秒の周期で繰り返す（押下の遷移と押しっぱなしの状態の両方を作る。OVMF が loader を起動する時刻は分からないので繰り返す）。 | (a) 起動の 10〜20 秒後の screendump に text の kernel message が写り、logo が無い（`boot-test.py` の `read_text()` で `zedBSD` の banner 行を読む）。(b) login prompt まで達する（`boot-test.py` の判定）。(c) SSH で `sysctl kern.boot.login` が `console`、`dmesg | grep 'login=console'`（record の text が log にあれば）。 |
| Q2 safe (Ctrl+Space) | 同じ周期で `ctrl-spc` を `send-key`（Ctrl 単独を event にしない firmware の経路） | Q1 と同じ |
| Q3 control | key を送らない | screendump に logo（splash）が写り、`sysctl kern.boot.login` が `graphical`（framebuffer の QEMU では greeter が終わって getty になるのは従来どおりなので login prompt は両方で出る。差は (a) と (c)） |
| Q4 誤検出なし | Q3 の後、kernel が動いてから `ctrl` を送る | 影響なし（念のため。login の値は `graphical` のまま） |

Q1〜Q3 は 1 つの image で 3 回の boot。1 回 60 秒以内。T1 の台帳に 1 件の依頼として出す。

### 9.3 実機（ユーザー、5330）

image は 9.2 と同じ config（または UAT の config）で build し USB に書く。手順: 電源 → Ctrl を押したまま → 画面に kernel の message が流れることを写真で。BUG-202 の panic がそこで読めれば、BUG-202 の解析の入力になる。loader の告知 `Safe boot (Ctrl)` が 1 秒見える。実機の証拠は QEMU と分けて phase.md に書く。Ctrl 単独が効かず Ctrl+Space だけが効いた場合はその firmware の事実として記録する。

## 10. 実装 Phase の分け方と受け入れ条件

| Phase | 内容 | 受け入れ条件 | 依存 |
| --- | --- | --- | --- |
| **ws174-p002** parser | `zedbsd-config.c/.h` の `parse_mode`・safe の行・result の名前。host 試験 H1〜H9。`bootloader/README.md` の文法の節 | host 試験 PASS（通常 + ASan/UBSan）。amd64 の `make -j16` で `BOOTX64.EFI`・`BOOTZBSD.EXE` が warning 0、pcat（i386）と pc98 の `BOOTZBSD.EXE` も warning 0 で build できる。H1 で現行の record と byte 単位で同じ | — |
| **ws174-p003** UEFI loader と build | `uefi.h` の宣言、`safe-key.c/.h`、`bootx64.c` の S1・S2・告知・Stall、`vmunix.mk` の safe の行と cfg の名前、host 試験 K1・K2、docs（`bootloader/uefi/README.md`・`docs/reference/kernel-boot-parameters.md` §7c・`docs/howto/boot-and-storage.md`） | build warning 0。K1・K2 PASS。生成 cfg に safe の 2 行。T1 の Q1〜Q4 PASS（Q1 か Q2 の少なくとも一方で safe。両方失敗なら uncleared）。`check-amd64-native-image.py` PASS | p002 |
| **ws174-p004** BIOS PC/AT（任意、D4） | `bootzbsd.S` の int 16h と `parse_mode(SAFE)` の呼び出し、`msg_safe_boot`、静的 cfg への safe の行、`bootloader/README.md`。QEMU（SeaBIOS、`bios-hdd-image.img`）で Q1 相当 | pcat・amd64 BIOS の build warning 0、T1 の BIOS cell PASS | p002 |
| **ws174-p005** 規約の全文の見直し | WS の変えた C を `plan/coding-style.md` の全文と照らす（code を作る WS に必須） | 指摘 0 か記録された例外 | p003（p004 をやるならその後） |

p003 が cleared でも WS の受け入れ（「panic などが画面で読める」）は実機の確認（9.3）で判定する。QEMU で panic を起こす試験は作らない（細かい確認を厚くしない方針）。

## 11. 判断の項目（ユーザーへ）と推奨

| ID | 問い | 推奨 | 理由 |
| --- | --- | --- | --- |
| D1 | config の形は `safe.NAME=VALUE`（§2.2）でよいか（`[safe]` の節は不採用） | はい | 古い loader が file を拒否しない唯一の形。kernel は未知の名前を無視する |
| D2 | config に safe の行が無くても、Ctrl で `logo`・`login`・`kmsg` を落とす（loader 組み込みの既定）でよいか | はい | 既に書き込まれた ESP の cfg でも効く。build は明示の 2 行を足す |
| D3 | safe の set は `kmsg=console login=console`（logo なし）の 2 つだけでよいか。`video=640x480` は入れない | はい、入れない | ユーザーの定義どおり。`video=` は「必ず満たす」要求で、GOP に無い mode なら boot が止まる（`video_select()`）。logo が無ければ loader は mode を変えず firmware の mode のまま |
| D4 | BIOS PC/AT（p004）をこの WS でやるか | **後回し**（Future Work か p004 を planned のまま置く） | 動機の実機は UEFI。p002 で BIOS loader の build は守る |
| D5 | safe mode だけ告知の後に 1 秒止めるか | はい | normal は遅れない。safe を選んだ人が告知を読める |
| D6 | 検出は Ex protocol だけ（simple の `ReadKeyStroke` の制御文字は使わない）でよいか | はい | 誤検出を避ける。Ex の無い firmware では Ctrl の safe boot は無い、と document |
| D7 | S1 と S2 の 2 点で標本を取る（§3.4）でよいか。S2 では video/logo は戻さない | はい | firmware が早い押下を捨てる場合の救済。遅延 0 |
| D8 | `safe.kernel`・`safe.boot0` を拒否（新しい result）でよいか | はい | kernel path と boot0 の合成を mode に依存させない |

後の選択肢（この WS では**やらない**。ユーザーの定義の外。Future Work に載せるかは Q1）:

- kernel parameter `safe=1` と `sysctl kern.boot.safe`（userland が safe boot を知る）。
- safe の set に `i915.start=manual`（i915 が scanout を取って console が消える場合）。§5 の未確認が実機で問題になった時に検討。
- PC-98 の Ctrl（int 18h）。
- 削除の文法（`safe.NAME=` で normal の token を消す）。

## 12. ライセンスと転記

- 新しい code（`safe-key.c/.h`、parser の変更、試験）は `Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib`。
- `uefi.h` に足す宣言は UEFI 仕様の **事実**（GUID の値、構造体の member の順、定数）だけを自分の書き方で書く。UEFI 仕様の文章・EDK2（BSD-2-Clause-Patent）の code や comment は写さない。EDK2 の keyboard driver の振る舞いはこの設計の推測の根拠として参照したが、code は読んで写していない（記憶に基づく。§3.4・§3.5 に推測と明記）。
- BIOS の int 16h AH=02h の意味は公開の BIOS interface の事実。

## 13. 敵対的な自己レビュー（設計の担当）

1. **firmware が早い Ctrl を捨てる** → S2 と Ctrl+Space で救済、docs に手順。実機で Ctrl 単独が効かない可能性は残る（推測）。
2. **`SetState(EXPOSED)` が NumLock を消す** → 副作用を document。kernel の keyboard driver の LED の扱いは未確認（残課題）。
3. **壊れた firmware が `ReadKeyStrokeEx` で SUCCESS を返し続ける** → 32 回で止める。
4. **safe の行の誤りが safe boot の時だけ現れる** → 両 mode で検査（§2.2）。
5. **古い BUILD の cfg が作り直されない** → 名前に `-safe1` を足す。
6. **BIOS loader の asm の呼び出しが壊れる** → 既存の入口を wrapper で残す。p002 の受け入れに pcat・pc98 の build を入れる。
7. **record の長さ**: safe mode は `kmsg=console login=console`（26 byte）を足すが `logo=logo.ppm login=graphical kmsg=quiet` を落とすので短くなる。override で長くなる場合は既存の `parameters-too-long` で止まる（H8）。
8. **告知が kernel の console の初期化で消える** → 1 秒の Stall（D5）と debug port。
9. **S2 で safe になった時 `logo_shown` で `quiet_console=1` のまま** → 告知の前に 0 に戻す（§4）。`framebuffer_stage()` は `quiet_boot` を見るので再計算が要る（§7.3）。
10. **試験が console log に頼る** → PNG と SSH の sysctl で判定（§9）。WS013 の OVMF 試験は terminal の文字列で判定しているので流用せず、新しい script を書く。
11. **i915 が scanout を取って text console が見えない** → safe boot の範囲外。実機の text image の結果で分かる（§5）。
