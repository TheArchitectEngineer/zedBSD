# WS174 設計: 起動時の Ctrl で safe boot options に切り替える

Parent: [ws174-p001](phase.md) / [WS174](../ws.md)
版: 第 2 版（2026-10-05 夜。第 1 版に design-reviewer の指摘を反映。反映の一覧は phase.md の「レビュー」節）

## 0. 範囲とユーザーの定義

- ユーザー（2026-10-05 夜）「ブート時にctrlキーが推されていたらsafe boot optionsの設定に切り替えるように、ブートローダとブートコンフィグファイルを変更したいです。設計だけできますか？」
- ユーザーの明確化（同夜、Q1 経由）「safe boot optionsとはグラフィカルブートにせずカーネルのメッセージをコンソールに出力するオプションを選択することです。」

従って **safe boot とは次の 2 つだけ**である。

1. graphical boot をしない: loader の logo（splash）を出さず、`login=graphical` を使わない（console の getty）。
2. kernel の message を console に出す: `kmsg=quiet` を使わない。

build の `ZEDBSD_GRAPHICAL_BOOT=n ZEDBSD_BOOT_KERNEL_MESSAGES=y` が作る config（`build/uat-0505g-text/zedbsd-native-uefi-graphical-n-kmsg-y.cfg` = `kernel=`、`video=640x480`、`rootpart=`、`swap0=`）と同じ選択を、Ctrl で選ぶ。違いは `video=640x480` の扱いだけ（D3: text の config は 640x480 を**必須**で要求するが、safe boot では「希望」にとどめる）。i915 や ACPI を止めるなどの追加は safe boot に**含めない**（§11 の「後の選択肢」に挙げるだけ）。

動機は [BUG-202](../../bugs/BUG-202.md): 実機 5330 で graphical boot の image が kernel の起動の途中で止まるが、logo と `kmsg=quiet` のため画面で読めない。

## 1. 責務と境界

| 構成要素 | 変える | 変えない |
| --- | --- | --- |
| UEFI loader `bootloader/uefi/bootx64.c` | Ctrl の検出（3 点）、safe の parameter record の選択、画面の告知、`kmsg=quiet` の有無の再判定、`A64 PARAMS` に実効の record を出す | handoff の layout（ZBL6 v7）、memory map、ELF の load、volume の発見 |
| 共通 parser `bootloader/uefi/zedbsd-config.c/.h` | `safe.kmsg=console`・`safe.login=console` の行の認識、mode（normal / safe）付きの入口、normal mode での safe の record の長さの空測り | 既存の入口 `zbl_uefi_kern_config_parse()` の意味（normal と同じ結果）、`struct zbl_uefi_kern_config` の大きさ（asm が `ZBL_KERN_CONFIG_RESULT_SIZE` で領域を取る） |
| UEFI の宣言 `bootloader/uefi/include/uefi.h` | Simple Text Input Ex protocol の型と GUID、`EFI_NOT_READY`、`Stall` の型 | 他（`ConIn` は使わないので型を付けない） |
| build `platform/amd64/vmunix.mk` | native UEFI の生成 cfg に safe の 2 行を足す（BIOS 用の cfg は触らない、§2.3） | layout・image の形 |
| BIOS loader `bootloader/pcat/bootzbsd.S` | （別 Phase、ユーザーの判断）int 16h の shift flag で Ctrl を見て safe mode で parser を呼ぶ | — |
| kernel・HAL・UAPI・init・sessiond | **変えない** | — |
| docs | `bootloader/uefi/README.md`・`bootloader/README.md`・`docs/reference/kernel-boot-parameters.md`・`docs/howto/boot-and-storage.md`（`plan/` と Bug への link は書かない） | — |

kernel は safe boot を知らない。loader が渡す record に `logo=`・`login=graphical`・`kmsg=quiet` が無く `kmsg=console login=console` がある、というだけで、kernel と `sessiond`（`kern.boot.login` を読む、`userland/desktop/sessiond/main.c:58`）の既存の動作がそのまま text の boot になる。**handoff・UAPI・HAL の変更は要らない**（§6）。

## 2. boot config の形式

### 2.1 現行の文法（`zedbsd-config.c` の事実）

