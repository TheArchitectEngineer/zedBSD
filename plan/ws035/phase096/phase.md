<!-- awesome-plan project=zedbsd record=ws035p096 -->

# ws035-p096: UEFI loader のロゴ（完全なグラフィカル起動の 1）

Phase ID: `ws035-p096`
Parent: [WS035](../ws.md)
Status: cleared（2026-09-28、サブエージェント）
Phase disposition: normal
Queue: なし（2026-09-28 main の割り当て。ユーザー「ブートローダにロゴを表示させます。…ブートローダはppmのようなシンプルな画像
ファイルを読みます。」）

## 実装（2026-09-28）

- `bootloader/uefi/logo.c`・`logo.h`（新）: zedbsd.cfg の `logo=PATH`（kernel= と同じ FAT の相対 path、安全な文字だけ、`..` 無し、
  63 文字まで）の binary PPM（P6、最大値 255、一辺 4096 以下、16 MiB 以下）を読み、画面を左上の画素の色で塗り、中央に描く
  （画面より大きければ中央を切り出す。RGBX・BGRX）。`zbl_uefi_parameter_present`（token が丸ごとあるか）。
- `bootx64.c`: video の選択の後に logo を描く（pool に読み、描いたら返す）。描けたら loader の進捗の文字を ConOut に出さない
  （debug port には出す。起動を止める失敗は `fail_status` で文字に戻して出す）。`kmsg=quiet` があれば進捗の白い block
  （stage 1・2）を描かず、transition の新しい入口 `zbl_transition_quiet`（stage 3 を描かない）で kernel に入る。
  logo が無い・読めない・PPM でないときは何もせず起動を続ける。
  最初は handoff の flags に `QUIET` の bit を足したが、HAL の handoff の検査（V7 は flags が required と等しいこと）で起動しなく
  なった（gdb で見る前に、quiet の有無の比較で分かった）。HAL を変えないよう transition の入口を分ける形にした。
- `tools/build/make-boot-logo.py`（新）: logo を図形から作る（外部の画像を入れない）: 青の gradient の角丸の四角と白い Z、
  独自の線の字形の「zed」（白）「BSD」（淡い青）、紺の背景。600x200（loader の 640x480 の mode に収まる）。同じ入力で同じ出力。
- `zedimage-host`（native layout）に `--logo FILE`（ESP の `/logo.ppm`）、`check-amd64-native-image.py` に `--logo`、
  `platform/amd64/vmunix.mk` が `$(BUILD)/boot-logo.ppm` を作り native image の ESP に置く（zedbsd.cfg が `logo=` を書くのは p098）。
- `bootloader/uefi/README.md` に `logo=`・`kmsg=quiet`。
- 試験の道具 `plan/ws035/tests/boot-shots.py`（新）: image を写し ESP の zedbsd.cfg に行を足し（mtools）、boot-test と同じ形
  （OVMF・NVMe・標準 VGA・8 GiB、KVM）で起動し、変わるたびに画面を撮る。`--pause-at ADDR` は gdb stub の hardware
  breakpoint で kernel の入口（0xffffffff80200000）で止めてその時の画面を撮る。

## 検証（amd64、QEMU、2026-09-28）

- `boot-shots.py IMAGE build/ws035-p096/pause --cfg logo=logo.ppm --cfg kmsg=quiet --pause-at 0xffffffff80200000`: kernel の
  入口で画面は logo だけ（文字も進捗の block も無い）: `p096-20260928-loader-logo.png`。その後 kernel の HAL の早期 console が
  画面を消して文字を出し（p097 の HAL の提案の範囲）、login まで起動する。
- `kmsg=quiet` 無しの `logo=` だけ: 起動して login。
- boot test（既定の zedbsd.cfg、logo= 無し）: PASS（`p096-20260928-boot-test-login.png`）。
- 規約: logo.c・logo.h の style-check 0、bootx64.c は増えていない（228 → 228）。build warning 0。
- 画面（`/home/awe/zedBSD-rpi4/build/ws035-shots/`）: `p096-20260928-logo-file.png`（logo の file）、`-loader-logo.png`、
  `-boot-test-login.png`。
- 実機: 未実施。BIOS（pcat）の loader の logo は範囲外（amd64 の既定は UEFI の native layout、残り）。

## 残り

- BIOS の loader（`bootloader/pcat/bootzbsd.S`、VBE）の logo。
- logo の高解像度版（loader は `video=` の mode で描く。既定は 640x480）。
