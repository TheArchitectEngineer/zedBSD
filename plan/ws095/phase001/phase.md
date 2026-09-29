<!-- awesome-plan project=zedbsd record=ws095-p001 -->

# ws095-p001: IME の設計

Status: cleared（2026-09-29。人間の判断 D1〜D14 は既定で進め、ユーザーの答えで design.md を直す形）
Disposition: normal
Parent: [WS095](../ws.md)
Queue: main が割り当て（2026-09-29、worktree `.claude/worktrees/ws095-ime`、branch `wt/ws095`）。Queue の ID は main が記録する

## 範囲と受け入れ

compositor（`userland/desktop/wayland`）の `zwp_input_method_v2`・`zwp_text_input_v3` の仲介、IME の program（候補の窓は input-method の popup surface）、
ローマ字→かな→変換、辞書（REmacs の SKK の辞書、image への入れ方）、app の側（Terminal・Text Editor・Browser の text field・Settings の検索）、
切り替えの key を設計する。source は変えない（他の WS が wayland・terminal・browser・textedit を変えているため）。人間の判断が要る点は既定を選んで挙げる。

受け入れ: design.md が全節あり、protocol の XML の入手元と pin が確かめてあり、敵対的なレビューの指摘を source で確かめて反映し、判断の点が既定と
一緒に挙がっていること。→ 満たした（下）。

## 結果

1 回目（前任、2026-09-29、wrap up で中断）: [design.md](../design.md) の §1〜§14 を書いた。確かめた事実（zdesktop の手書きの protocol、`zwl_seat_key()`
の順、US だけの keymap、usb-hid の Japanese の key、`SKK-JISYO.X` の 16,412 見出し、REmacs の revision `1a72439`、WS092 の口、titlebar と home の field）。

2 回目（引き継ぎ、2026-09-29）:

### protocol の XML の入手元と pin（design §3.1）

| XML | 入手元 | SHA-256 | license |
| --- | --- | --- | --- |
| text-input-unstable-v3.xml | build の host の Debian の wayland-protocols 1.44-1 | `49048087…1982` | HPND 型（MIT 系） |
| input-method-unstable-v2.xml | wlroots tag 0.19.2（`a047c2a33ff7724a476892cc4fe5dcb803607ef5`）`protocol/` | `99414dba…0703` | MIT |
| virtual-keyboard-unstable-v1.xml | 同上 | `7ad78700…94f5` | MIT |

- 確かめた: host の 1.44 に input-method v2・virtual-keyboard は無い（v1 だけ）。wlroots の raw を `curl` で取得し hash を取った（`git ls-remote` で tag の commit を確かめた）。
- `build/distfiles/wayland-protocols-1.49.tar.xz` の text-input-v3 は interface の version 2（action・language・preedit_hint、二段の適用）。zdesktop は v1 で広告する。
- 1.49 の `experimental/xx-input-method-v2.xml`（grab 無し、「後方互換の無い変更が予想される」）は使わず `zwp_input_method_v2` を選んだ。
- 辞書の pin（design §9.1）: REmacs の commit の GitHub の archive `1a724393…tar.gz`（597,025 byte、SHA-256 `419d03a1…1507`）を取得し、中の
  `dict/SKK-JISYO.X` の SHA-256 `73819384…1ab9` が共有の `build/sources/remacs` の X と同じことを確かめた。

### レビューの反映（design-reviewer、指摘 45 件: 高 9・中 26・低 10）

指摘の高の全てと主な中を source で確かめてから反映した（確かめた例: `seat.c:830-880` の key の順、XML の text-input の「commit の数を done の serial に」、
X に `みm`・`かi`・`よn`・`いt`・`いい` が無く `すr /刷/擦/磨/`・`い /胃/…/` があること、remacs の Makefile の `REMACS_GIT_REF ?= main`、Terminal の
font が `keiland-mono.ttf` の 1 つ、`usb-hid.c:2226` の KEY_KATAKANAHIRAGANA、`libwayland/client.c:42` の WAYLAND_SOCKET、WS092 の `char preedit[64]`）。

