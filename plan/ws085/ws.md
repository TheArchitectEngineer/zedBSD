<!-- awesome-plan project=zedbsd record=ws085 -->

# WS085: Windows版QEMUのVenusでデスクトップを表示する

Status: incomplete
Primary Milestone: MG006
Related Milestones: MG001
Parent: [Master](../master.md)
Queue: [q499](../queue.md)
Resume point: p001。Windows QEMUのmapped blob scanoutでデスクトップを確認済み。SDL→仮想USB HIDの10指タッチを追加し、QMPの2指注入でメニュー起動を確認。Files起動停止の原因だったWindowsの共有画像通信のpadding・fd所有権を修正し、Files開閉・再起動とTerminal同時起動を確認。物理タッチ入力と所有者切替の確認待ち。

## 目標と境界

ユーザーの2026-09-29指示「Windowsで動くように修正してみてください」と、その後の`vendor/`へのforkソース導入・直接修正許可に基づき、Windows版WINQ-EMU Alpha10で既定のamd64 imageのデスクトップを表示し、copy表示の速度問題を調べ、Windows SDLの指入力で人がタッチUIをデバッグできるようにする。Linux hostの実証済みVenus経路を維持する。HALとtoolchainは変更しない。vendorの変更はユーザーがレビューしてcommit/pushする。

## main への取り込み（2026-09-29）

ユーザー:「GitHubのorigin/venus-win32ブランチに、QEMUのWindows版でVenusを利用できるようにした特殊ビルドにおいてKeiが動作するようにしたパッチがあります。
これを取り込んで、WindowsのVenusも利用でき、かつ、我々のテスト環境のLinuxでのVenusも利用できるように、していきます。ちなみにWindowsの方がVenus 1.4, LinuxがVenus 1.3でした。」
→ **目標に追加: Windows（WINQ-EMU、Venus 1.4）と Linux（テスト環境の QEMU、Venus 1.3）の両方で同じ image が動く。**
- `origin/venus-win32`（94908af8）を main に merge。条件（ユーザー）: submodule は不要（`.gitmodules` の `vendor/winq-emu-*` の 2 つを外した）、font は license に
  問題が無ければ git に入れる（Inter・JetBrains Mono は SIL OFL 1.1、Droid Sans Fallback は Apache 2.0。Droid の license の file は Apache の本文を Debian の
  `/usr/share/common-licenses` に頼るので、本文 `userland/desktop/fonts/Apache-2.0.txt` を足して image の `/usr/share/licenses/keiland-fonts/` に入れる）。
  font は wayland の package が入れるので、`plan/ws075/demo/build-demo-image.sh` の `build/ws035-fonts/` の注入を外した。
- 取り込みの後の確認: Linux の Venus（1.3）の回帰（desktop の image の起動と guest の試験）。Windows（1.4）はユーザー。
- 後続: [WS088](../ws088/ws.md)（Kei-nightly.zip の CI 配布）。

## Phase

| Phase | 目的 | Status | 依存 |
| --- | --- | --- | --- |
| [ws085-p001](phase001/phase.md) | paired Windows rendererの同期契約、copy fallback、mapped blob scanout、SDLマルチタッチ入力を検証 | in-progress | — |

### 取り込みの後の確認（2026-09-29 main）

- merge commit dac0ed2b（main dc339f71 + origin/venus-win32 94908af8）。衝突は plan/master.md（WS の一覧）と plan/known-bugs.md（BUG-100〜103 を全部残す）。
- Venus の変更の読み: kernel（`venus/transport.c`・`share.c`）と libvulkan（`context.c` 等）は、Windows の renderer が名乗る vendor の flag 15（host scanout）と
  protocol の版 1.4.343 を受け入れる分岐の追加で、Linux の renderer（flag 7、1.3.269）の経路は変わらない。
- build の不具合: branch の BUG-100 の Remacs の patch の規則は `.nb`・`.nbc` の 2 つの書き方だけを想定し、最新の Remacs（1a72439、`noct --compile --app` で
  `remacs.nap` を直接作る）で「unexpected bytecode output rule」で止まった。`userland/packages/editors/remacs/Makefile` にその形を足した（patch は不要）。
- Linux の Venus（1.3）: `make ZEDBSD_CONFIG=config/ci/config-amd64.mk BUILD=build/ws085-ci disk-image`（CI と同じ構成、warning 0）を
  `plan/ws035/tests/zdesktop-guest.sh` で起動し、desktop（壁紙・システムバー・時計の文字）が出た（`build/ws085-ci/desktop.png`）。アプリの起動の試験は未実施。
- Windows（1.4）: 未実施（ユーザー）。
