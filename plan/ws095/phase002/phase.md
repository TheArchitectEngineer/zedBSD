<!-- awesome-plan project=zedbsd record=ws095-p002 -->

# ws095-p002: 日本語の engine（Wayland 無し）と host の試験

Status: cleared（2026-09-29）
Disposition: normal
Parent: [WS095](../ws.md)
Queue: main が割り当て（2026-09-29、worktree `.claude/worktrees/ws095-ime`、branch `wt/ws095`）。Queue の ID は main が記録する

## 範囲と受け入れ

[design.md](../design.md) §5〜§7・§12 の engine の部分: ローマ字 → かな、SKK の辞書の読み込みと引き、活用の規則、文節の分割と候補、利用者の辞書
（学習）、変換の操作。Wayland を知らない C で `userland/desktop/ime/` に置き、host（Linux）の試験を ASan・UBSan で流す。試験は固定の小さな辞書で行い、
X の品質は p003 で計る。main の指示（2026-09-29）: D2 が変わっても差し替えられるよう、変換の操作の層を engine の中心から分ける。

受け入れ: engine の file が coding-style の全文に沿って warning 無しで build でき、host の試験（ローマ字・辞書・活用・分割・学習・key の列）が全て通ること。

## 前提と判断

- ユーザーの判断（2026-09-29、main 経由）: D2 = MS-IME の型（設計の既定どおり）、D3 = 補いの辞書を書き下ろす、D7 = デモ機は US 配列で
  JIS は後回し・切り替えは Super+Space。design.md の §14・§10.3 を決定に直した。D14 は main が既定（WS095 の中で p006 の前提として足す）に決めた。
- HAL・toolchain は変えていない。共有の `build/` は読むだけ（`build/sources/remacs/dict/SKK-JISYO.X` を試験で読む）。

## 結果

### 作った file（`userland/desktop/ime/`、新規、Zlib）

| file | 役割 |
| --- | --- |
| `engine.h` | 言語の engine の interface（`ime_key`・`ime_output`・`ime_engine_ops`、`ja_config`、evdev の code と text-input-v3 の hint・purpose の値） |
| `output.c` | 出力の初期化と確定の文字列の追加（4000 byte の上限） |
| `engine-direct.c` | 直接入力の engine（全ての key を戻す） |
| `ja.h` | 日本語の engine の部品の型と関数 |
| `ja-kana.c` | UTF-8、カタカナ・半角カタカナ・全角英数、かなの子音と母音（REmacs の `KANA_CONS` と同じ）、五段の行 |
| `ja-romaji.c` | ローマ字 → かな（REmacs の表＋ca・qa・she・thi・tsa・ltu・xwa・n'・tch 等。合わない英字は捨てずに残す） |
| `ja-dict.c` | SKK の辞書（file 全体を読み、FNV-1a の hash の表。壊れた行は数えて飛ばす、注釈と Lisp の候補は出さない、8 MB の上限） |
| `ja-user.c` | 利用者の辞書（SKK の形式、最後に選んだものが先、一時 file → fsync → rename、mode 0600、1 MB の上限、10,000 見出し） |
| `ja-inflect.c` | 活用の規則（五段の行と音便、最初のかな＋一段・ら行五段・形容詞、r の一段、i・k の形容詞、語尾の automaton、する・来る）と助詞の表 |
| `ja-segment.c` | 動的計画法の分割（費用 = 不明＋1 字の名詞 → 文節の数 → 辞書の長さ）と文節の候補の一覧 |
| `ja-engine.c` | engine の中心（key を知らない操作: 打つ・変換・候補・文節の移動と伸縮・F6〜F10 の形・確定と学習・出力）と `ime_engine` の ops |
| `ja-keys.c` | 操作の層（MS-IME の型の key の割り当て）。D2 を替える時はこの file だけを替える |

### 試験（`plan/ws095/tests/`）

- `host-engine.sh`（`host-engine.c` を engine の source と一緒に clang・`-fsanitize=address,undefined -fno-sanitize-recover=all`・`-Werror` で build して流す）、
  `ja-test.dict`（固定の辞書: X の行の写し＋わたし/私・にほん/日本・でんしゃ・のr、注釈・Lisp・壊れた行 3 つ）。