- 1 行 `name=value`。文字は 0x21〜0x7e、`"` `'` `#` `;` `\` は不可（`unsupported_syntax()`）。`=` の無い行・空の name・空の value は `malformed-line` で **file 全体が拒否**される（`next_line()` 116〜118 行）。
- 行数 64、行 511 byte、file 4096 byte、最終 record 3071 byte。
- `kernel=` だけ loader が消費し、他の行は全部 kernel の parameter token になる（`builder_line()`）。`overlay-root`・`overlay-data`・`swapN` の相対 path は `boot0:` を付ける。
- 名前に `.` を含む既存の token: `display.mode=`・`i915.start=`・`i915.debug=`（`include/kern/boot.h`）。
- kernel は知らない名前を**数えて無視**する（`src/kern/boot.c` 250〜262 行 `record_unknown()`、最初の名前だけ 31 byte まで保持し、`src/kern/main.c` 174〜178 行が 1 行の診断に出す）。重複の error（`EEXIST`）は既知の名前だけ（273〜277 行）。

### 2.2 選んだ形: `safe.kmsg=console`・`safe.login=console`

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

- `safe.` で始まる名前の行は **loader だけの行**（`logo=` と同類）。受け付けるのは **`safe.kmsg=console` と `safe.login=console` の 2 つだけ**（名前と値を固定で照合）。それ以外の `safe.` の行（`safe.logo=`、`safe.kmsg=quiet`、`safe.video=`、`safe.kernel=`、`safe.=x`、`safe.safe.kmsg=`…）は両 mode で `invalid-safe-line` として file を拒否し、同じ行が 2 つなら `duplicate-safe-line`。一般の override（任意の `safe.NAME=VALUE`）は**採らない**: safe boot を config で打ち消せてしまい（`safe.login=graphical`）、ユーザーの定義の外の仕組み（置き換え・`boot0:` の修飾・S1/S2 で効き方が違う token）を持ち込むため。後で safe の set を広げる時は、この許可の表に名前と値を足す（§11）。
- **後方互換**: 古い loader（3 つの BIOS loader を含む）はこの行を知らず、普通の行として kernel に `safe.kmsg=console` を渡す。kernel は未知の名前として無視する（診断 1 行、起動は普通に続く）。`[safe]` のような節の見出しは `=` が無いので古い loader が file ごと拒否して**起動できなくなる**ため選ばない。token の照合は全部「token 全体」で行う（`zbl_parameter_present()`、HAL の `quiet_boot()`、`src/kern/entry.c:310`）ので、古い loader が渡す `safe.kmsg=quiet` が `kmsg=quiet` と誤認されることも無い（この行は新しい parser では拒否されるが、手で書かれた場合の話）。
- **既定（config に safe の行が無い時、D2）**: loader の safe mode は、config の safe の行の有無に関わらず、normal の行のうち **`logo`・`login`・`kmsg`** の 3 つの名前を落とす。この 3 つは kernel・loader の既定が「logo なし・console の login・console に message」なので、落とすだけでユーザーの定義の safe boot になる（`kern.boot.login` は `""` で、sessiond は graphical でないと扱う、`src/kern/sysctl.c:382-385`）。build が足す 2 行は、record を自己記述的にし（`dmesg`・`sysctl kern.boot.login` で `console` と見える）、file を読む人に何が起きるかを示すための明示であって、無くても同じ boot になる。古い zedbsd.cfg（safe の行の無い ESP）でも新しい loader なら Ctrl が効く。
- safe mode の record の組み立て（`zbl_uefi_kern_config_parse_mode(..., SAFE)`）:
  1. `kernel=` は normal と同じく消費する。
  2. `boot0=` の合成（選んだ FAT の UUID）は normal と同じ。
  3. normal の各行: 名前が `logo`・`login`・`kmsg` なら落とす。`safe.` の行は飛ばす。それ以外は normal と同じ処理（`boot0:` の修飾を含む）で出す。
  4. `safe.kmsg=console`・`safe.login=console` の行を file の順に `kmsg=console`・`login=console` として末尾に出す。
  5. record の長さの上限（3071）は組み上がった record に対して判定する。
- **normal mode は safe の record を空測りする**（measure-only の builder: `record == NULL` なら書かずに長さだけ数える。buffer を増やさない。PC-98 の memory のため）。safe の record が `parameters-too-long` になる file は normal の boot でも拒否する。これで、UEFI loader（両 mode を parse する）と BIOS loader（normal だけ）が同じ file を同じ理由で受け入れ・拒否し、Ctrl の時にだけ現れる誤りが無い。許可の表の 2 行は path を含まないので、path の検査の差は生じない。
- `safe=...`（prefix でなく名前そのもの）は普通の未知の名前として kernel に渡る（検査しない。kernel が無視する）。
- 行数 64 の上限は safe の行を含めて数える（変えない。生成する config は 8 行）。

### 2.3 build が作る config

`platform/amd64/vmunix.mk`:

- `$(AMD64_NATIVE_UEFI_ZEDBSD_CONFIG)`（2090 行〜）の recipe の末尾に、`ZEDBSD_GRAPHICAL_BOOT`・`ZEDBSD_BOOT_KERNEL_MESSAGES` の値に関わらず `printf '%s\n' safe.kmsg=console safe.login=console >> $@.tmp` を足す。text の build（graphical=n、kmsg=y）では normal の行に logo/login/kmsg が無いので safe の行は無害（Ctrl を押しても同じ boot）。
- 生成名に format の印を足す（`...-kmsg-$(…)-safe1$(AMD64_BOOT_EXTRA_TAG).cfg`）: recipe の変更は make の依存に現れないので、既存の `BUILD` directory の古い cfg を作り直させるため。`platform/amd64/tools/check-amd64-native-image.py` は ESP の `/zedbsd.cfg` を同じ make 変数の file と比べる（`vmunix.mk` 2110〜2117 行）ので両方が一緒に変わる。
- **`$(AMD64_BIOS_ZEDBSD_CONFIG)`（BIOS/hybrid image の cfg、1983〜1990 行）は p003 では触らない。** 理由: `userland/retro/zedinst/admission.noct` 57〜60 行は installer の source の cfg の名前を `kernel, overlay-root, overlay-data, swap0, boot0, init, video` に限っており、safe の行を足すと text の BIOS image が installer の source にならなくなる。BIOS の Phase（p004）で足す時は zedinst の admission の変更（他の WS の file）を Q1 と調整する。D2 の組み込みの既定があるので、BIOS 用の cfg に行が無くても p004 の Ctrl は効く。
- 静的な `platform/amd64/zedbsd.cfg`・`platform/pcat/*.cfg`・`platform/pc98/*.cfg` も同じ理由で触らない。
- `ZEDBSD_BOOT_EXTRA_LINES`（`display=edp` など）は normal の行として扱う: safe mode では `logo`・`login`・`kmsg` の名前の行だけが落ちる。Guardrail の GPU の開発の config（`ZEDBSD_GRAPHICAL_BOOT=n "ZEDBSD_BOOT_EXTRA_LINES=display=edp login=graphical"`）の `login=graphical` は EXTRA_LINES にあっても safe mode で落ちる（名前で判定するので由来は問わない）。`display=edp` は残る。

### 2.4 互換の行列

| loader | zedbsd.cfg | normal の boot | Ctrl の boot |
| --- | --- | --- | --- |
| 旧 | 旧 | 今と同じ | 今と同じ（Ctrl は無視） |
| 旧 | 新（safe の行あり） | kernel が `safe.kmsg`・`safe.login` を未知の名前として無視（診断「unknown parameter」1 行、count 2）。起動は普通（H10 で確かめる） | 同左 |
| 新 | 旧 | 今と同じ（record は byte 単位で同じ、H1） | logo/login/kmsg を落とした record で起動（safe） |
| 新 | 新 | safe の行を落とした record（旧 loader + 旧 cfg と同じ） | 上に加えて `kmsg=console login=console` を付けた record |

## 3. UEFI loader の Ctrl の検出

### 3.1 使う firmware の口

`uefi.h` に足す宣言（UEFI 仕様 2.x の事実。GUID・layout・定数の値だけを書き、仕様書や EDK2 の文は写さない）:

```c
typedef struct { uint16_t ScanCode; CHAR16 UnicodeChar; } EFI_INPUT_KEY;	/* 4 byte */
typedef struct { UINT32 KeyShiftState; uint8_t KeyToggleState; } EFI_KEY_STATE;
typedef struct { EFI_INPUT_KEY Key; EFI_KEY_STATE KeyState; } EFI_KEY_DATA;	/* 12 byte */

