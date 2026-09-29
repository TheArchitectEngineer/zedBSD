<!-- awesome-plan project=zedbsd record=ws089-p009 -->

# ws089-p009: 生成の壁紙をデモの image に同梱

Status: cleared（2026-09-29、QEMU の Venus の guest。実機は未実施）
Disposition: normal
Parent: [WS089](../ws.md)
Queue: なし（2026-09-29 main の依頼。サブエージェントが worktree の branch `wt/ws089` で実行）

## 目的と受け入れ

- ユーザーの判断（2026-09-29 夜、D8 の質問への回答、main 経由）: 「生成の壁紙を入れる」。
- main の依頼: デモに見せられる質の壁紙を 3〜5 枚、script で生成してデモの image に同梱する。グラデーション・ぼかした形など Kei の
  見た目に合うもの、既定の壁紙の大きさ（1080 の高さ）、UI に出る英語の短い名前。script は試験ではなく製品の一部（source の側）、
  生成物（PPM）は git に入れず build の時に作る。license は zlib。
- main の許可: `plan/ws075/demo/build-demo-image.sh` と `config-demo-hdmi.mk` に生成と同梱の最小の変更。
- 確認: 並べた画面を目で見る、Settings の Wallpaper の頁から選べることを QEMU で、`build-demo-image.sh` の変更を `make -n` 相当か build で。

## 実装

- 新しい `userland/desktop/wallpapers/generate.py`（Python の標準の library だけ、zlib の header）: 5 枚（Aurora・Dawn・Lagoon・Meadow・
  Twilight、1920x1080、P6 の PPM）。縦のグラデーション、広い光、ぼかした楕円の形を 4 分の 1 の大きさで描き、双線形で拡大し、
  固定の seed の微かな dither で段を消す（同じ command は同じ byte を書く）。`--preview=PNG` で並べた sheet。約 16 秒で 5 枚。
- `plan/ws075/demo/build-demo-image.sh`（main の許可、+7 行）: `$build/wallpapers` に生成し、各 PPM を
  `/usr/share/keiland/wallpapers/NAME.ppm` として `ZEDBSD_TEST_EXTRA_FILES` に足す。header の説明に一文。
  `config-demo-hdmi.mk` は変更なし（`settings` は既に入っていて、壁紙は image の file として足す）。
- `plan/ws089/tests/build-settings-image.sh`: 試験の image も同じ generate.py の壁紙を入れる（p004 の試験用の `make-wallpapers.py` は消した）。
- `userland/desktop/settings/look.c`: 縮小画像は tile の比（16:10）で真ん中を切り取る（16:9 の壁紙を押し潰さない）。
- 試験: `settings-p009.sh`（guest）。`settings-p004.sh` は壁紙の数と名前を新しいものに合わせた（6 枚、Aurora）。

## 確認（2026-09-29）

- 生成: `python3 userland/desktop/wallpapers/generate.py build/ws089-wallpapers-gen --preview=build/ws089-shots/p009/wallpapers.png`
  → 5 枚、16 秒。並べた sheet を目で見て、Aurora（平らだった）と Meadow（白い丸が汚れに見えた）の形を一度直した。
- build（worktree の `build/amd64`、-Werror）: `build-settings-image.sh` → exit 0、desktop の warning 0。規約: settings の全 file → 0。
  `git diff --check` → 0。
- **QEMU（Venus の guest、起動の直後に開始）**: `plan/ws089/tests/settings-p009.sh` → **PASS**。image の
  `/usr/share/keiland/wallpapers/` に 5 枚（各 6,220,817 byte）、Wallpaper の頁に 6 枚（既定と 5 枚）、5 枚を順に選ぶと Settings が key を書き、
  zdesktop が描き直した（`ZWL GLASS wallpaper ... ms=98〜151`）、既定に戻すと session の壁紙。zdesktop の log に ERROR なし。
  画面 `build/ws089-shots/p009/`（`grid.png` は頁と 5 枚の desktop、目で確かめた）。
- デモの script: `sh plan/ws075/demo/build-demo-image.sh build/ws089-demo-dryrun -n` → exit 0、`build/ws089-demo-dryrun/wallpapers/` に 5 枚が
  生成され、make の命令の `ZEDBSD_TEST_EXTRA_FILES` に `/usr/share/keiland/wallpapers/{Aurora,Dawn,Lagoon,Meadow,Twilight}.ppm=...` が渡った。
  dry run の directory は消した。実際のデモの image の build は未実施（i915 の実機の image は main が作る）。
- 起動の確認: `OUTPUT=build/ws089-boot-test plan/tools/boot-test.sh build/amd64/hdd-image.img` → PASS。
- 未実施: 実機（1920x1080 の HDMI での見た目、i915 での壁紙の差し替えの時間）、デモの image の実際の build。

## 残り

- 壁紙の枚数は 5（Wallpaper の頁の上限は既定を含めて 8、`SE_WALLPAPERS`）。
- 暗い壁紙（Aurora・Twilight）の上では、頁の説明の文字の contrast が下がる（読める）。glass の tint は zdesktop の範囲。