- `host-engine convert <system> [<supplement>] -- <読み>...`: 辞書で読みを変換して分割と第一候補を出す（p003 の計測に使う）。

## 実行したコマンドと結果

- `git merge -m WIP main`（開始時）。
- `timeout 300 sh plan/ws095/tests/host-engine.sh` → **`host-engine: 142 passed, 0 failed`**（最後の実行、2026-09-29）。内訳: ローマ字 23、辞書の読み込み 12
  （固定の辞書 31 見出し・壊れた行 3・注釈・Lisp・ENOENT・EFBIG）、**X の読み込み 3**（`build/sources/remacs/dict/SKK-JISYO.X`: 壊れた行 0、見出し 16,412 =
  注釈でない行の数）、活用 28、分割 15（本を｜読んだ、食べています、今日は｜いい｜天気です、勉強します、私は｜日本語を｜話します、会社に｜行きます、
  見ます、高かった、難しい、考えています、物を｜変った、電車に｜乗って、今日は｜いい｜天気ですね、ぱそこんを と パソコンを の候補）、利用者の辞書 15
  （学習の順、0600、読み直し、壊れた file、上限を超える file）、key の列 40 余り（Space の変換・候補・窓・数字の選択・Enter・学習・Esc・文節の移動と
  Shift の伸縮・入力中の確定・F6〜F10・大文字の英字・Ctrl の素通し・数字・reset の確定と破棄・上限・矢印・sensitive_data で学習しない）、直接入力 2。
- `gcc -std=c11 -O2 -Wall -Wextra -pedantic -Werror -Wdeclaration-after-statement -c`（全 file）→ warning 0。
- `clang -std=c11 -Wall -Wextra -Werror -Wdeclaration-after-statement -Wshadow -c`（全 file）→ warning 0。
- coding-style の機械の見直し（条件の中の関数の呼び出し・閉じ括弧の後の空行・呼び出しの結果の直接の return を探す script、if・for の前の目的の
  comment を探す script）→ 見つかった 31＋12 か所を直した。`git diff --check` → 問題なし。
- `host-engine convert <X> -- …`（15 文）→ 結果は design §7.5。

## 実装で決めたこと（design に反映）

- 費用の 1 つ目を「不明の字数＋1 字の名詞の数」の和にした（ぱそこん が ぱそ｜子｜ん になったため）。§7.2。
- 助詞は「表の項 1 つ（連接を含む）＋文末の助詞 1 つ」に絞った（に＋の＋って が 1 文節になったため）。助詞だけの文節を許し、1 つの助詞なら ひらがな を先に。§7.2。
- する・来る の し・き・こ 1 字だけの形は語にしない（こ が 子 より安くなったため）。§7.2.1。
- 候補の順を決めた（名詞＋する は動詞の後。はなします が 花します になったため）。§7.2。
- 辞書は完全一致の hash だけで、前方一致の表は作らない。§7.3。
- 学習の見出しは文節の読み全体、学習した読みは分割でも辞書の語として数える。§7.4。
- engine の interface に `content_type` を足し、sensitive_data・hidden_text・password・pin の時は学習しない（design §7.4 の二重の守り）。§6。
- 入力中の矢印は IME が取って何もしない（preedit の中の cursor の移動は作らない、制限）。§7.1。

## 未実施・制限

- **guest の toolchain での build は未実施**（この worktree に config.mk が無い。p004 で package の Makefile を足す時に guest で build する）。package の
  Makefile はまだ無い（engine だけで program が無いため。p003・p004）。
- coding-style の全文への適合は、機械で探せる規則と目で見た範囲で直した。全文の見直しは WS の規約の適合の Phase（p011）で行う。
- 変換の品質（X での第一候補の正しさ）は計っていない（p003）。見つかった誤りは design §7.5。
- 実機・QEMU の確認は無い（Wayland の部分が無いため）。

## Resume point

p002 は cleared。次は p003（辞書の package と品質の計測、補いの辞書の案）か p004（protocol と zdesktop の仲介）。