typedef struct efi_simple_text_input_ex_protocol {
	EFI_STATUS (EFIAPI *Reset)(struct efi_simple_text_input_ex_protocol *, BOOLEAN);
	EFI_STATUS (EFIAPI *ReadKeyStrokeEx)(struct efi_simple_text_input_ex_protocol *, EFI_KEY_DATA *);
	EFI_EVENT WaitForKeyEx;
	EFI_STATUS (EFIAPI *SetState)(struct efi_simple_text_input_ex_protocol *, uint8_t *);
	void *RegisterKeyNotify;
	void *UnregisterKeyNotify;
} EFI_SIMPLE_TEXT_INPUT_EX_PROTOCOL;

#define EFI_NOT_READY                (0x8000000000000006ULL)
#define EFI_SHIFT_STATE_VALID        0x80000000U
#define EFI_RIGHT_CONTROL_PRESSED    0x00000004U
#define EFI_LEFT_CONTROL_PRESSED     0x00000008U
#define EFI_TOGGLE_STATE_VALID       0x80U
#define EFI_KEY_STATE_EXPOSED        0x40U
/* EFI_SIMPLE_TEXT_INPUT_EX_PROTOCOL_GUID dd9e7534-7762-4698-8c14-f58517a625aa */
typedef EFI_STATUS (EFIAPI *EFI_STALL)(UINTN microseconds);
```

`EFI_BOOT_SERVICES.Stall` に `EFI_STALL` の型を付ける（slot の位置は変わらない）。`ConIn` は使わないので型を付けない（検出は `ConsoleInHandle` の Ex protocol で行う）。

### 3.2 2 つの方法の比較と選択

| 方法 | 分かること | 問題 |
| --- | --- | --- |
| A. `EFI_SIMPLE_TEXT_INPUT_EX_PROTOCOL.ReadKeyStrokeEx` の `KeyState.KeyShiftState` | 鍵の event に付く修飾 key の状態（Ctrl 左右）。`SetState(EFI_KEY_STATE_EXPOSED)` を受ける firmware では **Ctrl 単独の押下**も「key の無い event」として読める | event が queue に無いと `EFI_NOT_READY`。押下の**遷移**が event になるのであって、押しっぱなしの**状態**は読めない（§3.4） |
| B. `EFI_SIMPLE_TEXT_INPUT_PROTOCOL.ReadKeyStroke` | 文字と scan code だけ | Ctrl 単独は event にならない。Ctrl+文字は制御文字（0x01〜0x1a）になるが Tab・BS・CR・LF と区別が付きにくい |

**A を選ぶ**（D6）。B は退避にもしない: 制御文字の判定は曖昧で、誤って safe になる方が困る。Ex の無い firmware（UEFI 2.1（2007）より前）では Ctrl の safe boot は無い、と document する。

### 3.3 標本を取る手順（`bootloader/uefi/safe-key.c/.h`、新 file）

```c
/* 呼び手が持つ状態。file scope の static は置かない（host 試験で作り直せるように）。 */
struct zbl_uefi_safe_key {
	EFI_SIMPLE_TEXT_INPUT_EX_PROTOCOL *input;	/* 0: 使えない */
	int exposed;					/* SetState(EXPOSED) が通った */
	int pressed;					/* どれかの標本で Ctrl を見た */
};

