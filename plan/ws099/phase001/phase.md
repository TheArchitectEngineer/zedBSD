<!-- awesome-plan project=zedbsd record=ws099p001 -->

# ws099-p001: 基準 C1〜C10 を確かめる一括の試験と、今の状態の把握

Phase ID: `ws099-p001`
Parent: [WS099](../ws.md)
Status: in-progress（2026-09-30、サブエージェント、worktree `wt/ws035`（ws035-keiland））
Phase disposition: normal
Queue: なし（2026-09-30 main の割り当て「ws099-p001 を実行。基準 C1〜C10 を確かめる一括の試験の script（既存の zdesktop の試験を束ね、
足りない C2・C5・C7・C10 を足す）、今の main での実行と基準ごとの PASS・FAIL と数値の表、FAIL の基準ごとの直す Phase の案。直すのは p002 以降。
BUG-115（p072）は原因の切り分けまで」。基準はユーザーの確認待ちの案なので、閾値は script の先頭の変数）

## 範囲

- 試験と現状の把握だけ。compositor の見た目と動きは変えない（例外: C5 の計測のための診断の log、下の「診断の追加」）。
- IME の file（`ime.h`・`text-input.c`・`input-method.c`、seat.c の IME の hook）とブラウザの source には触れない。

## 作ったもの（`plan/ws099/tests/`）

| file | 内容 |
| --- | --- |
| `config-amd64-criteria.mk`・`build-criteria-image.sh` | 基準の image: graphical の login の image（`plan/ws035/tests/config-amd64-graphical.mk`）に Notes（C3 の swipe）と Settings、生成の壁紙 5 枚（`userland/desktop/wallpapers/generate.py` → `/usr/share/keiland/wallpapers/`、C7）。kei の autologin |
| `criteria.sh` | 一括: `criteria.sh [IMAGE] [OUTDIR] [C1 … C10]`。基準（と C9 の試験）ごとに guest を起こし直し（C2 と p128 は 1920x1280、他は 1280x800）、`OUTDIR/results.txt` に 1 行ずつ（PASS・FAIL・秒・RESULT の行）。閾値は先頭の変数（`C5_FIRST_FRAME_MS=100`・`C5_GAP_MS=150`・`C5_WINDOWS=10`・`C5_ROUNDS=3`・`C7_MIN_CONTRAST=4.5`・`C10_SECONDS=3600`・`C10_MAX_ERRORS=0`・`C1_CYCLES=2`・`C9_TESTS`）。C6 は実機の計測なので「NOT-RUN」と書く |
| `c2-geometry.sh`（C2） | kei の session（1920x1280）で App Home から Files: 四隅と四辺の resize（40 px、大きさと反対側の角・辺）、title bar の drag での移動、title bar の double click の最大化と system bar の題の double click の戻し（位置と configure の大きさ）、最小化の button と Wiseview の tile での戻し（戻った後の drag で元の位置を確かめる）。14 項目 |
| `c5-transitions.sh`・`c5-parse.py`（C5） | `--glass --log-frames` の compositor と popup-probe 10 個で、Wiseview（Super+Tab・Esc）と App Home（launcher・Esc）の開閉を 3 回。log の `at_ms` から、要求から最初の frame まで・開ききるまで・frame の最大の間隔 |
| `c7-contrast.sh`・`c7-contrast.py`（C7） | 既定と生成の 5 枚の壁紙ごとに compositor を起こし直し、system bar の時計と窓（popup-probe）の title の文字の contrast（WCAG の相対輝度: 領域の中央値のガラスと、最も暗い 1% の文字） |
| `c10-soak.sh`（C10） | `C10_SECONDS` の間、1 周: probe 3 つを開き、title bar の drag、Wiseview と App Home の開閉、最大化と戻し、close button と kill で閉じる。周ごとに compositor の生存、最後に `ZWL ERROR`・`ZWL FAILED` の数 |

既存の試験（`plan/ws035/tests/`）は変えていない（C1 = p126、C3 = p138、C4 = p137、C8 = p134、C9 = p052・p053・p072・p076・p126・p128・p134・p137・p138）。

### 診断の追加（compositor、C5 の計測のため）

App Home と Wiseview の開閉の log には時刻が無かったので、次の行の末尾に ` at_ms=N`（`zwl_milliseconds()`）を足した（見た目・動きは変えない。
既存の試験の pattern は行の頭の一致なので影響なし: 束ねた試験は全て PASS）:
`ZWL HOME open via=`・`ZWL HOME close via=`・`ZWL HOME opened`・`ZWL HOME closed`（`home.c`）、`ZWL WISEVIEW opening key`・`close key`・
`open windows=`・`closed`（`shell.c`）、`--log-frames` の `ZWL COMPOSE frame=`（`compose.c`）。規約: `style-check.py` 0。

## 結果（2026-09-30、QEMU の Venus、`build/ws099-criteria.img`（この worktree の main（5dfe65e0）の取り込み後、上の診断を含む）、実機は未実施）

`plan/ws099/tests/criteria.sh build/ws099-criteria.img build/ws099-criteria C1 … C9`（`results.txt`）。C2 は title bar の掴む点を直した後に単独で
（`build/ws099-criteria-c2/`）、C7 は socket の待ちに直した後に再度（`build/ws099-criteria-c10/`）、C10 は単独（同）。

