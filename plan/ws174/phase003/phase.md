<!-- awesome-plan project=zedbsd record=ws174-p003 -->
# ws174-p003: UEFI loader の key の検出と統合・docs・QEMU の試験

Parent: [WS174](../ws.md)
Status: in-progress（2026-10-06 P1: 実装・build・host 試験まで。T1 の QEMU の 5 cell の結果を Q1 が判定するまで cleared にしない）
Disposition: normal
Queue: Q1 の dispatch（2026-10-05 夜、P1 へ p002・p003・p005）。承認: ユーザー「OKです。ブートローダの仕様変更を実装してください。BIOSは後日でよいです。」
依存: [ws174-p002](../phase002/phase.md)（`zbl_boot_override_apply()`）

## 範囲

設計 [design.md](../phase001/design.md)（第 3 版）§3・§4.1・§7.2〜7.4・§9.1 の K1・K2・§9.2・§10 の p003。Q1 の技術の決定で **S2 は外した**（標本は S0 と S1 の 2 点）。Q1 の割り当てで `boot-keys.c/.h` と K1・K2 はこの Phase。

- `bootloader/uefi/include/uefi.h`: `EFI_NOT_READY`、`EFI_INPUT_KEY`・`EFI_KEY_STATE`・`EFI_KEY_DATA`、修飾 key と toggle の bit、`EFI_SIMPLE_TEXT_INPUT_EX_PROTOCOL`、その GUID（UEFI 仕様の事実だけを自分の書き方で）。`EFI_SYSTEM_TABLE`・`EFI_BOOT_SERVICES` の slot は不変。
- `bootloader/uefi/boot-keys.h`・`boot-keys.c`（新）: `struct zbl_uefi_boot_keys`、`zbl_uefi_boot_keys_open()`（ConsoleInHandle → LocateProtocol、`SetState(VALID|EXPOSED)`、失敗は boot を止めない）、`zbl_uefi_boot_keys_sample()`（最大 32 回、累積）、`zbl_uefi_boot_keys_from_state()`（VALID の時だけ、左右の Ctrl → KMSG、左右の Shift → LOGIN）。file scope の static なし。
- `bootloader/uefi/bootx64.c`: `struct loader_context` に `keys`。S0 = `A64 UEFI ENTRY` の直後に `open` と `sample`。S1 = config の parse の直後に `sample` → `apply_boot_keys()`（key 無しなら呼ばない＝record は byte 単位で不変、-1 なら `fail_discovered("Override parameters")`）→ `zbl_logo_path()` → video の希望（if / else if / else: Ctrl 640x480、logo 1920x1080、他 0）→ `framebuffer_from_gop()` → `notice_boot_keys()`（`Boot: kernel messages (Ctrl)`・`Boot: console login (Shift)`・`A64 PARAMS OVERRIDE <record>`、SetMode の後）→ `show_logo()` → `quiet_boot`（書き換え後の record から）。`TEXT_MODE_WIDTH/HEIGHT`。
- `platform/amd64/vmunix.mk`: `$(BUILD)/uefi/boot-keys.o` の rule、`bootx64.o` の依存に `boot-keys.h`・`boot-override.h`、`BOOTX64.EFI` の link に `boot-keys.o`。
- docs: `docs/reference/kernel-boot-parameters.md`（§7c を新設、§7a・7b に 1 項ずつ、§9 の「行ごとに 1 token」「LoadOptions」「4 経路で同じ意味」に但し書き）、`bootloader/uefi/README.md`、`docs/howto/boot-and-storage.md`（Failure diagnosis）、`bootloader/README.md`（BIOS loader は key を読まない）。`plan/`・Bug への link は無い。
- 試験: `plan/ws174/tests/boot-keys-host-test.c`・`run-boot-keys-host-test.sh`（K1・K2）、`run-boot-keys-qemu.py`・`run-boot-keys-qemu.sh`（T1 用の 5 cell）。

## 検証（2026-10-06、P1 の worktree）

| 確認 | コマンド | 結果 |
| --- | --- | --- |
| host 試験 K1・K2 | `timeout 120 plan/ws174/tests/run-boot-keys-host-test.sh`（cc = gcc）と `CC=clang ... build/ws174-host-clang` | `30 checks, 0 failures` ×4（gcc・clang、通常・ASan/UBSan。mock は ms_abi の callback） |
| host 試験 O1〜O11（p002 の回帰） | `run-boot-override-host-test.sh`・`run-boot-override-kernel-host-test.sh` | 43/0 ×2、72/0 |
| UEFI loader | `make -j16 ZEDBSD_CONFIG=plan/ws174/tests/config-amd64-keys.mk BUILD=build/ws174-keys build/ws174-keys/uefi/BOOTX64.EFI` | rc 0、warning 0（`-Werror`）、未解決の symbol なし、`BOOTX64.EFI check: PASS` |
| amd64 BIOS loader（共有の parser・logo-path は不変） | 同じ BUILD で `build/ws174-keys/bootloader/BOOTZBSD.EXE` | rc 0、warning 0 |
| amd64 image | `flock /tmp/zedbsd-image-build.lock timeout 5400 plan/tools/guest/test-image.sh plan/ws174/tests/config-amd64-keys.mk build/ws174-keys` | rc 0、`check-amd64-native-image: ... OK`。log の warning は openssh・openssl の package と noct の既存の物で、bootloader・kernel の file には 0。cfg は `kernel=vmunix rootpart=… swap0=… logo=logo.ppm login=graphical kmsg=quiet`（cfg の生成は不変） |
| QEMU script の判定の部品 | `run-boot-keys-qemu.py` の `classify()` を合成の PPM（640x480 の text、1920x1080 の中央 80x30 の text、市松、黒）と実際の `boot-logo.ppm` を 1920x1080 に置いた画面で | text・text・logo・other・logo。ブロック文字（塊の明暗）は text に数えない |
| `git diff --check` | | 0 |

