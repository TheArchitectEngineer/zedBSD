<!-- awesome-plan project=zedbsd record=ws131-p023 -->

# ws131-p023: 互換の除去・header の一本化・PnP の接続

Status: in-progress（2026-10-06 q820、P1。Q1 の指示で開始。host の確認まで済み、keiui.h・keiland-ui.h の削除（git rm）を Q1 に、QEMU と FreeBSD の native build を T1 に依頼）
Disposition: normal
Parent: [WS131](../ws.md)、計画の正本 [design.md](../design.md)
Queue: q820（P1）
依存: p016〜p020・p025・p022 cleared。PnP の部分は WS132 の kernel の通知が main にある時だけ
目安: 3〜4h（1 Queue）。実行者: Q1 が割り当てる（high）
所有 path: keiland-ime・kuidemo・xserver・probe・その他の残りの改名、`userland/desktop/keiland/`（`keiui.h` の削除、`keiland-ui.h` の `keiland.h` への統合、互換の block の除去）、backend の device 領域、`wayland/system.c` の devices、`libkeiland/system/`、`plan/ws131/`。（Q1 の委任が要る: Q1 が Phase の前に委任を記録する）: `keiui.h` を使う host の試験 13 file（design.md §2.4）、`plan/tools/keiland-os-boundary/`（B5）

## 目的と結果

旧名（`kui_`・`KUI_`・`keiland_`・`KEILAND_`（paths.h を除く））の使用を 0 にし、互換の `keiui.h` と互換の block を除き、`keiland-ui.h` を `keiland.h` に統合して公開の header を一つにする（2026-10-03 user）。WS132 の成果があれば PnP を backend と拡張に接続する。

## 範囲

1. 残りの利用者（keiland-ime 27 の名前ほか、grep で数える）と host の試験 13 file（review 19）を新名と `<keiland.h>` へ。
2. `keiui.h`・`keiland-ui.h` を除き、`keiland.h` に統合。FreeBSD の header の表を更新。checker B5 を FAIL の条件に。
3. PnP（WS132 があれば）: backend の device 領域、`kl_system_devices_v1`、`kl_system_devices_*`。無ければ残件として WS132 へ。

## 受け入れ

- 3 OS の build（zedBSD の amd64 は exit 0・自前の warning 0、`make keiland-linux` の gcc と clang、FreeBSD の native build と `native-build-audit.py`）と `plan/tools/keiland-os-boundary/check.sh` PASS。
- `grep -rE "\bkui_|\bKUI_|keiui\.h|keiland-ui\.h"` と旧 `keiland_`・`KEILAND_`（paths.h と D16 の対象外を除く）が userland と plan の試験で 0。B5 PASS。
- 全 app の起動（zedBSD と Linux の PNG）、§2.4 の host の試験、boot-test、C1・C2・C9。PnP を接続した時は WS132 の試験。

## 検証の方法

[zedbsd-commands.md](../../tools/keiland-linux/zedbsd-commands.md)、[keiland-linux/README.md](../../tools/keiland-linux/README.md)、[keiland-freebsd/README.md](../../tools/keiland-freebsd/README.md)。QEMU の console・serial の log で判定しない（不具合は gdbstub・monitor・QMP）。build は自分の `BUILD=build/ws131-pNNN/`、共有の `build/` を消さない。FreeBSD の起動の確認は判断 D11 の範囲（passthrough なし）。QEMU と実機の証拠を分ける。

## 衝突・危険・rollback

- 衝突: keiland-ime は WS095・WS102 の file。 順は Q1 が決める（design.md §7.2）。
- rollback: 前へ直すのを基本にし、後ろに同じ file の変更が無い時だけ統合の commit を revert する（design.md §7.4）。

## 実行の記録（2026-10-06、P1）

委任: Q1 が「keiui.h を使う host の試験 13 file（§2.4）と `plan/tools/keiland-os-boundary` の B5」の直しを委任した（2026-10-06、rm を足さない・他の WS の試験の意味を変えない）。

### 範囲 1: 旧名を新名に