/* protocol を探し、EXPOSED を頼む。失敗しても boot は止めない。 */
void zbl_uefi_safe_key_open(struct zbl_uefi_safe_key *key, EFI_SYSTEM_TABLE *system);
/* queue を読み切る。1: この標本か前の標本で Ctrl を見た。 */
int zbl_uefi_safe_key_sample(struct zbl_uefi_safe_key *key);
/* 純粋な判定。host 試験 K1 の対象。 */
int zbl_uefi_safe_key_from_state(const EFI_KEY_DATA *data);
```

1. `open`: `system->ConsoleInHandle` が 0 でなければ `boot->HandleProtocol(ConsoleInHandle, &EX_GUID, &input)`、失敗なら `boot->LocateProtocol(&EX_GUID, 0, &input)`。どちらも失敗なら `input = 0`。`input->SetState(input, &toggle)`（`toggle = EFI_TOGGLE_STATE_VALID | EFI_KEY_STATE_EXPOSED`）。失敗は無視（`exposed = 0`: Ctrl 単独は読めず、Ctrl+key だけが効く）。副作用: `SetState` は NumLock・CapsLock・ScrollLock の bit も書くので firmware の LED の状態が消える（通常の起動時は NumLock off が普通。kernel の keyboard driver が自分の LED を設定するかは**未確認**。§11 の残課題）。費用: LED の更新は USB の control transfer か PS/2 の command で**ms の桁**と見込む（計っていない）。待ち時間は足さない。
2. `sample`: `input` が 0 なら `pressed` を返す。`ReadKeyStrokeEx` を **最大 32 回**、`EFI_NOT_READY`（または他の失敗）まで繰り返す（成功を返し続ける壊れた firmware で止まらないための上限）。どれかの `EFI_KEY_DATA` が `from_state()` で真なら `pressed = 1`。
3. `from_state`: guard の `if` を 2 つ: `KeyShiftState & EFI_SHIFT_STATE_VALID` が 0 なら 0、`KeyShiftState & (EFI_LEFT_CONTROL_PRESSED | EFI_RIGHT_CONTROL_PRESSED)` が 0 なら 0、残れば 1。Key の中身（空でも、文字でも）は見ない。従って **Ctrl 単独（EXPOSED が効く firmware）も、Ctrl+任意の key も** safe になる。
4. 読んだ event は捨てる（kernel の keyboard driver は自分で初期化するので、loader の間に打たれた key が kernel に届かないのは今と同じ）。

### 3.4 いつ標本を取るか（起動を遅らせない）

待ち時間は**足さない**（ユーザーの要求「no key の時に boot を遅らせない」）。loader の実行の間に 3 点で標本を取る。

- **S0**: `efi_main` の入口、`A64 UEFI ENTRY` の直後（GOP の前）。ここで `open`（EXPOSED を頼む）と最初の `sample`。目的: EXPOSED を**できるだけ早く**設定して、以後の Ctrl 単独の押下が event になる窓を広げる。volume の発見と config の読み出し（`bootx64.c` 1438〜1444 行）の間に押された Ctrl が S1 で読める。POST の間に firmware が queue を flush しないなら、その前に打った Ctrl+key もここで読める。
- **S1**: config を読んで parse した直後、`zbl_uefi_video_select()` と `show_logo()` の**前**（1446 行の前）。ここまでに Ctrl を見ていれば: parameter record は safe のもの、`logo_named` は 0、video の希望は D3 の 640x480（§3.6）、logo を読まない、`quiet_boot` は 0。
- **S2**: kernel の ELF を読み終えて volume を閉じた直後、`build_bootstrap()` の**前**（1531〜1543 行の間）。S1 で normal だったが S2 で Ctrl を見たら: handoff に入れる record を safe のものに替え、`quiet_boot` を 0 にし（stage block を描き、`zbl_transition_start` から入る。`quiet_boot` は 1673・1686・1691 行の判定だけが読むので再計算で足りる）、告知を出す（`context.quiet_console = 0` にしてから。`fail_status()` 309 行と同じ）。video と logo は既に決まっているのでそのまま（kernel の console が `kmsg=console` で画面を消して描く、`cons.c` 387〜391 行）。

**Ctrl を電源投入から押しっぱなしにするだけで検出できるかは firmware 次第で、未確認。** EDK2 系の keyboard driver は修飾 key 単独の押下を、EXPOSED が設定される前なら event にせず捨て、押しっぱなしを自動で繰り返さない（**推測、EDK2 の記憶**）。S0 で EXPOSED を頼んでも、電源投入の直後の押下は loader の前に起きている。PS/2 の keyboard が Ctrl の make code を typematic で繰り返すなら（5330 の内蔵 keyboard、**推測**）効くかもしれないが、頼らない。

従って docs に書く**主の手順**は: 「電源を入れたら **Ctrl を押したまま、Space を繰り返し叩く**（zedBSD の logo が出るまで、または kernel の message が流れ始めるまで）。Ctrl+Space の event は EXPOSED の有無によらず queue に入り、loader の 3 点のどれかで読まれる。」Ctrl 単独で効く firmware があればそれは firmware の性質として記録する（9.3）。

### 3.5 firmware の癖への備え

- USB keyboard が loader の時点で未初期化: firmware の BDS が console を connect してから loader を起動するのが普通で、`ConsoleInHandle` の Ex は ConSplitter 経由。無ければ `input = 0` で普通に起動する。
- BDS や boot の hotkey の handler が `StartImage` の前に ConIn を flush するか（POST の間の Ctrl+Space が消えるか）は firmware 次第で**未確認**。S0〜S2 の窓（loader の実行の間）に打てば読める。
- 誤検出（firmware が Ctrl の bit を立てっぱなしにする等）の害は text の boot になることだけ。告知で分かる。
- QEMU/OVMF（`boot-test.sh` は `-device usb-kbd` を xHCI に付ける。`-machine q35` の i8042 もある）: `ConsoleInHandle` の Ex がどちらの keyboard の event を流すかは**未確認**。試験では key の送り先を `usb-kbd` に `id` を付けて `input-send-event` の `device` で指定する（§9.2）。OVMF の UsbKbDxe が EXPOSED に対応している前提の cell（Q1）と、対応に依らない Ctrl+Space の cell（Q2）の両方を流す。

### 3.6 safe mode の video mode（D3）

`zbl_uefi_video_select()` の第 5・6 引数は「希望」で、firmware に無くても現在の mode のまま成功する（`video.c` 45〜53 行「only a wish」）。logo の 1920x1080 の希望はこの経路で、`video=` の明示だけが「必ず満たす」要求（失敗で boot が止まる）。safe mode では S1 で safe になった時に **640x480 を希望**として渡す（`video=` の行があればそちらが勝つ）。理由: HAL の早期 console は 80 桁の固定の文字の面を framebuffer の中央に置く（`cons.c` 364〜366 行 `framebuffer_x = (width - 80*8)/2`）ので、1920x1080 のままだと文字が画面の中央に小さく写り、写真で読みにくい。640x480 なら text の image と同じ見え方になる。希望なので無い firmware では現在の mode のまま。これはユーザーの定義の「graphical boot をしない」の中の text の見え方の話で、追加の機能ではないが、D3 でユーザーに確かめる。

### 3.7 BIOS（PC/AT）の経路

`bootloader/pcat/bootzbsd.S` は real mode で `load_kern_configuration` から parser を呼ぶ（922〜935 行、`.code16gcc` で 5 つの引数を push）。Ctrl の検出は **int 16h AH=02h**（AL の bit 2 = Ctrl が押されている。BIOS の keyboard handler が BDA 0040:0017 に押下中の状態を保持するので、押しっぱなしで読める。event ではなく状態なので UEFI より素直）。safe なら `zbl_uefi_kern_config_parse_mode` を mode=SAFE で呼ぶ（push を 1 つ足し `add esp, 24`）。告知は `puts`（msg_safe_boot）。amd64 BIOS（同じ `BOOTZBSD.EXE`）も同じ。PC-98 は `int 18h AH=02h` の key 状態の bitmap で Ctrl を読めるはずだが（**bit の位置は未確認**）、PC-98 には graphical boot が無く動機が薄い。

**提案**: PC/AT BIOS は小さい別 Phase（p004）として用意し、やるかどうかはユーザーの判断（D4）。PC-98 は Future Work。p002 の parser の変更で BIOS loader の build と大きさが壊れないこと（§10）は p002 の受け入れ条件に入れる。

## 4. 画面の告知と kernel・init への伝達

- 告知: `console_ascii(&context, "Safe boot (Ctrl): no logo, kmsg=console, login=console\n")`。S1 で safe なら `quiet_console` は 0 のままなので ConOut に普通に出る。S2 で logo の後なら `context.quiet_console = 0` にしてから出す。debug port（0xe9）にも常に出る。`A64 PARAMS` の行（811〜813 行）は normal の record を出しているので、S1・S2 の決定の後に**実効の record** を `A64 PARAMS SAFE ...` として出し直す（debug port だけでよい）。
- safe mode だけ `boot->Stall(1000000)`（1 秒）を告知の後に入れて読めるようにする（normal の boot は遅れない）。D5。
- kernel は record の `kmsg=console`・`login=console` を今の parser で受ける（`parameter_word()` 390〜393 行）。`sysctl kern.boot.login` が `console` を返す。`sessiond` は `console` を見て終わり、init が `getty_console` を始める（`docs/reference/kernel-boot-parameters.md` §7b の既存の動作）。
- **新しい kernel parameter は足さない**（`safe=1` は後の選択肢 §11）。handoff の `flags` に bit を足すことも、HAL の `hal.h` も、UAPI も**変えない**。

## 5. safe mode で何が画面に残るか（panic と、BUG-202 の期待の整理）

事実（source）:

- `kmsg=console` のとき HAL の早期 console は framebuffer を黒く消して描く（`src/hal/amd64/bsp-pcat/cons.c` 376〜398 行。`kmsg=quiet` の時だけ `console_suspended=1` で描かない）。kernel の text console は `text_hidden = kern_log_quiet()` = 0 で最初から表示（`src/drivers/platform/pcat/graphics/text.c:542`）。
- panic（`__libc_panic`・`kern_fatal`、`src/kern/panic.c` 71・114 行）と supervisor の fault（`src/kern/user-probe.c` 273〜284 行、その後 `src/hal/amd64/int.c` の `amd64 fault` と `HAL_FATAL`）は、**quiet な graphical boot でも** `kern_text_reveal_fatal()` で console を出す（BUG-158 の直し、`text.c:936`: graphics mode が text を suspend していても、lock をこの CPU が持っていても描く）。

従って BUG-202 で「何も出なかった」停止は、kernel の text console が登録された後の panic・fault では**ない**可能性が高く、次のどれかである:

| 種類 | graphical boot で見えるか | safe boot で見えるか |
| --- | --- | --- |
| (a) kernel の text console が登録される前の停止（HAL の早期 console が `console_suspended=1` の間: `cons.c` 383 行） | 見えない | **見える**（早期 console が描いている。最後の行まで残る） |
| (b) panic しない hang | 見えない（splash の spinner が止まるだけ） | **最後の message まで見える** |
| (c) reset・reboot | 見えない | 一瞬だけ見える（写真かビデオ） |
| (d) display を graphics が取った後の停止 | reveal_fatal で出るはず | 同左 |

safe boot が直すのは (a)・(b) の「最後の message が読めない」ことで、panic の message の表示そのものは既にある。**9.3 の実機の確認は「kernel の最後の message が画面に残る」で判定する**（「panic が読める」ではない）。追加の実装は要らない。残る限界（document に書く、変えない）:

- HAL の早期 console が立ち上がる前の停止（`locore`〜`cons` の初期化の間）は画面に出ない。どの設定でも同じ。
- `login=console` でも i915 の driver は `i915.start=auto`（既定）で起動する。firmware の pipe の引き継ぎ（`i915_resident_takeover`、`display/modeset.c:1736`）は `drv_i915_present_window` の中で、worker は最初の present でそこに入る（`worker.c` 343〜363 行）ので、console の login では takeover は起きないと見る（**推測**）。probe の時の電源や DC の変更が scanout に影響するかは**未確認**。BUG-202 の text の image `build/uat-0505g-text` を実機で起動した結果がその証拠になる。
- text console が引き継いだ後も HAL の console は `console_suspended=0` のままなので（`cons.c` が 1 にするのは 383 行だけ）、両方が同じ framebuffer に描き得る。遅い `hal_printf` の fault の行が読めるかは**未確認**。
- panic の後に scroll はしない（最後の画面が残る）。

## 6. handoff・UAPI・HAL の変更の要否

- handoff: parameter record の text だけが変わる。`build_bootstrap()` は pointer から `sizeof` で写す（1373 行）ので `&effective->parameter_record` を渡すだけで ZBL6 v7 の layout は不変。
- UAPI: `kern.boot.login` は既存。新しい sysctl は無い。
- HAL: `src/hal/x86/boot-parameters.c` は record を写すだけ。`cons.c` は `kmsg=quiet` の token を見るだけ。**hal.h も `src/hal/` も変えない。承認の要る差分は無い。**

## 7. 内部 interface（実装 Phase への指示）

### 7.1 `bootloader/uefi/zedbsd-config.h`

```c
enum zbl_kern_config_mode {
	ZBL_KERN_CONFIG_MODE_NORMAL = 0,
	ZBL_KERN_CONFIG_MODE_SAFE = 1
};
/* 末尾に足す result */
	ZBL_UEFI_KERN_CONFIG_INVALID_SAFE_LINE,
	ZBL_UEFI_KERN_CONFIG_DUPLICATE_SAFE_LINE