| # | 結果 | 数値・内容 |
| --- | --- | --- |
| C1 | PASS（範囲は一部） | p126 の 2 周（1280x800）: login・Log Out の替わり目 4 回で 70〜76 枚ずつ撮り、黒 0 枚・文字の console 0 枚。**起動から greeter と Shut Down の替わり目は試験に無い**（未確認）。実機は未実施 |
| C2 | PASS | 14/14: 四隅（右下・左上・右上・左下）と四辺（右・左・下・上）の resize が各 40 px、反対側の角・辺は動かない。title bar の drag（+60,+40）が位置どおり。最大化で 1920 幅に dock、戻しで同じ位置（x,y）と大きさ（configure の width・height）。最小化と Wiseview の tile での戻しの後、位置が元のまま。1 回目は試験の誤り（Files の title bar の右寄りの検索欄を掴んだ）で 3 項目 FAIL、掴む点を中央に直して PASS |
| C3 | PASS | p138: Notes の swipe → Esc で作業の領域の中央（x=128 y=98）、title bar の drag で動き、2 回目の swipe → Esc で戻り先へ |
| C4 | PASS | p137: 閉じた窓（kill・close button）の次の窓が enter と key を受け、別の desktop の窓は受けない |
| C5 | **FAIL（QEMU）** | 窓 10 個、Wiseview と App Home の開閉 12 回の全て: 要求から最初の frame まで **102〜215 ms**（基準 100 ms。ほぼ全てが 101〜105 ms、App Home の最初の開きだけ 213〜215 ms）、開ききるまで 243〜390 ms、frame の間隔の最大 129〜215 ms（開閉の途中の frame は 2〜3 枚）。止まりはしない（全ての開閉が開ききり・閉じきった）。**QEMU の Venus の frame の間隔（約 130〜140 ms）が主**で、基準の 100 ms は実機の数（WS075）。実機は未計測 |
| C6 | 未実施 | 実機（5330）の計測（`plan/ws075/tests/hdmi/measure-apps.sh`、WS075）。QEMU では測らない |
| C7 | PASS（範囲は一部） | 6 枚 × 2（時計・窓の題）の 12 点が全て 4.5 以上。最小 **5.48**（Aurora の system bar の時計、ガラスの輝度 0.345）、次に 6.24（Twilight の時計）。窓の title は 9.07 以上。**client が描くガラスの上の文字（Settings の頁の説明、Files の文字）は測っていない**（旧 ws035-p135 の懸念はこちら） |
| C8 | PASS | p134: Terminal の本体 4/4 直角・title bar 丸い、dock・戻しも同じ、他の窓は両方丸い |
| C9 | PASS | p052・p053・p072・p076・p126・p128・p134・p137・p138 の 9 本が全て PASS（新しい guest で 1 本ずつ） |
| C10 | （実行中） | |

画面: `build/ws099-criteria/`（c1〜c9 の各 directory）、`build/ws099-criteria-c2/c2/`（opened・after-*・moved・maximized・restored・minimized・unminimized）、
`build/ws099-criteria-c10/c7/`（壁紙 6 枚）、`build/ws099-shots/c5/`（windows・wiseview・home）。

## BUG-115（p072）の切り分け

- 今の main の image（`build/ws099-criteria.img`）では再現しない: 新しい guest で PASS、同じ guest で p076 の直後でも PASS。
- ws094-p002 の条件（main の古い image `build/ws035-sq/hdd-image.img`、2026-09-29 05:04 の複写 `build/ws099-bug115-old.img`）では**再現した**
  （`WLSHM FAILED run=a setup errno=5`）。
- 原因: **試験の待ち**。p072 は compositor を起こして `sleep 4` の後に wlshm を起こす。古い image の compositor は `--glass` の起動（READY、
  socket の作成）に 6 秒より長くかかり（WS035 p129〜p133 の起動の短縮の前）、wlshm は socket の無いうちに `wl_display_connect` に失敗して
  EIO を返す。同じ古い image で compositor の 12 秒後に wlshm を起こすと、接続して描く（`WLSHM FRAME`、`ZWL CLIENT client=1`）。
- 今の main では起動が約 3 秒（p136 の image の `ZWL STARTUP step=compose ms=2702`）なので 4 秒の待ちに収まるが、余裕は約 1 秒。
- 直し方の案（試験の側、p00x）: p072 の `sleep 4` を socket（`/tmp/wayland-0`）か `ZWL READY` の行を待つ形にする。同じ固定の待ちの試験
  （`zdesktop-p0xx.sh` の多く）にも同じ危険がある。WS099 の新しい試験（c5・c7・c10）は socket を待つ形にした。

## 未実施・制限

- 実機（5330）の確認（C1・C5・C6 の実機の部分）。
- C1 の起動から greeter と Shut Down の替わり目。C7 の client の描くガラスの上の文字。
- C9 の試験の一覧は、基準に直接関わるもの 9 本に絞った（`plan/ws035/tests/zdesktop-p*.sh` の全て（約 70 本）ではない。古い試験には別の image
  （files の image、`build/ws071-run`）や前提が要るものがある）。

## Resume point

2026-09-30: C1〜C9 の実行と BUG-115 の切り分けまで済み。C10（1 時間、`build/ws099-criteria-c10/`）の結果を待って表に書き、ws.md に直す Phase の案を並べる。
