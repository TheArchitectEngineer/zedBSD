<!-- awesome-plan project=zedbsd record=ws131-p022 -->

# ws131-p022: compositor の log の接頭辞を KWL に（試験と同時に）

Status: cleared（2026-10-07 Q1 の判定: T1-275 の PNG（Q1 が見た）、後の回帰（T1-281: boot-test・textinput-p013・viewers-p008・demo-s8-s9・titlebar-p010・files-regress、T1-295 menu-p003、Linux の 8 app の PNG、FreeBSD の backend-test）も PASS）（旧: in-progress（q820、P1。置き換えと build・host の確認まで済み、T1 の結果待ち））
Disposition: normal
Parent: [WS131](../ws.md)、計画の正本 [design.md](../design.md)
Queue: q820（2026-10-06）
依存: p021 cleared。compositor と試験の他の作業が無い時
目安: 3〜4h（1 Queue）。実行者: Q1 が割り当てる（high）
所有 path: `userland/desktop/wayland/` の log の文字列（44 file、63 種類の tag）、`plan/ws131/`。（Q1 の委任が要る: Q1 が Phase の前に委任を記録する）: `"ZWL "` を読む試験の script 201 本（sh・py、`plan/history` を除く。他の WS の試験を含む）

## 目的と結果

compositor の log の行の接頭辞 `"ZWL "` を `"KWL "` にし、それを読む試験 201 本を同じ commit で直す（2026-10-03 user の注意: log の文字列の変更は symbol の改名と別の段に）。

## 範囲

1. 最初に `grep -rlE "ZWL [A-Z]" plan` で全ての試験を列挙して phase.md に表で残し、機械的に置き換える（`ZWL_` の識別子や関係の無い文字列を巻き込まない pattern）。
2. BUG の ticket や history の文書の中の log の引用は変えない。

## 受け入れ

- 3 OS の build（zedBSD の amd64 は exit 0・自前の warning 0、`make keiland-linux` の gcc と clang、FreeBSD の native build と `native-build-audit.py`）と `plan/tools/keiland-os-boundary/check.sh` PASS。
- 列挙した試験の全てが新しい接頭辞を読む。compositor に `"ZWL "` の文字列が 0。
- zedBSD: C1・C2・C9（criteria.sh）を前後で、各 WS の代表の試験（`zdesktop-p101.sh`・`settings-regress.sh`・`volume-p005.sh`・`textinput-p013.sh`・`demo-s8-s9.sh`）を前後で流し、結果が同じ。

## 検証の方法

[zedbsd-commands.md](../../tools/keiland-linux/zedbsd-commands.md)、[keiland-linux/README.md](../../tools/keiland-linux/README.md)、[keiland-freebsd/README.md](../../tools/keiland-freebsd/README.md)。QEMU の console・serial の log で判定しない（不具合は gdbstub・monitor・QMP）。build は自分の `BUILD=build/ws131-pNNN/`、共有の `build/` を消さない。FreeBSD の起動の確認は判断 D11 の範囲（passthrough なし）。QEMU と実機の証拠を分ける。

## 衝突・危険・rollback

- 衝突: 他の全ての WS の試験の script に触れる。Q1 が投入の時期を決める。 順は Q1 が決める（design.md §7.2）。
- rollback: 前へ直すのを基本にし、後ろに同じ file の変更が無い時だけ統合の commit を revert する（design.md §7.4）。

## Resume

依存の Phase の cleared と main への統合、関係する判断の決定の後に、Q1 が Queue を作る。

## q820（P1、2026-10-06）

- 置き換え: 正規表現 `\bZWL\b(?!_)`（`ZWL_` の識別子は p021 で既に無い）を `KWL` に。printf の `\nZWL` は別に直した。計 約 2770 か所・329 file（`plan/ws131/tools/rename-map.py` の識別子の正規表現 `(?:zwl|ZWL)_` は巻き込んだので戻した）。
- userland（60 file）: compositor（`userland/desktop/wayland/`）、libkeiland-backend の zedBSD・Linux の log（`IMPORT`・`SEAT` など）、sessiond の greeter が読む `KWL EXIT`。
- 試験と道具（270 file: sh・py・c と `plan/tools/aat/aat`）。変えない物: `plan/history`、Markdown の文書（BUG の ticket・phase.md の引用）、`evidence`・`handover`・`import` の下の記録と古い script、log・txt・json の証拠、held の patch。shell の補助関数 `zwl_app_client(s)`・`ZWL_RUN`（`plan/tools/guest/zwl-clients.sh`）は名前のまま（読む文字列は `KWL CLIENT`）。

| 場所 | file の数 |
| --- | --- |
| `plan/tools/aat` | 8 |
| `plan/tools/compositor` | 1 |
| `plan/tools/files` | 18 |
| `plan/tools/gpu-boundary` | 2 |
| `plan/tools/guest` | 1 |
| `plan/tools/imageview` | 2 |
| `plan/tools/keiland-freebsd` | 2 |
| `plan/tools/settings` | 1 |
| `plan/tools/showcase` | 1 |
| `plan/tools/titlebar` | 9 |
| `plan/tools/wallpaper` | 2 |
| `plan/tools/x11` | 2 |
| `plan/ws005` | 3 |
| `plan/ws014` | 1 |
| `plan/ws031` | 3 |
| `plan/ws035` | 55 |
| `plan/ws068` | 17 |
| `plan/ws073` | 2 |
| `plan/ws074` | 13 |
| `plan/ws075` | 2 |
| `plan/ws079` | 9 |
| `plan/ws081` | 10 |
| `plan/ws089` | 16 |
| `plan/ws090` | 5 |
| `plan/ws094` | 2 |
| `plan/ws095` | 7 |
| `plan/ws099` | 28 |
| `plan/ws100` | 5 |
| `plan/ws102` | 7 |
| `plan/ws114` | 1 |
| `plan/ws127` | 6 |
| `plan/ws128` | 3 |
| `plan/ws129` | 1 |
| `plan/ws131` | 2 |
| `plan/ws132` | 2 |
| `plan/ws134` | 5 |
| `plan/ws139` | 2 |
| `plan/ws142` | 5 |
| `plan/ws154` | 2 |
| `plan/ws158` | 1 |
| `plan/ws159` | 2 |
| `plan/ws160` | 1 |
| `plan/ws166` | 1 |
| `plan/ws172` | 1 |
| `plan/ws173` | 1 |

- 確認: zedBSD amd64 の wayland・sessiond（exit 0、warning 0、binary の `ZWL ` の文字列 0、`KWL ` 597）、`make keiland-linux` の gcc と clang（exit 0、warning・error 0）、`keiland-os-boundary/check.sh` PASS、AAT の host（aat-host PASS）、run-host-role・host-layout・host-keyboard・host-network-info。直した sh は `sh -n`、py は `py_compile` で全て通る。
- 未実施: FreeBSD（環境なし）、QEMU（T1）: C1・C2・C9 と各 WS の代表の試験（`zdesktop-p101.sh`・`settings-regress.sh`・`volume-p005.sh`・`textinput-p013.sh`・`demo-s8-s9.sh`）、AAT。