enum zbl_uefi_kern_config_result zbl_uefi_kern_config_parse_mode(
	struct zbl_uefi_kern_config *configuration,
	const void *source, size_t source_size,
	const char *selected_uuid, size_t selected_uuid_capacity,
	enum zbl_kern_config_mode mode);
/* 既存。parse_mode(..., NORMAL) と同じ。BIOS loader の asm の呼び出しを変えないために残す。 */
enum zbl_uefi_kern_config_result zbl_uefi_kern_config_parse(...);
```

`struct zbl_uefi_kern_config` は変えない（`_Static_assert` の大きさを保つ。asm は result が 0 以外かだけを見るので enum の追加は無害）。

### 7.2 parser の内部（`zedbsd-config.c`）

- 許可の表: `static const struct { const char *name; const char *value; } safe_lines[] = { {"kmsg","console"}, {"login","console"} };`。
- `line_safe_index(const struct config_line *line)`: 名前が `safe.` で始まるなら、`safe.` を外した名前と値を表と照合して index（0・1）を返す。表に無ければ -1（→ `INVALID_SAFE_LINE`）。`safe.` で始まらなければ -2。
- 第 1 pass（`kernel`・`boot0` を数える既存の loop）に足す: `safe.` の行は index を取り、不正なら `INVALID_SAFE_LINE`、同じ index が既出なら `DUPLICATE_SAFE_LINE`（index ごとの bit で覚える。配列は要らない）。
- 第 2 pass（record の組み立て）: `safe.` の行は飛ばす（両 mode）。`mode == SAFE` のとき名前が `logo`・`login`・`kmsg` の行を飛ばす。
- 第 3 pass（`mode == SAFE` だけ）: 第 1 pass で見た index の順でなく **file の順**に、`safe.` の行を `text+5, length-5, equal-5` の view で `builder_line()` に渡す（`builder_line()`・`line_needs_boot0()` は `struct config_line` の view を取るので再利用できる。2 行は path を含まないので修飾は起きない）。
- measure-only: `struct parameter_builder` に `record == NULL` を許し、`builder_append()` は NULL なら長さだけ進める。`mode == NORMAL` の `parse_mode` は、normal の record を作った後、`record = NULL` の builder で SAFE の組み立てをもう一度走らせ、`PARAMETERS_TOO_LONG` なら file を拒否する（第 2・3 pass を関数にまとめて 2 回呼ぶ）。buffer は増やさない。
- 新しい関数は `plan/coding-style.md` の形（forward declaration の block、意味の段落の comment、guard の `if`）で書く。既存の関数の既存の逸脱（`for` の初期化の宣言など）は WS174 では直さない（p005 の見直しの範囲は**新しい関数と変えた関数**。file 全体の restyle はしない）。

### 7.3 `bootloader/uefi/bootx64.c`

- `struct loader_context` に `struct zbl_uefi_safe_key safe_key;`。
- `static struct zbl_uefi_kern_config configuration;` に加えて `static struct zbl_uefi_kern_config safe_configuration;`（それぞれ 256 + 3088 byte の `.bss`）。`load_selected_configuration()` は読み込んだ `config_buffer` から **両方を parse** する（file は 1 回だけ読む）。どちらかが OK でなければ今と同じく `zedbsd.cfg rejected: <name>` で止める（§2.2 の空測りで、normal が通って safe だけが落ちる file は無い。両方を検査するのは parser の不変条件の確認で、片方の結果を捨てない）。
- S0: `A64 UEFI ENTRY` の直後に `zbl_uefi_safe_key_open()`・`sample()`。S1・S2 で `sample()`。
- `const struct zbl_uefi_kern_config *effective = &configuration;` を S1 で選び、以降の `zbl_logo_path`・`zbl_uefi_video_select`（safe なら希望 640x480）・`show_logo`・`zbl_parameter_present(kmsg=quiet)`・`kernel_placement_parse` は `effective` を読む。S2 で `effective` を替え、`quiet_boot` を再計算し、`build_bootstrap(..., &effective->parameter_record, ...)` に渡す。
- 告知・`A64 PARAMS SAFE`・Stall（§4）。

### 7.4 `bootloader/uefi/safe-key.c/.h`（新 file）

§3.3 の struct と 3 関数。`uefi.h` の宣言だけに依存し、`bootx64.c` の static には触らない。file の先頭は coding-style §13 の複数行の header（`zedBSD` / `Copyright (C) 2026 Awe Morris` / `SPDX-License-Identifier: Zlib`）と file の説明の comment。

### 7.5 build の rule

`platform/amd64/vmunix.mk` の `$(BUILD)/uefi/bootx64.o`（555 行〜）と同じ形で `safe-key.o` の rule を足し、UEFI loader の link の object に加える。BIOS の helper（`AMD64_BOOTZBSD_HELPERS`・`PCAT_BOOTZBSD_HELPERS`・`PC98_BOOTZBSD_HELPERS`）は `zedbsd-config.c` を i386 で compile しているので、新しい入口が増えても object は 1 つのまま。

## 8. 失敗と回復・並行性・資源

- 失敗はどれも boot を止めない: protocol が無い・`SetState` 失敗・`ReadKeyStrokeEx` の error → normal の boot。config の safe の行の誤りだけは（normal の boot でも）`zedbsd.cfg rejected: invalid-safe-line` 等で止める（今の「不正な config は見える形で止める」方針のまま）。
- 並行性: loader は single thread、boot services の中で TPL は application。event の待ちは使わない（`WaitForEvent` を使わない。待たないため）。
- 資源: 追加の pool allocation は無い。`safe_configuration` は static。`config_buffer` は今と同じ 1 回の読み出し。
- BIOS loader の大きさ: parser の object は 3 つの BIOS loader に共通（今 `.text` 2775 byte、`build/amd64/bootloader/bios-zedbsd-config.i386.o`）。amd64 BIOS は `stage2_end = 0xb596`、上限 `0xd000`（`bootzbsd.ld`）で 6762 byte の余裕。**pcat（i386）と pc98 は build が無く未測定**（pc98 の上限は `stage2.ld` の `0x8000`）。p002 は 3 つの loader の `stage2_end` を前後で記録し、safe の論理（許可の表の照合と measure-only）を小さく保つ。
- 割込み・DMA: 無関係。

## 9. 試験計画

判定は **QMP の screendump の PNG と、guest への SSH/serial の問い合わせ**で行い、QEMU の console log・serial log では判定しない（Guardrail）。実装の担当は QEMU を起動せず、T1 に依頼する。

### 9.1 host 試験（`plan/ws174/tests/`、p002・p003 の受け入れ）

`run-zedbsd-config-safe-host-test.sh` が `bootloader/uefi/zedbsd-config.c` と `zedbsd-config-safe-host-test.c` を host の cc で compile して流す（通常と `-fsanitize=address,undefined` の 2 回。`zedbsd-config.c` は `<stddef.h>` と `kern/boot.h` だけに依存し host で compile できる）。

| 番号 | 入力 | 期待 |
| --- | --- | --- |
| H1 回帰 | safe の行の無い現行の 3 つの cfg（`platform/amd64/zedbsd-native-uefi.cfg` に graphical の行を足したもの、`zedbsd.cfg`、`zedbsd-native.cfg`） | `parse()` と `parse_mode(NORMAL)` の record が byte 単位で今の出力と同じ（期待値は試験に固定で書く） |
| H2 normal | 生成する cfg（§2.2 の例） | NORMAL: `boot0=UUID=… rootpart=… swap0=… logo=logo.ppm login=graphical kmsg=quiet`（safe の行が無い） |
| H3 safe | 同上 | SAFE: `boot0=UUID=… rootpart=… swap0=… kmsg=console login=console` |
| H4 既定 | safe の行の無い graphical の cfg | SAFE: logo/login/kmsg が落ちるだけ |
| H5 順 | `safe.login=console` を `safe.kmsg=console` より前に書く | SAFE: `login=console kmsg=console`（file の順） |
| H6 EXTRA | `display=edp`・`login=graphical` を normal の行に持つ text の cfg | SAFE: `display=edp` は残り `login=graphical` は落ちる |
| H7 拒否 | `safe.kmsg=quiet`、`safe.login=graphical`、`safe.logo=logo.ppm`、`safe.video=640x480`、`safe.kernel=x`、`safe.=x`、`safe.safe.kmsg=console`、`safe.kmsg=console` の重複 | 両 mode で `invalid-safe-line` / `duplicate-safe-line` |
| H8 境界 | 64 行（safe の行を含む）。normal の record が 3071 に収まり、safe の record（logo/login/kmsg を落として 26 byte 足す）が越える file と越えない file | 越える file は **両 mode** で `parameters-too-long`（空測り）、越えない file は両 mode で OK |
| H9 CRLF | H2 の CRLF 版 | H2・H3 と同じ |
| H10 旧 loader | 新しい cfg を**行のまま**（旧 parser の出力と同じ text: `boot0=UUID=… rootpart=… swap0=… logo=logo.ppm login=graphical kmsg=quiet safe.kmsg=console safe.login=console`）kernel の parser `kern_boot_parameters_parse()`（`src/kern/boot.c`、BR-T42 の host 試験の対象だった）に渡す。`src/kern/boot.c` が host で compile できなければ、旧 parser（`git show <p002 の前の SHA>:bootloader/uefi/zedbsd-config.c`）の出力を host で作り、text を目で確かめて記録する | 0 を返し、`unknown_count` が 2、`unknown_name` が `safe.kmsg`、`login` の値が `graphical` |
| K1 判定 | `EFI_KEY_DATA` の表: VALID+LEFT_CTRL、VALID+RIGHT_CTRL、VALID のみ、LEFT_CTRL だが VALID 無し、空 | `zbl_uefi_safe_key_from_state()` が 1,1,0,0,0 |
| K2 走査 | mock の system table（`ms_abi` の関数 pointer。WS013 の `uefi-volume-discovery-test.c` と同じ手法）: `ConsoleInHandle` 0 と `LocateProtocol` 失敗 / `SetState` 失敗 / 3 つ目の event に Ctrl / 常に SUCCESS で空の event を返す / 2 回目の `sample` で前の `pressed` が残る | 0 / `exposed=0` で動く / 1 / 32 回で止まり 0 / 1 |

### 9.2 QEMU（T1 に依頼、p003 の受け入れ）

image: `plan/ws174/tests/config-amd64-safe.mk` は `plan/tools/guest/config-amd64-ssh.mk` を include し（他の WS の `tests/` は include しない。WS159 の完了で消える）、`ZEDBSD_GRAPHICAL_BOOT := y`・`ZEDBSD_BOOT_KERNEL_MESSAGES := n` を置く。`plan/tools/guest/test-image.sh` で build。生成された cfg に safe の 2 行があることを build の後に `grep` で確かめる。

QEMU の起動は `boot-test.sh` と同じ形（q35、OVMF、`-device qemu-xhci` + `-device usb-kbd,id=kbd0,...`、QMP socket）。新しい script `plan/ws174/tests/run-safe-key-qemu.sh` が **1 つの QEMU のインスタンス**で cell を順に流し、cell の間は QMP `system_reset`（NVRAM は共有でよい）:

| cell | 操作 | 判定 |
| --- | --- | --- |
| Q1 safe (Ctrl 単独) | reset の直後から、QMP `input-send-event`（`device: kbd0`）で `ctrl` の down 50 ms・up 50 ms を繰り返す。やめる条件: screendump の `read_text()`（`boot-test.py` の font 読み）に kernel の text か logo の画面が写ったら（1〜2 秒ごとに撮る）、または 60 秒 | (a) kernel の text が写り logo が無い screendump が 1 枚ある。(b) login prompt まで達する（`boot-test.py` の判定）。(c) SSH で `sysctl kern.boot.login` が `console`。 |
| Q2 safe (Ctrl+Space) | 同じ条件で `send-key` の `ctrl-spc`（hold 50 ms）を繰り返す（EXPOSED に依らない経路） | Q1 と同じ |
| Q3 control | key を送らない | screendump に logo（splash）が写り、`sysctl kern.boot.login` が `graphical`（framebuffer の QEMU では greeter が終わって getty になるのは従来どおりなので login prompt は両方で出る。差は (a) と (c)） |
| Q4 誤検出なし | Q3 の後、kernel が動いてから `ctrl` を送る | 影響なし（login の値は `graphical` のまま） |

Q1 の PASS は「loader の実行中の Ctrl の押下の**遷移**が検出される」ことの証拠であって、「電源投入から押しっぱなしで効く」ことの証拠では**ない**（§3.4）。Q1 と Q2 の両方が FAIL なら p003 は uncleared、Q2 だけ PASS なら OVMF の UsbKbDxe の EXPOSED の対応の事実として記録して p003 は cleared（Ctrl 単独は firmware 次第と document する）。

### 9.3 実機（ユーザー、5330。WS の受け入れ）

image は 9.2 と同じ config（または UAT の config）で build し USB に書く。手順: 電源 → Ctrl を押したまま Space を繰り返し叩く → loader の告知 `Safe boot (Ctrl)` が 1 秒見え、kernel の message が流れる → 止まった画面を写真に。判定は「**kernel の最後の message が画面に残る**」。それが BUG-202 の解析の入力になる。Ctrl 単独（Space なし）でも効くかは別に試し、firmware の事実として記録する。実機の証拠は QEMU と分けて phase.md に書く。この確認は WS の受け入れの項目で、どの実装 Phase の cleared にも含めない（ws.md に「実機の確認」の行を持つ。担当はユーザーと Q1）。

## 10. 実装 Phase の分け方と受け入れ条件

| Phase | 内容 | 受け入れ条件 | 依存 |
| --- | --- | --- | --- |
| **ws174-p002** parser | `zedbsd-config.c/.h` の `parse_mode`・許可の表・measure-only・result の名前。host 試験 H1〜H10。`bootloader/README.md` の文法の節 | host 試験 PASS（通常 + ASan/UBSan）。amd64 の `make -j16` で `BOOTX64.EFI`・`BOOTZBSD.EXE` が warning 0、pcat（i386）と pc98 の `BOOTZBSD.EXE` も warning 0 で build できる。3 つの BIOS loader の `stage2_end` を前後で記録し ld の上限に収まる。H1 で現行の record と byte 単位で同じ | — |
| **ws174-p003** UEFI loader と build | `uefi.h` の宣言、`safe-key.c/.h`、`bootx64.c` の S0〜S2・告知・`A64 PARAMS SAFE`・Stall・640x480 の希望、`vmunix.mk` の native UEFI の cfg の safe の行と名前、host 試験 K1・K2、docs（`bootloader/uefi/README.md`・`docs/reference/kernel-boot-parameters.md` §7c・`docs/howto/boot-and-storage.md`。`plan/`・Bug への link を書かない、断定で書く） | build warning 0。K1・K2 PASS。生成 cfg に safe の 2 行。`check-amd64-native-image.py` PASS。T1 の Q1〜Q4（9.2 の判定） | p002 |
| **ws174-p004** BIOS PC/AT（任意、D4） | `bootzbsd.S` の int 16h と `parse_mode(SAFE)` の呼び出し、`msg_safe_boot`、`bootloader/README.md`。BIOS 用の cfg に safe の行を足すなら zedinst の admission を Q1 と調整。QEMU（SeaBIOS、`bios-hdd-image.img`）で Q1 相当（BIOS は状態を読むので、Ctrl を押しっぱなし（down のまま）で loader の時刻を跨ぐ） | pcat・amd64 BIOS の build warning 0、`stage2_end` の記録、T1 の BIOS cell PASS | p002 |
| **ws174-p005** 規約の全文の見直し | WS の変えた C（新しい関数と変えた関数）を `plan/coding-style.md` の全文と照らす | 指摘 0 か記録された例外 | p003（p004 をやるならその後） |

WS の受け入れ（「panic などが画面で読める」= kernel の最後の message が実機の画面に残る）は 9.3 で判定し、ws.md に実機の確認の行を持つ。QEMU で panic を起こす試験は作らない（細かい確認を厚くしない方針）。

## 11. 判断の項目（ユーザーへ）と推奨

| ID | 問い | 推奨 | 理由 |
| --- | --- | --- | --- |
| D1 | config の形は `safe.kmsg=console`・`safe.login=console` の 2 行（loader だけの行、許可の表で固定。`[safe]` の節と一般の `safe.NAME=VALUE` の override は不採用）でよいか | はい | 古い loader が file を拒否しない唯一の形。許可の表で safe boot を config で打ち消せない。ユーザーの定義どおりの最小 |
| D2 | config に safe の行が無くても、Ctrl で `logo`・`login`・`kmsg` を落とす（loader 組み込みの既定）でよいか | はい | 既に書き込まれた ESP の cfg でも効く。BIOS 用の cfg に行を足せない（zedinst）間も効く |
| D3 | safe mode で GOP に 640x480 を**希望**（無ければ現在の mode のまま、boot は止まらない）してよいか | はい | 80 桁の console が framebuffer の中央に固定で置かれるので、1920x1080 のままだと文字が小さく写真で読みにくい。text の image と同じ見え方になる。希望なので失敗しない |
| D4 | BIOS PC/AT（p004）をこの WS でやるか | **後回し**（p004 を planned のまま置く） | 動機の実機は UEFI。p002 で BIOS loader の build と大きさは守る |
| D5 | safe mode だけ告知の後に 1 秒止めるか | はい | normal は遅れない。safe を選んだ人が告知を読める |
| D6 | 検出は Ex protocol だけ（simple の `ReadKeyStroke` の制御文字は使わない）でよいか | はい | 誤検出を避ける。Ex の無い firmware では Ctrl の safe boot は無い、と document |
| D7 | S0（入口）・S1・S2 の 3 点で標本を取り、待ち時間は足さない。docs の主の手順は「Ctrl を押したまま Space を繰り返し叩く」でよいか | はい | 電源投入からの押しっぱなしが効くかは firmware 次第で未確認。遅延 0 |
| D8 | 許可の表に無い `safe.` の行は file ごと拒否してよいか | はい | 誤った safe の行を normal の boot でも見つける。古い loader は未知の行として kernel に渡すだけなので互換は保つ |

後の選択肢（この WS では**やらない**。ユーザーの定義の外。Future Work に載せるかは Q1）:

- kernel parameter `safe=1` と `sysctl kern.boot.safe`（userland が safe boot を知る）。
- 許可の表に `i915.start=manual` を足す（i915 が scanout を取って console が消える場合。§5 の未確認が実機で問題になった時に検討）。
- PC-98 の Ctrl（int 18h）。
- BIOS 用の cfg への safe の行（zedinst の admission の変更が要る）。

## 12. ライセンスと転記

- 新しい code（`safe-key.c/.h`、parser の変更、試験）は coding-style §13 の header（`zedBSD` / `Copyright (C) 2026 Awe Morris` / `SPDX-License-Identifier: Zlib`）。
- `uefi.h` に足す宣言は UEFI 仕様の **事実**（GUID の値、構造体の member の順、定数）だけを自分の書き方で書く。UEFI 仕様の文章・EDK2（BSD-2-Clause-Patent）の code や comment は写さない。EDK2 の keyboard driver の振る舞いはこの設計の推測の根拠として参照したが、code は読んで写していない（記憶に基づく。§3.4・§3.5 に推測と明記）。
- BIOS の int 16h AH=02h の意味は公開の BIOS interface の事実。

## 13. 敵対的な自己レビューと未確認の事項

1. **電源投入からの Ctrl の押しっぱなしは firmware 次第**（EDK2 系は修飾 key 単独の早い押下を捨て、自動で繰り返さない、と推測）→ S0 で EXPOSED を早く設定、S0〜S2 の 3 点、主の手順は Ctrl+Space の繰り返し。Q1 の PASS を押しっぱなしの証拠にしない。5330 の firmware が EXPOSED を受けるか、内蔵 keyboard が Ctrl を typematic で繰り返すか、BDS が `StartImage` の前に ConIn を flush するかは**未確認**（9.3 で記録）。
2. **`SetState(EXPOSED)` が NumLock を消す** → 副作用を document。kernel の keyboard driver の LED の扱いは未確認（残課題）。費用は ms の桁と見込む（計っていない）。
3. **壊れた firmware が `ReadKeyStrokeEx` で SUCCESS を返し続ける** → 32 回で止める。
4. **safe の時にだけ現れる config の誤り** → 許可の表（path を含まない）と normal mode の空測りで、全 loader が同じ file を同じ理由で受け入れ・拒否する。
5. **古い BUILD の cfg が作り直されない** → 名前に `-safe1` を足す。
6. **BIOS loader の asm の呼び出しと大きさ** → 既存の入口を wrapper で残す。p002 で 3 つの loader の build と `stage2_end` を確かめる。
7. **record の長さ**: safe mode は `kmsg=console login=console`（26 byte）を足すが `logo=logo.ppm login=graphical kmsg=quiet` を落とすので普通は短くなる。長くなる file は両 mode で `parameters-too-long`（H8）。
8. **告知が kernel の console の初期化で消える** → 1 秒の Stall（D5）と debug port。
9. **S2 で safe になった時 `logo_shown` で `quiet_console=1` のまま** → 告知の前に 0 に戻す。`quiet_boot` は再計算する（読むのは 1673・1686・1691 行だけ）。
10. **試験が console log に頼る** → PNG と SSH の sysctl で判定。WS013 の OVMF 試験は terminal の文字列で判定しているので流用せず、新しい script を書く。
11. **i915 が scanout を取って text console が見えない** → safe boot の範囲外。console の login では present が無く takeover に入らないと見る（推測）。実機の text image の結果で分かる（§5）。
12. **zedinst の admission が BIOS 用の cfg の safe の行を拒む** → p003 では native UEFI の cfg だけに足す。
13. **OVMF の `ConsoleInHandle` の Ex が usb-kbd と i8042 のどちらの event を流すか未確認** → 試験は `device: kbd0` で usb-kbd に送り、Q1 と Q2 の両方を流す。
