<!-- awesome-plan project=zedbsd record=ws216-p007 -->

# ws216-p007: T1 の QEMU（AAT の draw-tools・fill-region）と実機 5330 のペン（ユーザー）

Parent: [WS216](../ws.md)
Status: in-progress（2026-10-13 P1: q946 で準備（scenario 2 つ、helper、試験の image の config）済み。test-wait: T1 の QEMU の試験（§4 の依頼、T1 の番号は Q1 が付ける）と 5330 のユーザーの確かめ）
Phase disposition: normal
Queue: q946（2026-10-13 Q1 の依頼、P1）。承認はユーザーの再開の指示（2026-10-13「N=2で、P1,P3を起動し、T1でテストする流れで、実行を再開してください。」）と ws.md の D1〜D12・ユーザーの決定

## 範囲と入力

- 設計は [p001 §8](../phase001/phase.md#8-段の分け方と受け入れ試験) の p007 の行、試験の案は [p006 §9](../phase006/phase.md)。
- 依存: p002〜p006 の出力（main に入っている。p004〜p006 は test-wait で、その画の確かめがこの Phase）。
- q946（準備）の範囲: AAT の scenario 2 つ（`tests/scenarios/apps/notes/draw-tools.md`・`fill-region.md`）、その補助（`plan/tools/aat/scenarios/helpers_notes_edit.py` の `apps.notes.draw-tools`・`apps.notes.fill-region`）、試験の image の config（`plan/ws216/tests/config.mk`）、T1 への依頼。QEMU は担当が起動しない（T1）。
- 触らない: Notes・libpdf の source、`plan/tools/aat/` の共有の道具（`aat`・runner・`config-amd64-aat.mk`）、toolchain・HAL。

## 1. 試験の image

- `plan/ws216/tests/config.mk` = AAT の image（`plan/tools/aat/config-amd64-aat.mk`）＋ `peninject`（試験のペン）。AGENTS.md の「試験の image の作り方」どおり config.mk の build だけで、`--file` の複写は無い。
- PDF（`edit-basic.pdf`）は image に入れない。補助が host で `plan/ws175/tests/make-edit-samples.py` で作り、run の時に scenario の folder に置く（既存の `apps.notes.pdf-*` と同じ）。
- build: `AAT_CONFIG=plan/ws216/tests/config.mk plan/tools/aat/build-image.sh BUILD`（T1 の worktree の `build/` の中、image の build は `flock /tmp/zedbsd-image-build.lock`）。

## 2. scenario と補助

| id | 中身 | 機械の判定 | 人（T1）が見る撮影 |
| --- | --- | --- | --- |
| `apps.notes.draw-tools` | key B・L・W、`peninject` の筆圧 0→4095→0 の線（筆・鉛筆・水彩の青と赤の交差）、描く道具の button（48）で帯、帯の Marker（2）、mouse の Marker の線、undo・redo、保存と開き直し、host の `qpdf`・`pdftoppm` | `NOTES TOOL 49/50/51`、`NOTES STROKE … tool=2/3/4 … tilt=1`（筆圧の最大 > 30000）、`NOTES PIPE made name=grain`・`name=multiply`、`NOTES PIPE fallback` が無い（開き直しの後も）、`NOTES DRAWS open`・帯の `NOTES BUTTONS`・`NOTES TOOL 2 name=highlighter`・`NOTES DRAWS closed`、`NOTES UNDO/REDO`、`NOTES SAVE`・`NOTES OPEN … kind=notes` の線の数、`qpdf --check`、展開した PDF の `/Multiply` と `/PatternType 1` | `brush-pencil`・`watercolor`・`strip`・`all-tools`・`reopened`・`poppler` |
| `apps.notes.fill-region` | ペンの閉じた円、key F で中を click、同じ所の 2 度目、drag、消しゴム（線ごと）と undo、`edit-basic.pdf` の 1 頁の画像の白い正方形の click、保存、host の `qpdf`・`pdftoppm` | `NOTES FILL made place=0`・`bands=… background_us=0`、`NOTES FILL refused reason=same`、`NOTES FILL moved`、`NOTES ERASE page=0 removed=1 strokes=1`、`NOTES UNDO page=0 strokes=2`、PDF で `background_us>0` の `NOTES FILL made`、`NOTES SAVE … pages=3 strokes=1`、`qpdf --check` | `filled`・`already-filled`・`tap-to-fill`・`erased`・`undone`・`pdf-filled`・`poppler` |

- 試験のペン: `aat-input` が `/dev/input-inject` の口（`INPUT_INJECT_OPENS_MAX` 4）を全部持つので、補助は線ごとに `aat stop` → `peninject`（root）→ `aat start --size` を流す。`peninject` の無い image では mouse の線（筆圧なし）で最後まで流し、fail（理由: 試験のペンが無い）を返す（`full.suite` の `apps.*` が普通の AAT の image で流した時に分かるように）。
- 補助の変更: `Notes(…, opened=)`（新しい notebook は `NOTES START` を待つ）、`Notes.area()`（頁の見えている所、toolbar と帯の下）、`pen_script()`・`pen_stroke()`・`has_pen()`・`row()`・`pdf_parts()`・`page_picture()`、action と道具の定数、`PICTURE_SQUARE`（edit-basic の画像の白い正方形の中心、PDF の (192, 495)）。
- 色と道具は Notes の設定に残るので、どちらの scenario も最後に黒と Pen に戻す。

## 3. 受け入れ（p001 §8 の p007 の行）

| # | 条件 | 確かめ | 結果 |
| --- | --- | --- | --- |
| A1 | QEMU: 筆・鉛筆・水彩を `peninject` の筆圧 0→最大で描く、帯の選択、undo/redo、保存 → 開き直しで同じ画面、grain・multiply の pipeline、代わりなし | T1 `apps.notes.draw-tools` | 未実施（T1） |
| A2 | QEMU: 塗り（閉じた円、同じ所、drag、消しゴム・undo、PDF の囲み）、帯ごとの背景、時間の数（`ms=`・`background_us`） | T1 `apps.notes.fill-region` | 未実施（T1） |
| A3 | PDF の中の見た目が他の viewer（Poppler）で近い | 両 scenario の `poppler` の PNG（T1 が画面の PNG と並べて見る） | 未実施（T1） |
| A4 | 1.1・2.0 の file の読み | host の `plan/ws216/tests/run-notes-tools.sh`（p003 で ok）。QEMU の既存の `apps.notes.pdf-*` が同じ image で流れれば、それも記録 | host ok（p003）。QEMU は T1 の任意 |
| A5 | 5330: 筆の入り抜き・鉛筆の濃さ・水彩の重ね・塗りの見た目、指の塗り（Finger on の 8 px）、`NOTES FRAME vertices=` の最大、`v_ref` などの定数の調え | ユーザー（Q1 が依頼） | 未実施（ユーザー） |

## 4. T1 への依頼（Q1 へ送った文面）

> **T1 依頼（ws216-p007、Notes の描く道具と塗りつぶし）**
> - tree: main に q946 の merge が入った SHA（helper と scenario が要る）。
> - image: `AAT_CONFIG=plan/ws216/tests/config.mk plan/tools/aat/build-image.sh build/<T1 の dir>`（AAT の image ＋ `peninject`）。`--file` の複写は無い。使い回しの image なら notes・libpdf・peninject が依頼の tree で作り直されたことを確かめる。
> - 流す: `plan/tools/aat/run-aat.sh qemu OUTDIR apps.notes.draw-tools apps.notes.fill-region`（任意で同じ起動に `apps.notes.draw-stroke apps.notes.pdf-edit-multipage` を足すと p006 の helper の追従も見られる）。
> - 合否: 両 scenario の verdict が `needs-person`（機械の判定は全部通った）で、撮影を見て: 筆は入りと抜きが細く中が太い、鉛筆に粒があり区間の継ぎ目に線が無い、水彩は半透明で交差が濃く青と赤が減法で混ざる、帯が toolbar の下に 6 つ並ぶ、開き直しが前と同じ、塗りが円の線まで届き白い隙間が無く線が上、「Already filled」と「Tap to fill」が見える、消しゴムで塗りだけ消え undo で戻る、PDF の白い正方形だけが塗られる、Poppler の頁が画面に近い。`fail` は理由の行と `OUTDIR/logs/ID.log`・`records/ID.md` を返す。
> - 撮る PNG: 補助が撮る（draw-tools: `brush-pencil`・`watercolor`・`strip`・`all-tools`・`reopened`・`poppler`、fill-region: `filled`・`already-filled`・`tap-to-fill`・`erased`・`undone`・`pdf-filled`・`poppler`）。
> - 時間の記録: `records/apps.notes.fill-region.md` の `NOTES FILL made … ms=` と `NOTES FILL bands=… background_us=`（新しい頁と PDF の 2 回）、各 scenario の秒（`verdicts.tsv`）、`NOTES GRAIN hash=`。
> - 実機 5330 のペン（A5）はユーザーの目で別に（Q1 が依頼）。

## 5. 実行の記録（2026-10-13 P1、agent/p1、main bac387218 を merge）

### 5.1 変えた所

| file | 内容 |
| --- | --- |
| `tests/scenarios/apps/notes/draw-tools.md`（新） | §2 |
| `tests/scenarios/apps/notes/fill-region.md`（新） | §2 |
| `plan/tools/aat/scenarios/helpers_notes_edit.py` | §2 の補助 |
| `plan/ws216/tests/config.mk`（新） | §1 |

### 5.2 command と結果（host）

- `python3 plan/tools/aat/check-scenarios.py` → `check-scenarios: PASS`（155 scenarios）。`--list` に 2 つが `active either look`。
- `python3 plan/tools/aat/scenarios/helpers_notes_edit.py --list` → 2 つの id が出る。`python3 -m py_compile` ok。
- 補助の関数の host の確かめ（scratchpad の小さな script、`--list` の形で module を読み込み）: `pen_script()` が `size 32767 32767`・`wait 3000`・`tool pen`・hover・`down … 0 20 -15`・筆圧の最大 4095 の 88 行、`PICTURE_SQUARE` = (192.0, 495.0)。
- `sh plan/tools/aat/tests/run-host.sh` → `aat-host: PASS`。
- config の確かめ: `make -f <Makefile を include して ZEDBSD_USER_PROGRAMS を出す小さな makefile> ZEDBSD_CONFIG=plan/ws216/tests/config.mk` で `aat-input`・`keiland-shot`・`notes`・`peninject` が入る。

### 5.3 未実施・制限

- QEMU で補助を流していない（T1、§4）。座標（頁の見えている所、帯の Marker、PDF の正方形）、`aat stop`/`start` を挟んだペン、塗りの待ち 120 秒は QEMU で初めて確かめる。
- `peninject` の無い普通の AAT の image（`full.suite` の `apps.*`）では `apps.notes.draw-tools` は fail（理由つき）になる。普通の AAT の image に `peninject` を入れるかは Q1 の判断（`plan/tools/aat/config-amd64-aat.mk` は担当の範囲の外）。
- 5330 のユーザーの確かめ（A5）は未実施。

## 6. 残りと再開の条件

- T1 の結果を Q1 が判定する。PASS（needs-person で撮影が §4 の合否どおり）なら A1〜A3 が埋まる。FAIL は補助か Notes の直しを同じ Phase の新しい attempt に。
- A5（5330）のユーザーの結果。A1〜A5 が揃ったら cleared、p008（規約の全文の見直し）へ。