- **QEMU（T1）: 未実施**（Q1 経由で依頼する。下の「T1 への依頼」）。実装の担当は QEMU を起動していない。
- **実機: 未実施**（WS の受け入れ、ユーザーと Q1）。

## 確かめた事実と限界

- kernel の keyboard driver は LED を自分で設定しない: `src/drivers/platform/pcat/ps2-8042.c` は LED の command（0xED）を送らない。`src/drivers/usb/usb-hid.c` が出す output report は raw の interface（FIDO、hidraw）への要求だけ。従って `SetState(EXPOSED)` が消した NumLock 等の LED は kernel が戻さない（docs §7c に記載）。QEMU では観測できない。
- S2 は外した（Q1 の決定）。S1 の後（config の parse の後、kernel の読み込みの間）に打たれた key は使われない。
- 告知（`Boot: …`・`A64 PARAMS OVERRIDE`）は画面では best effort（GOP の直接の SetMode の後の ConOut、logo、kernel の console）。debug port には常に出る。判定には使わない。
- `kern.boot.kmsg` の sysctl は無い。Ctrl の判定は screendump の kernel の text と、SSH で読む `dmesg` の `boot: parameters:` の行（kernel の log buffer。console log ではない）で行う。
- `plan/ws013/tests/run-uefi-zedbsd-config-ovmf.sh` は `bootx64.c` を自分で link するが、既に `video.o`・`logo.o` を欠いた古い試験で、この Phase の前から link できない（触っていない）。

## T1 への依頼（Q1 経由）

- image: commit（下の SHA）の worktree で `plan/tools/guest/test-image.sh plan/ws174/tests/config-amd64-keys.mk build/ws174-keys`（graphical boot、`kmsg=quiet`）。
- 実行: `timeout 1800 plan/ws174/tests/run-boot-keys-qemu.sh build/ws174-keys/hdd-image.img <OUTDIR>`。cell ごとに image の複写と OVMF の変数の複写から QEMU を起動し直す（同時に 1 つ、`-no-reboot`、NVMe の boot disk、usb-net port 2、usb-kbd port 3、QMP）。key は QMP `send-key`（0.1 s ごと、hold 50 ms、修飾 key も毎回押し直す）。
- 5 cell と判定（screendump の PNG と SSH だけ。console・serial の log は読まない）:

| cell | 操作 | (a) 画面 | (b) | (c) SSH |
| --- | --- | --- | --- | --- |
| C0 none | 無し | loader の後に logo | login prompt | `sysctl -n kern.boot.login` = `graphical`、dmesg `boot: parameters:` に `kmsg=quiet` と `logo=` |
| C1 Ctrl | `ctrl`+`spc` | loader の後に logo 無しで kernel の text | login prompt | `graphical`、`kmsg=console`、`logo=` 無し |
| C2 Shift | `shift`+`spc` | logo | login prompt | **`console`**、`kmsg=quiet`、`logo=` |
| C3 Ctrl+Shift | `ctrl`+`shift`+`spc` | logo 無しで kernel の text | login prompt | **`console`**、`kmsg=console`、`logo=` 無し |
| C4 late | loader が mode を変えて 5 s 後から `ctrl`+`spc`・`shift`+`spc` を 2 s | logo | login prompt | `graphical`、`kmsg=quiet`、`logo=`（loader の後の key は効かない） |

- 記録（判定ではない）: C1・C3 の loader の mode（640x480 が通ったか、`loader_modes`）、C1 の最初の frame に告知 `Boot: kernel messages (Ctrl)` が写るか（PNG を見る）。
- C1〜C3 の全部で key が検出されなければ `--no-usb-kbd --cells C1` で 1 回だけ再試行し（key は PS/2 へ）、分けて記録する。余裕があれば `--ctrl-alone`（R0: Space なしの `ctrl` だけ）を参考に流す（判定に入れない）。
- 結果: `<OUTDIR>/summary.json`、cell ごとの `result.json`・`frames/*.png`・`login.png`。PASS/FAIL と PNG を Q1 へ。C1〜C3 のどれかが FAIL なら p003 は uncleared。
