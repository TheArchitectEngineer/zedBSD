<!-- awesome-plan project=zedbsd record=ws154 -->

# WS154: Settings の Languages の頁で IME を選ぶ（日本語・SKK・なし=英語）と、SKK の IME の新しい実装

<!-- awesome-plan-current:start -->
Status: planning
Primary Milestone: MG006
Related Milestones: —
Parent: [Master](../master.md)
Queue: なし（担当と時期は未定）
Resume point: p001（設計）から。
<!-- awesome-plan-current:end -->

## 単一目標

Settings に Languages の頁を足して、使う IME を「日本語」「SKK」「なし（英語の mode）」から選べるようにし、SKK の入力方式の IME を新しく実装する。

## ユーザーの指示（2026-10-04 夜、原文）

「SettingsにLanguages画面を追加して、IMEを選択できるようにします。また、選べるIMEは、日本語、SKKです。何もIMEを選ばないときは英語モードです。SKK IMEは新規実装します。辞書はEmacsのものを使ってください。」

## 用語（Q1 の整理）

- **日本語**: 今の Kei の IME（[WS095](../ws095/ws.md)、ローマ字かな変換で文節を変換する方式。辞書は SKK の形式の `SKK-JISYO.X`・`SKK-JISYO.kei` を使う）。
- **SKK**: SKK の入力方式（Emacs の SKK・ddskk の操作: 大文字で変換の開始、送り仮名の区切り、▽・▼ の mode、`l`・`q`・`C-j` などの mode の切替）の IME を新しく実装する。
- **なし**: IME を使わない英語の mode（key がそのまま入る）。

## 範囲（p001 で設計して確定）

1. **Settings の Languages の頁**（新しい頁。pages.c の表・glyph・検索の語）: IME の選択（日本語・SKK・なし）。設定は kl_settings_* → compositor の desktop.conf（Guardrail の「app と設定」）。選択を変えると login の session の中ですぐ切り替わる（再 login 不要かは設計で決める）。
2. **IME の切り替えの仕組み**: compositor の input method（zwp_input_method_v2 の側、keiland-ime の process）が選んだ IME を起動・切り替える。今の IME の on・off の key・右上の IME の status（A／あ、ws095-p005）との関係、SKK の mode の表示（▽・▼・かな・カナ・英数）。
3. **SKK の IME の新しい実装**: SKK の操作の状態機械、送り仮名、変換の候補の選択（space・x・候補の窓）、辞書の登録（再帰の登録の mode）、利用者の辞書の保存（今の日本語の IME の辞書の保存（BUG-143、入力が無い 3 分の後）と同じ考え）。
4. **辞書**: Emacs（`userland/base/emacs/dict/` の REmacs の SKK の辞書、`SKK-JISYO.X`・`SKK-JISYO.remacs`）を使う（ユーザー）。install の場所は今の IME の辞書の path の整理（`/usr/share/kei/ime` → `/usr/share/keiland/ime` の案、ユーザーの判断待ち）と合わせる。**辞書は重複して持ち、別々に管理する**（2026-10-04 夜 ユーザー「Emacsの辞書は重複して持ってください。別々に管理します。」）: SKK の IME 用に Emacs の辞書の複写を SKK の IME の側（例: `userland/desktop/ime/skk/dict/`）に置き、日本語の IME の辞書・`userland/base/emacs/dict/` とは独立に更新する。
5. Linux・FreeBSD の Keiland でも同じ（IME は Keiland の process）。

## Phase（案）

| Phase | 内容 | Status | 依存 |
| --- | --- | --- | --- |
| ws154-p001 | 設計（IME の切り替えの仕組み、Languages の頁、SKK の状態機械と操作の範囲、辞書の共有と path、試験の方法） | planning | WS095 の今の構成 |
| ws154-p002 | IME の選択の仕組みと Languages の頁（日本語・なし） | planning | p001 |
| ws154-p003 | SKK の IME の実装（host の試験で状態機械と変換） | planning | p001 |
| ws154-p004 | SKK を選択に加え、QEMU（T1）と実機の UAT、全文の規約 | planning | p002、p003 |
