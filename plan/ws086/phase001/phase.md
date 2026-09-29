<!-- awesome-plan project=zedbsd record=ws086-p001 -->

# ws086-p001: GNU ls との差の一覧、列の配置の設計、含める option の範囲

Status: cleared
Disposition: normal
Parent: [WS086](../ws.md)
Queue: main が subagent（worktree `wt/ws086`）へ依頼（2026-09-29）
Approval: ユーザー（2026-09-29）「lsの結果が1つずつ改行されています。GNU lsと同じにしたいです。」

## 範囲と受け入れ

- 今の `userland/base/ls/main.c` と GNU coreutils の ls（host の 9.7）の出力の差を一覧にする。
- 列の配置（`-C`・`-x`）、幅の決め方、既定の format の選び方を GNU に合わせる設計を書く。
- 含める option と含めない option を決め、理由を書く。人間の判断が要る点は既定を選んで記録する。
- 受け入れ: 下の設計と範囲が p002 の実装の手順として使えること。コードは変えない。

## 観察の方法

host の GNU ls（`ls (GNU coreutils) 9.7`）を、[tests/tty-run.py](../tests/tty-run.py)（出力を指定の幅の擬似端末に
つないで byte のまま取る。OPOST を切るので `\n` はそのまま）で端末として、また pipe で走らせ、`od -c`・`cat -A` で
見た。locale は `LC_ALL=C` と `LC_ALL=C.UTF-8`（Kei の libc が持つ 2 つ）。GNU の実装は coreutils の `ls.c`
（`decode_switches`・`calculate_columns`・`print_many_per_line`・`print_horizontal`・`print_with_separator`・`indent`・
`length_of_file_name_and_frills`）と gnulib の `quotearg.c` の振る舞いを観察で確かめた。

## GNU ls との差（2026-09-29 の main.c）

| # | 項目 | 今の ls | GNU ls |
| --- | --- | --- | --- |
| D1 | 既定の format | 常に 1 行に 1 つ | 出力が端末なら `-C`（縦の順の列）、端末でなければ `-1` |
| D2 | 行の幅 | `-C` は固定の 80 | `-w N`（`--width`）→ 出力が端末なら `TIOCGWINSZ` の `ws_col`（0 は無視）→ `COLUMNS` → 80。0 は無制限。幅を使う format（`-C`・`-x`・`-m`）のときだけ求める |
| D3 | 列の配置 | 全列が同じ幅（最長の名前＋2、`-F` なら全員に＋1）、列の数 = 80 / 幅 | 列ごとに幅が違う。列の数を最大（`max_idx = 幅/3 + (幅%3 != 0)` と名前の数の小さい方）から減らしながら、各列の最長の名前＋2（最後の列は＋0）の和が幅より小さい最大の列の数を選ぶ（`calculate_columns`）。行の数 = ⌈数/列⌉ |
| D4 | 列の間の空白 | 空白だけ | `indent()`: tab の幅（既定 8、`-T`・`TABSIZE`）で次の tab stop を越えるなら tab、それ以外は空白。行の最後の名前の後には何も書かない |
| D5 | `-x`・`-m`・`-w`・`-T` | 無い | `-x` は横の順の列（同じ列の計算、`idx = filesno % cols`）、`-m` は `, ` で区切り幅で折る、`-w`・`-T` |
| D6 | format の option の優先 | `-l` が常に勝つ。`-1`・`-C` は互いに打ち消す | `-1`・`-C`・`-x`・`-m`・`-l` は最後に書いたものが勝つ（POSIX も同じ） |
| D7 | `-i` | 番号を詰めて書く | 一覧の中の最大の桁に右寄せ（`-1`・`-C`・`-x`・`-l`。`-m` は詰める）。列の幅にも入る |
| D8 | `-F` の列の幅 | 全員に 1 を足す | 印の付く名前だけ 1 を足す |
| D9 | `-lF` | 印を付けない | 名前に印（symbolic link の名前には付けない）、link の先に先の種類の印 |
| D10 | 端末での名前の quote | byte のまま | shell-escape: shell の特別な文字を含む名前を `'…'` で囲む（`'` だけなら `"…"`、制御文字は `'$'\ooo'` など）。`-C`・`-x`・`-l` では一覧に quote された名前が一つでもあれば他の名前の前に空白を 1 つ置いて揃える。directory の見出しと link の先も同じ。pipe では byte のまま |
| D11 | 名前の幅 | byte の数 | locale の文字の表示幅（`mbrtowc`・`wcwidth`）。C locale では 0x80 以上の byte は印字できない文字として端末では escape |
| D12 | `-N`・`-q` | 無い | `-N`（literal、端末では制御文字を `?`）、`-q`（印字できない文字を `?`） |
| D13 | option の位置 | 最初の operand で option が終わる | operand の後の option も読む（`POSIXLY_CORRECT` があれば POSIX の順）。長い形（`--width=N` など） |