- 改名の表: `keiland.h` の互換の block（206）と `keiui.h`（341）の `#define 旧 新`、それに共有の source の内部の名前（D16）`KEILAND_MARK_*`→`KL_MARK_*`（artwork/mark.h）・`keiland_color_image`→`kl_color_image`（picture/color-glyph）・`keiland_freebsd_dma_error`→`kl_freebsd_dma_error`（freebsd-compat・libvulkan-compat）。表は `build/ws131-p023/map.txt`（一時）、識別子ごとに置き換え（`\b` 区切り、表に無い名前は触らない）。
- 対象: userland と plan の `.c`・`.h`・`.inc`（plan/history と libbrowser（別の component、WS107 の規約）を除く）。44 file・1,521 か所。monitor（旧 titlebar・gesture）、xserver・titlebar-probe・menu-probe・popup-probe・wlshm（旧 titlebar・menu）、compositor の touch.c（motion）、kuidemo・keiland-ime・videoplayer・Files・Settings・libkeiland の ui、host の試験。
- `#include <keiui.h>`・`<keiland-ui.h>` を `<keiland.h>` に（重なる時は消す）。試験の script の header の複写・symlink から両者を除いた（§2.4 の 13 file を含む 26 本）。
- 古くなっていた試験を直した（改名と関係しない取りこぼし）: `ws014/phase006/tests/wayland-client.c`（p021 の protocol 名 `kl_gpu_buffer_v1`）、`ws035/tests/p107`・`p129`（p014 の `kl_mark_raster`）、`ws081/tests/run-pdftouch.sh`（PDF Viewer の `find.c` と libpdf の Makefile の source の一覧の不足で link できなかった）。
- 残す名前: install の path の macro（paths.h の `KEILAND_BINDIR`・`DATADIR`・`LIBEXECDIR`・`SYSCONFDIR`・`FONT_*`、make の `KEILAND_PREFIX`・`KEILAND_VULKAN_BACKEND_PATHS`）、環境変数（`KEILAND_DRM_DEVICE`・`KEILAND_SEAT`・`KEILAND_DESKTOP_TOKEN`・`KEILAND_VULKAN_BACKEND`・`KEILAND_VULKAN_NO_DEEPBIND`、利用者の設定なので名前を変えない）、include guard（D16、`KEILAND_H` を含む）、WS035 p075 の試験の独自の protocol（`keiland_generic_*_v1`、p021 の決定）、build の make の変数（`KEILAND_LINUX_*`・`KEILAND_FREEBSD_*`）。

### 範囲 2: header を一つに

- `keiland-ui.h` の本文（説明の comment と宣言）を `keiland.h` の終わり（`extern "C"` の中）へ移し、互換の block（`KL_COMPAT`）を除いた。履歴の comment の `KUI_VERSION n` は「libkeiui's version n」に。
- `libkeiland/exports.py` は `keiland.h` だけを読む。`exports.map` の関数の集合は変わらない（差は comment の 1 行）。
- FreeBSD: `keiland-freebsd.mk` の公開の header の表から `keiland-ui.h`・`keiui.h` を除き、古い install の物を消す一覧（RETIRED、make の規則の rm）に足した。`native-build-audit.py` は `keiland.h` があり `keiui.h`・`keiland-ui.h` が無いことを確かめる。Linux は公開の header を install しない。
- checker: B5 を足した（FAIL の条件）。C の source の旧名（上の残す名前を除く）と、`#include`・script の複写の `keiui.h`・`keiland-ui.h`。C5 に `keiland-ui.h` を足した。
- **`userland/desktop/keiland/keiui.h` と `keiland-ui.h` の file の削除は Q1 に依頼**（rm は Q1 の手順、2026-10-06 user）。今は誰も include しないが file が残るので、B5 は keiui.h の行だけで FAIL（削除の後に PASS の見込み）。

### 範囲 3: PnP

WS132 p002〜p005（cleared）が kernel の事象、backend の `events-zedbsd.c`、compositor の devices、`kl_system_devices_*`（get・info・eject・mount・busy_program）をすでに接続している。この Phase で足す物は無い。

### 確認（host、2026-10-06）

| 確認 | 結果 |
| --- | --- |
| `make -j16 disk-image`（zedBSD amd64、`-Werror`） | exit 0、warning 0、image あり |
| image に入らない利用者 `build/amd64/bin/{monitor,keiland-ime,videoplayer,kuidemo,titlebar-probe,menu-probe,popup-probe,wlshm}` | exit 0（41 file を compile）、warning 0 |
| `make -j16 keiland-linux`（gcc） | exit 0、warning 0 |
| `make -j16 keiland-linux CC=clang KEILAND_LINUX_BUILD=build/ws131-p023/linux-clang` | exit 0、warning 0 |
| `keiland-os-boundary/check.sh` | B5 だけ FAIL（`keiui.h` 自身の行、削除待ち）、他は PASS |
| 旧名の grep（B5 と同じ条件、`keiui.h` を除く） | 0 |
| host の試験（直した script と §2.4、28 本） | PASS: files host-build、keiui host-chooser 85/85、textedit host-core 53/53、ws081 browsertouch・filestouch・motion・notestouch・pdftouch（33 checks ×2）・scroll・termtouch、ws089 host-about・host-build・host-slot・host-wired、ws090 host-mahora・host-pad・host-widgets 94/94、ws100 host-audio、ws102 host-inset、ws127 scroll-bar-test、ws128 host-share（script の rm を除いて同じ手順を手で）、ws131 host-system、ws132 host-lid、ws155 calendar、ws169 mailer、ws170 phone |
| 既存の失敗（変更前の tree でも同じ） | ws090 host-input 77/78（`ui axis: the end lets the content fly`）、ws014 phase006 run-wayland-client（include の path が古い、`wayland/wayland-client.h` が無い）。どちらも改名と関係しない |

未実施: FreeBSD の native build と `native-build-audit.py`、全 app の起動（zedBSD と Linux の PNG）、boot-test、C9（T1 に依頼）。

## Resume

依存の Phase の cleared と main への統合、関係する判断の決定の後に、Q1 が Queue を作る。