| 指摘 | 反映（design の節） |
| --- | --- |
| A1 自前の field・Home が IME の分岐より前に key を取る | 切り替えの key を home より前に、titlebar の field は field の処理より前で grab へ（§4.2 の 1・3）、Home は範囲外（§1・D13） |
| A2 menu・tab の shortcut（F10・Ctrl+Z）が変換中に効く | IME の `composing` を status で知らせ、変換中は shortcut より前で grab へ、戻った key にだけ shortcut（§4.2 の 3・4、§8） |
| A3 押下中の key・modifiers の整合 | key ごとの宛先の記録、release の合成、grab 中の modifiers は VK の値だけ（§4.2 の 5・6、§4.5） |
| A4 IME のハングで入力できない | watchdog 500 ms、D5 の既定を「直接入力では grab を飛ばす」に変更（§4.2 の 2、§4.5、D5） |
| A5・A6・H3 起動・pid の信頼・回収・再起動 | socketpair と WAYLAND_SOCKET、client の印、切断で検出、60 秒に 3 回（§2.1） |
| A7 全画面・合成の経路 | plain・glass の両経路、direct scanout を止める、file を p005 に（§4.3、§13） |
| A8 内部の field と client の text input の同時 | activate を field に移し、終われば戻す（§4.4） |
| A9 lock・greeter・Home・Wiseview | deactivate と popup を隠す（§4.3） |
| A10・A11 VK の keymap の fd、indicator の file | fd を即 close、`shell.c`・`network.c`（§4.2 の 7、§8） |
| B1 done の serial の誤り | 2 種類の serial を別に数える、合わない時も文字を渡す（§3.2） |
| B2 password 以外の保護 | pin・hidden_text・sensitive_data も IME を通さない（§4.1、§7.4） |
| B3 focus の変化の preedit | zdesktop が確定して送る（§4.1、D10 を追加） |
| B4 その他の protocol の規則 | 2 つ目の enable、全ての text_input への enter、4000 byte の分割（§3.2） |
| C1 分割の費用で不明が常に勝つ | 費用を 不明の字数 → 1 文字の名詞 → 文節の数 → 語の長さ に、不明は最長の連続（§7.2） |
| C2・C3 辞書は辞書形だけ、音便・一段が引けない | `ja-inflect.c` の活用の規則（音便・r の両仮説・形容詞・母音の見出し）、語尾の automaton、する・くる・いい の固定の表（§7.2.1） |
| C4 助詞の表の不足 | 連接・助動詞を足し、不明の文節にも付ける（§7.2） |
| C5 ローマ字の抜け・捨てる文字 | 綴りを足し、合わない英字を残す、数字・空白（§7.1、D11 を追加） |
| C6・C7 p002 の試験が p003 に依存 | p002 は固定の小さな辞書、見出しの数は pin と組（§12） |
| D1〜D3（key） | カタカナ/ひらがな を追加、変換中の 変換 は変換、PS/2 は p010（§10） |
| E1〜E5 app の口 | Browser は `form.c`、Terminal の CJK の font の依存と 256 byte、WS092 の口の違い、Home を外す（§11、D14 を追加） |
| F1〜F4 試験・Phase | guest の判定は SSH で file、境界の試験を追加、p004 を p004・p005 に分け依存を直した（§12・§13） |
| G1 辞書の pin が事実と違う | commit を固定した tarball と X の hash（§9.1） |
| G2・G3 判断の見直し・抜け | D5 変更、D10〜D14 を追加（§14） |
| H1 Terminal の password が IME を通る | echo off の間 sensitive_data・hidden_text（未確認の点は p006 で、§11） |
| H2 利用者の辞書・log | 0600・fsync・大きさの上限・log に文字を出さない（§7.4、§4.5） |

反映しなかったもの: なし（H4「unauthorized は使われない」は事実として §2.1 に注記しただけ）。確かめられなかった点（レビューも推測とした）:
5330 の内蔵 keyboard が PS/2 か、QEMU の qcode に henkan 等があるか、Terminal が master の側から termios を読めるか、fsync をしない時の UFS の挙動。
これらは該当の Phase（p010・p004・p006）で確かめる。

### 判断の点（main 経由でユーザーに聞く。既定で進めた）

design §14 の D1〜D14。特に D2（変換の操作）、D3（補いの辞書の量と範囲）、D7（JIS の配列と 半角/全角）、D1（辞書の license の表示）、
D14（Terminal の CJK の font）。

## 実行したコマンド（要点）

- `git merge -m WIP main`（worktree を main に合わせた）
- `ls /usr/share/wayland-protocols/unstable/...`、`sha256sum`、`tar tJf build/distfiles/wayland-protocols-1.49.tar.xz`、`diff`（1.44 と 1.49 の text-input-v3）
- `git ls-remote https://gitlab.freedesktop.org/wlroots/wlroots.git`、`curl` で wlroots 0.19.2 の XML 2 つ
- `curl` で REmacs の archive、`tar … -O | sha256sum`
- `grep` で `SKK-JISYO.X` の見出し、`seat.c`・remacs の Makefile・usb-hid・libwayland・WS092 の textedit の確認
- design-reviewer の agent（読むだけ、変更なし）

build・QEMU・host の試験は行っていない（設計の Phase）。source は変えていない。

## Resume point

p001 は cleared。ユーザーの答え（D1〜D14）で design.md を直す。次は ws095-p002（日本語の engine、host の試験）。
