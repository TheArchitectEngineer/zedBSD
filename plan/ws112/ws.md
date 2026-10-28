<!-- awesome-plan project=zedbsd record=ws112 -->

# WS112: Debian の package（Debian 13・Ubuntu 26.04 共通の amd64・arm64 と Raspberry Pi OS の arm64）

<!-- awesome-plan-current:start -->
Status: completed（2026-10-27 P3 の精査 q1608。2026-10-06 夜 ユーザーの新しいゴール（3 つの deb）を p002・p003 で生成（2026-10-07 Q1 の判定で cleared）。Fedora・Arch・CI の p004〜p006 は canceled）
Completed: 2026-10-27（q1608）
Primary Milestone: MG007
Related Milestones: MG001（追跡可能な build・配布の記録）、MG006（既存の Keiland の配布）
Parent: [Master](../master.md)
Queue: なし
Resume point: なし（新しい要求は新しい WS として立てる。この WS は再開しない）
<!-- awesome-plan-current:end -->

## 目標

2026-10-06 夜 ユーザー「WS112は、Debianパッケージのみにします。Debian 13, Ubuntu 26.04の両方に互換があるamd64/arm64のdebパッケージと、Raspberry Pi OSのarm64のdebパッケージ、この2つのファイルが生成できるようになることを、ゴールに設定し直します。」

次の 3 つの deb の file を生成できる: (1) Debian 13・Ubuntu 26.04 の両方に互換の amd64 の deb、(2) 同じく arm64 の deb、(3) Raspberry Pi OS の arm64 の deb。設計は [design.md](design.md)、環境は [native-environments.md](native-environments.md)、形式・依存・manifest は [package-contract.md](package-contract.md)、package の方針は [standards/ws112-linux-packages.md](../standards/ws112-linux-packages.md)。

## ユーザーの決定

- 2026-10-02: 最初の計画は Debian 13・Ubuntu 26.04・Raspberry Pi OS・Fedora 44・Arch Linux の 5 種類の package と CI。Raspberry Pi OS は「64 bit（arm64、推奨）」、build・deb の生成だけで受け入れ（GPU・GUI・表示の確認は不要）。FreeBSD は source から build・install、package は作らない。CI で導入・GUI の確認は不要。
- 2026-10-06 夜: 上の目標の設定し直し。Q1 のクリックの質問への回答「3 つ（amd64・arm64・RPi）」「第 2 段（ベータ2）」。Fedora・Arch・rpm・pacman の package は取りやめ。2026-10-05 の「ベータ4 以降」を置き換え。
- 2026-10-07 D-a（build の環境）: mmdebstrap の rootfs。対象の rootfs（Debian 13、RPi OS trixie）の中で native に build（arm64 は host の qemu-aarch64 の binfmt、user-mode）。
- 2026-10-07 D-b（確かめる範囲）: 生成＋形式・依存の解決。runtime・GUI の試験は無し。
- 2026-10-08「規約の整形（各 WS の全文規約の Phase）はベータ3」→ p007 は WS の外の一括の見直しへ。

## 結果

- p002: amd64・arm64 の共通の deb を mmdebstrap の rootfs で build し、Debian 13 と Ubuntu 26.04 の rootfs で `apt-get install --simulate` が exit 0、ELF の machine・manifest を確認（2026-10-07 Q1 の判定、host）。
- p003: RPi OS の arm64 の deb を RPi OS の rootfs（+rpt の package 19）で build、`--simulate` が exit 0（2026-10-07 Q1 の判定、host）。
- 実機・QEMU での導入と GUI の確認は D-b により範囲の外（未実施）。

## 制限・移管

- Fedora・Arch・CI の release 添付（p004〜p006）は取りやめ（2026-10-06 夜 ユーザー）。CI は Future Work。
- 規約の全文と WS の受け入れの照合（p007）は WS の外の一括の見直しへ。

## Phase 一覧

| Phase | 目的 | 結果 |
| --- | --- | --- |
| ws112-p001 | 5 種類の共通の契約・入力・形式の確定 | uncleared・canceled（2026-10-07、ゴールの設定し直しで superseded） |
| ws112-p002 | amd64・arm64 の共通の deb | cleared（2026-10-07 Q1 の判定） |
| ws112-p003 | Raspberry Pi OS の arm64 の deb | cleared（2026-10-07 Q1 の判定） |
| ws112-p004 | Fedora 44 の rpm | canceled（2026-10-07） |
| ws112-p005 | Arch Linux の package | canceled（2026-10-07） |
| ws112-p006 | CI の 5 target と release の添付 | canceled（2026-10-07） |
| ws112-p007 | 最終の全文規約と WS の受け入れ | planned のまま WS を閉じた（WS の外の一括の見直しへ） |

## 記録の所在

Phase の計画・結果・調査（`phaseNNN/`、5 OS の入力の URL・hash の調査表 `phase001/survey.md` を含む）は削除する（Q1 の pipeline）。git の commit `fb1f6afee` の `plan/ws112/` にある。Queue の履歴は [plan/history](../history/index.md)。
