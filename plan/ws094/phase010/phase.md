<!-- awesome-plan project=zedbsd record=ws094-p010 -->

# ws094-p010: L4a 長い名前と画面の大きさの変更

Status: cleared（2026-09-30 統合の試験で合格、下の節）。前の記録: uncleared（2026-09-30、サブエージェント P6、worktree `ws035-keiland`（branch `wt/ws035`）。
実装、host の試験、QEMU の guest の試験（`desktop-p010.sh`）は済んで PASS。回帰の C9・boot test と、`files-desktop-guest.sh` の既存の手順は未実施。
止めた理由: ユーザーの指示で優先を下げた（週間の使用量の上限が近いためのラップアップ）。
再開の条件: ユーザーが再開を言うとき。残るのは回帰の実行だけ。source は途中の状態ではないので戻していない（wip.patch は無い））
Disposition: normal
Parent: [WS094](../ws.md)
Queue: なし（2026-09-30 Q1 の割り当て）
依存: p006

## 範囲と受け入れ（ws.md の段 L4 の (a)(b)）

- (a) 40 文字の名前を 2 行に収め、中を省く（拡張子を残す）。選んだ時に全文を出すかを決める。
- (b) 画面の大きさを 1280x800 から 1920x1080 に変えても保存の場所を保ち、外れた項目は空いた cell に入れる（log）。
- 100 項目の溢れの扱いは変えない（ユーザーの判断「今のまま」、変えていない）。
- Files の desktop の source の変更は名前と配置の所だけ（P7 が後で libkeiui へ移す）。compositor は触らない（触っていない）。

## 決めたこと

- **選んだ時も全文は出さない。** 2 行の省いた形のまま、青い pill で 2 行を包む。
  - 理由: cell（96x104）の外へ描くと、隣の項目と重なる。また ws094-p009 の部分の描き直し（cell 1 つだけを描き直す）と合わない。
  - 全文は、F2 の名前の変更の欄（2 cell 幅まで）と Files の窓で見られる。
- **2 行の作り方**（`fm_desktop_label`）:
  1. 名前が 1 行に収まれば 1 行。
  2. 収まらなければ空白で分ける。空白は 1 行目の直後か、1 行目の後半にあれば使い、どちらの行にも描かない。残りが 2 行目に収まればそこまで。
  3. それでも収まらなければ、1 行目を一杯にして分ける。残りが 2 行目に収まればそこまで。
  4. それも無理なら、1 行目に先頭、2 行目に「…」と末尾を入れる。末尾はなるべく長く取るので、拡張子は必ず残る。
  - 例: 「a_forty_chara」/「…or_tests.txt」、「長い名前のフ」/「…録と末尾.txt」。
  - 2 行の時は、1 行目の baseline を 4 px 上げ（icon + 20）、行の間を 15 px にして、2 行とも cell に収める。

## 変更（`userland/desktop/files/`）

- `desktop-layout.c`: `fm_desktop_label`（上）と、UTF-8 の文字の境で進む・戻る補助を足した。宣言は `files.h`（`FM_DESKTOP_LABEL_MAX`）。
- `ui-desktop.c`:
  - 名前の描画を `desktop_name`（行の組み立てと pill）と `desktop_name_line`（1 行、pill か halo）に分けた。
  - 大きさが変わった時の log `DESKTOP grid width= height= columns= rows=` を足した。
  - `desktop_log_moved` で、知っている場所（保存、または前に出していた場所）と違う所へ置いた項目を log に出す: `DESKTOP moved name= from=c,r to=c,r saved=0|1`。
- 置き場所の規則は元のまま。保存の場所が grid の中で空いていれば保ち、外れた項目は右上から空いた cell に入る。layout file は大きさでは書き換えない。
  - 1920x1046（19 x 9）に保存した場所は、1280x766（13 x 7）では空いた cell に入り、1920 に戻ると元の場所に戻る。

## 試験

- `plan/ws094/tests/host-desktop.c` に 2 つの試験を足した。`host-desktop.sh` は **PASS**。
  - `check_label`: 1 行、空白で 2 行、一杯で 2 行、40 文字（英語と日本語）で「…」と拡張子、全ての行が 88 px に収まる。
  - `check_resize`: 1920 で保存の場所、1280 で空いた cell、1920 に戻して保存の場所。
- 新規 `plan/ws094/tests/desktop-p010.sh IMAGE OUTDIR`（QEMU の Venus、`plan/ws102/tests/build-inset-image.sh` の image）:
  1. 1920x1080 で far.txt が保存の 16,8 にある。長い名前が 2 行で出る（names-1920.png）。40 文字の項目を選べる（select の log、selected.png）。
  2. guest を 1280x800 で起こし直す。`DESKTOP moved name=far.txt from=16,8 ... saved=1` が出て、far.txt は空いた cell に入る（names-1280.png）。layout file は変わらない。
  3. その layout file のまま 1920x1080 に戻すと、far.txt は 16,8 に戻る（back-1920.png）。
  - 結果: **PASS**（`build/ws094-p010-shots/run3/`）。1・2 回目の失敗は試験の側の誤り（qmp-pointer の引数の順、click の y に system bar の 34 px を足し忘れた）で、直した。

## 結果

| 確認 | 結果 |
| --- | --- |
| build（`build/ws099/w94.img`、Files と compositor を含む） | exit 0、`userland/desktop` の warning 0 |
| `host-desktop.sh`・`plan/tools/files/host-model.sh` | **PASS**・**PASS** |
| `desktop-p010.sh` | **PASS** |
| C9、boot test、`files-desktop-guest.sh` の既存の手順（show・saved・drag） | **未実施**（ラップアップで止めた） |

画面: `build/ws094-p010-shots/run3/names-1920.png`・`selected.png`・`names-1280.png`・`back-1920.png`。

## 再開するとき

- 回帰を流す: `files-desktop-guest.sh` の install・show・saved・drag（BIN は build の dir）、`criteria.sh C9`、`boot-test.sh`。どれも PASS なら cleared にする。

## 統合の試験（2026-09-30 夕、Q1）

ユーザーの指示「P7の試験については、いまからすべての成果を統合して、1本の試験で動いたら、合格にしましょう。それはメインエージェントQ1でやります」により、
P4（ws090-p014）・P7（ws090-p008・p011）・P3（ws102-p024）・P6（ws094-p010）を main に統合した tree（722de611）で `plan/ws079/tests/demo-s8-s9.sh` を 1 本流し、
**PASS**（QEMU の Venus、build/amd64 で image を作り直した）。S8: Notes の全画面・pen の線・窓に戻る、S9: PDF Viewer の scroll の頁送り最長 140 ms、
page の frame 最長 148 ms、double tap と pinch、`ZWL ERROR` 0。画面と log は main の checkout の `build/integ-0930/`。この試験は Terminal を開かない
（Terminal の画面と p088 の切り分けは未実施）。実機は未実施。