どれも pipe の出力（1 行に 1 つ、quote しない）は変えない（D7 の `-i` の右寄せと D9 の `-lF` を除く）。POSIX の試験
（WS001・WS043 の `plan/tools/utils/cases/`）は pipe で走るので、既定の format は変わらない。

## 設計

### format と幅

- `struct options` の `one`・`columns` を一つの `format`（`LS_FORMAT_ONE`・`LS_FORMAT_COLUMNS`・`LS_FORMAT_ACROSS`・
  `LS_FORMAT_COMMAS`・`LS_FORMAT_LONG`）に置き換え、`-1`・`-C`・`-x`・`-m`・`-l` は最後の一つが決める。既定は
  `isatty(STDOUT_FILENO)` なら `COLUMNS`、でなければ `ONE`。
- 幅: `-w N`（`--width=N`、10 進、不正なら `ls: invalid line width: 'X'` で終了状態 2）。format が `COLUMNS`・`ACROSS`・
  `COMMAS` のときだけ、`-w` が無ければ出力が端末なら `TIOCGWINSZ` の 0 でない `ws_col`、次に空でない `COLUMNS`
  （不正なら `ls: ignoring invalid width in environment variable COLUMNS: 'X'` を出して無視）、最後に 80。
- tab の幅: 既定 8。`POSIXLY_CORRECT` が無ければ `TABSIZE`（不正なら `ls: ignoring invalid tab size in environment
  variable TABSIZE: 'X'`）。`-T N`（`--tabsize=N`、不正なら `ls: invalid tab size: 'X'` で 2）。0 は tab を使わない。
- 列の計算は GNU の `calculate_columns` と同じ手順（上の D3）。列の情報（各列の幅の配列と行の長さ）は `max_cols` 個を
  一度に calloc し、一覧ごとに作って捨てる（GNU は使い回すが、出力は同じ）。
- `-m`: 名前の間に `,`。次の名前が `, ` の後に入らないなら（`pos + 2 + 長さ > 幅`、幅 0 は無制限）`,\n`、入るなら `, `。
- 各名前の「長さ」（GNU の `length_of_file_name_and_frills`）= `-i` の桁（`-m` は自身の桁、他は一覧の最大の桁）＋1、
  quote した名前の表示幅（揃えの空白を含む）、`-F` の印があれば 1。

### 名前の書き方（quote）

- quote の様式: 出力が端末なら shell-escape、でなければ literal。`-N` で literal。
- 制御文字の扱い: 端末なら印字できない文字を `?`（literal のとき）、`-q` で常に `?`（literal のとき。shell-escape では
  escape が先に働く）。
- shell-escape の規則（gnulib の `shell_escape_quoting_style`、観察で確かめた範囲）:
  - quote が要る文字: 空白、`! " $ & ( ) * ; < = > ? [ \ ^ \` |`、tab と改行と他の制御文字、先頭の `#`・`~`、名前全体が
    `{` か `}` 一文字のとき。`'` も要る。印字できない文字（locale による）も要る。
  - 要らなければ名前のまま。要るなら `'…'` で囲む。中の `'` は `'\''`。
  - 印字できない byte・文字は、`'` を閉じて `$'\ooo'`（`\t`・`\n` などは `$'\t'`）を挟み、また `'` で開く
    （例 `'ctl'$'\001''x'`、先頭なら `''$'\343…'`）。
  - `'` を含み、他に escape の要る文字が無ければ `"…"`（例 `"it's"`）。`'` と `"` の両方なら `'both'\''"'`。
  - 端末の `-C`・`-x`・`-l` では、一覧のどれかが quote されたら quote されない名前の前に空白を 1 つ置く。`-1`・`-m` では置かない。
- 正確な規則は p002 で host の GNU と多数の名前で比べて合わせる（下の試験）。

### その他

- `setlocale(LC_ALL, "")` を呼び、表示幅は `mbrtowc`・`wcwidth`、印字の可否は `iswprint`（C locale では `isprint`）で決める。
  並びの順は `strcmp` のまま（Kei の libc の `strcoll` は `strcmp` と同じ）。
