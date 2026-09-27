<!-- awesome-plan project=zedbsd record=ws035p092 -->

# ws035-p092: 新しい窓の置き方、暗い窓の上の非 active なタイトルバー、app_id からの mark の文字

Phase ID: `ws035-p092`
Parent: [WS035](../ws.md)
Status: cleared（2026-09-28、サブエージェント）
Phase disposition: normal
Queue: なし（2026-09-28 main の割り当て「(a) cascade/smart placement of new toplevels within the work area (offset each new
window, avoid exact overlap, keep fully visible), (b) make inactive glass titlebars over dark content legible ... with
before/after screenshots」、および「fold "titlebar icon letter from app_id (fallback to title)" into p092, and make sure
Files, Terminal, Browser and the X clients set sensible app_ids」。fg010）

## 範囲と前の状態

p090 の demo の画面（`p092-...-before-all.png`）: 新しい窓は作業域の中央に置かれ（大きさが近いと 32 px の cascade だけ）、
前の窓のタイトルバーをほぼ隠した。非 active なタイトルバー（白 0.38 のすりガラス、灰色の文字）は、黒い窓（zterm・Gears）の
上で灰色になり題が読みにくかった。タイトルバーの mark の文字は題の最初の文字で、題が変わると変わった（p091）。

## 実装（2026-09-28）

1. **置き方**（zdesktop `shell.c` の `zwl_glass_place`）: 試す場所を順に、作業域の中央、top の窓（最後に map・raise
   された窓）から右下へ 48 px ずつ 8 段、作業域の左上から 48 px ずつ 8 段。どれも作業域の中に収める（`glass_fit`、
   入らない大きな窓は左上から右下へはみ出す。従来どおり）。場所ごとに、他の窓の題の最初の文字（mark の右、
   タイトルバーの中央より少し下の点）を覆うと 1、他の窓の角が 32 px 以内なら 4 を数え（`glass_crowd`）、0 の最初の場所、
   無ければ最小の場所を取る。見る窓は今の desktop の map された窓（最小化・dock・fullscreen を除く、`glass_placed`）。
   plain（glass でない）の置き方は変えない。
2. **非 active なタイトルバー**（`draw_title_bar`）: すりガラスの白を 0.38 → 0.54（active は 0.55 → 0.64、差を保つ）、
   非 active の文字を (0.40, 0.46, 0.56) → (0.30, 0.35, 0.44)。
3. **mark の文字を app_id から**: zdesktop は `xdg_toplevel.set_app_id` を `app_id` として保ち（`protocol.c`・`zwl.h`）、
   mark の文字は app_id の最後の語（最後の `.` か `-` の後。`files` → F、`terminal` → T、
   `browser` → B、`mview` → M）の最初の文字、無ければ従来どおり題の最初の文字（`mark_name`）。
   Files・Terminal・Browser・mview の app_id は既にこの形なので変えない。
4. **X の client の app_id**: libX11 に `XSetClassHint`（`XClassHint`、`Xutil.h`、`XA_WM_CLASS`）。zterm は
   `zterm`/`XTerminal`、zgears は `zgears`/`Gears` を設定。xserver は WM_CLASS（ChangeProperty で保たれる）の
   class（無ければ instance）を desktop の窓の app_id にする（`x11_window_class`）。無い X の窓は従来の
   `xserver`。窓を開いた後の WM_CLASS の変更は反映しない（最初の一回）。

## 検証（amd64、Venus の guest、2026-09-28。image は `plan/ws074/tests/build-browser-image.sh`）

- `plan/ws035/tests/zdesktop-p092.sh`（新規。p090 の demo の一覧の sheet を撮り、ZWL MAP の場所を調べる）PASS:
  Files (80,98)、Terminal (182,127)、Browser (230,98)、Model viewer (278,146)、Gears (340,203)、zterm (320,251)。どの角も
  前の窓の角から 32 px 以上離れ、作業域の中。Browser は作業域より高く（WS074 に報告済み）どこでも Terminal の題を
  一部隠すため、隠す数が最小の場所になった。
- 画面（`/home/awe/zedBSD-rpi4/build/ws035-shots/`）: `p092-20260928-venus-before-all.png`（p090 の sheet）と
  `-after-all.png`、`-titlebars-before-after.png`（上が前、下が後: Gears・Model viewer の題が黒い窓の上で読める）、
  `-after-01-files.png`…`-after-06-x-terminal.png`。mark の文字は F・T・B・M・G・X（zterm は class の XTerminal）。
- 回帰 PASS: files-p018（2 つ目の Files は中央だと 1 つ目の題を隠すので右下へ 1 段: x=128 y=146。期待値を新しい
  置き方に直した）、zdesktop-p084、zdesktop-p088、zdesktop-p070、x11-p003。browser image には make-home.sh が
  無いので、回帰の前に guest へ置いた。
- 規約: `shell.c`・`protocol.c`・`zwl.h`、x11server の 4 file、zgears、`Xutil.h`・`X.h` は style-check 0（前も 0）、
  `xlib.c` 186・zterm 61 は前と同数。build warning は自分の file に 0（X.h の変更で全体が build し直され、外部 package と
  noct の既存の warning が出る）。
- 実機（i915）: 未実施。boot test: ユーザーの指示で無し。

## 残り

- 非 active なタイトルバーの濃さは固定の値。下の明るさに合わせる（下を測る）のは残り。
- 窓を開いた後の WM_CLASS の変更、app_id の変更の後の Home の launch の icon との対応（Home は名前の頭文字）。
- 大きすぎる窓（Browser）は、どこに置いても下の窓の題を隠す。