- option の読み取りを `userland/base/common/command.h` の `command_options` に移す（GNU の順、`POSIXLY_CORRECT`、長い形:
  `--width`・`--tabsize`・`--literal`・`--hide-control-chars`・`--all`・`--directory`・`--classify`・`--human-readable`・
  `--inode`・`--dereference`・`--recursive`・`--reverse`）。不明な option の message と終了状態は今の usage と 1 のまま。
- `-i` の番号は一覧（directory の中身、operand の file の群）の最大の桁に右寄せ。status の無い名前は `?`。

## 含める option と含めない option

含める（p002）: 既定の format の選択（D1）、`-1`・`-C`・`-x`・`-m`・`-l` の最後が勝つ（D6）、`-w`・`--width`、`TIOCGWINSZ`・
`COLUMNS`、`-T`・`TABSIZE`、GNU の列の計算と tab の indent（D3・D4）、`-i` の右寄せ（D7）、`-F` の幅と `-lF`（D8・D9）、
端末での shell-escape の quote と揃えの空白（D10）、表示幅（D11）、`-N`・`-q`（D12）、option の GNU の順と長い形（D13）。

含めない（理由と再検討の契機。main に Future Work への登録を依頼する）:

- `--color`・`LS_COLORS`・`dircolors`: GNU ls の既定は色なし（色は distribution の shell の alias `ls --color=auto` が付ける）。
  `LS_COLORS` の database と `dircolors` が要り、既定の見た目ではない。契機: ユーザーが色を求めたとき。
- `-A`・`-s`・`-g`・`-o`・`-n`・`-S`・`-X`・`-v`・`-U`・`-c`・`-u`・`--group-directories-first`・`--time-style`・`-p`・`-k`・
  `--hyperlink`・`--dired`・`-b`・`-Q`・`--quoting-style`・`QUOTING_STYLE`・`--format=WORD`・`--help`・`--version`: 既定の見た目に
  関わらない追加の機能。契機: 必要な利用者の指示。
- 不明な option の message（GNU は `ls: invalid option -- 'z'` と `Try 'ls --help'…`、終了状態 2）: 今の `usage:` と 1 を保つ
  （POSIX の試験の前提を変えないため）。

## 人間の判断が要る点（既定を選んで先へ進んだ）

1. **locale と日本語の名前**: GNU は locale に従う。Kei の guest の session は `LANG`・`LC_*` を設定していない（`userland/desktop`・
   `platform`・`userland/base/login` に無い）ので C locale になり、GNU と同じにすると、Terminal では UTF-8 の名前（日本語）が
   `''$'\343\201\202'` の形で出る（今は byte のまま出て読める）。既定: **GNU と同じく locale に従う**。日本語の名前を読めるように
   するには session に `LANG=C.UTF-8` を設定する別の作業が要る（WS086 の範囲外。main に判断と計画を依頼）。
2. **色**: 含めない（上の理由）。
3. **option の順**: GNU の順（operand の後の option）を採る。`POSIXLY_CORRECT` があれば POSIX の順。

## 試験の計画（p002）

- host: `plan/tools/utils/build-host-utils.sh` の方法で ls を host 向けに build し、[tests/](../tests/) に比較の script を置く:
  GNU の ls（`LC_ALL=C` と `C.UTF-8`）と我々の ls を、端末（tty-run.py、幅 0・20・40・80・132）と pipe で、名前の集合
  （短い名前の多数、長い名前の混在、quote の要る名前、制御文字、UTF-8、1 つだけ、空）と option（既定、`-1 -C -x -m -l -i -F
  -iF -lF -w0 -w30 -T0 -T4 -N -q -R -d -a -r -t`、operand の file と directory の混在）で byte 単位に比べる。`-l` の時刻と所有者の
  列は GNU と同じ形なので、同じ tree の上でそのまま比べる。
- 回帰: `plan/tools/utils/util-diff.py --only ls` 相当（ls を使う case を含む files・chmod などの case）、`ls-compare.sh` の
  旧 ls との比較（pipe の出力は D7・D9 以外変わらないこと）。
- guest（amd64）: SSH の guest で `ls`・`ls -x` などを端末（`ssh -t` の擬似端末、`stty cols`）で走らせ、Terminal の画面を撮る。

## 結果

- 差の一覧 D1〜D13、設計、範囲、判断の既定を記録した。コードは変えていない。
- 実施した確認: host の GNU ls 9.7 の観察（上の方法）。build・QEMU は不要（設計の Phase）。
- 次: ws086-p002（実装と試験）。
