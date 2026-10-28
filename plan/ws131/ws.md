<!-- awesome-plan project=zedbsd record=ws131 -->

# WS131: libkeiland-backend の分離と libkeiui の吸収

<!-- awesome-plan-current:start -->
Status: completed（2026-10-27 P3 の精査 q1608。p001・p003〜p023・p025〜p027 cleared（libkeiland-backend の分離、libkeiui の吸収、kl_/kwl_ の改名、app の移行）。p002 の設計の D7 は後続の Phase の実施で決着）
<!-- retained-after-completion --> tests/ の 9 項目 はその場に残す: src/・userland/・platform/・Makefile・config/ の source か build の file が path を名指す（AGENTS.md §10 の source の comment の掃除の後に消す。2026-10-28 Q1 の方針、q1660）
Completed: 2026-10-27（q1608）
Primary Milestone: MG006
Related Milestones: MG007（Linux・FreeBSD の Keiland）
Objectives: O2, O3
Parent: [Master](../master.md)
Queue: なし
Resume point: なし（新しい要求は新しい WS として立てる。この WS は再開しない）
<!-- awesome-plan-current:end -->

## 目標

2026-10-03 ユーザー「各アプリのウィンドウの作成を含めたGUI構築を共通化して、libkeilandにまとめることで、libkeilandをGUIツールキット、兼、デスクトップ機能の抽象化レイヤー、にできそうか、検討してほしいです。できそうならアプリはlibkeilandをツールキットにする構成に変更していきます。これらを1つのWSにしてください。」

決まった構成: libkeiland-backend（compositor だけが使う OS の抽象化層、OS ごとに `libkeiland-backend-zedbsd`・`-linux`・`-freebsd` の別の source tree）、libkeiland（標準 app の UI toolkit と compositor の非標準の機能の wrapper、libkeiui を吸収）、compositor（OS に依存しない共通の source）。検討は [study.md](study.md)、移行計画は [design.md](design.md)（第 2 版）、改名の表は [rename-map.md](rename-map.md)・[rename-map-kwl.md](rename-map-kwl.md)。

## ユーザーの決定（2026-10-03）

- 構成「現状では、libkeilandは、コンポジタが使うOS抽象化機能と、クライアントが使うUIの共通化機能が、混在してしまっていますね。libkeiland-backend / libkeiland の2つに分解しましょう。libkeiland-backendはコンポジタが使うOS抽象化レイヤーの実装です。libkeilandは標準アプリが使うUIツールキットと、我々のコンポジタが持つ非標準機能をラップしたものです。電源管理、WiFi、ネットワーク、音声、PnPはlibkeiland-backendに実装して、各OSごとに、libkeiland-backend-zedbsd, libkeiland-backend-linux, libkeiland-backend-freebsdのような実装をソースツリーを分けて持ちましょう。」
- 「Settingsの設定変更は、libkeilandを通じてコンポジタの拡張で通信して設定され、コンポジタが記録を行うように修正してください。」
- 確認 1「新しい構成では、OS の抽象化は compositor 側の backend だけが持つことになります。そうすると、Settings からの操作は次の経路になります。理解は正しいです。Settings → libkeiland → compositor の拡張のプロトコル → compositor → libkeiland-backend → networkd や audiod」
- 確認 2「compositor のソースの中に、OS ごとの画面・入力の処理があります（wayland/linux/ の seat・logind・evdev、gpu-zedbsd.c など）。これも libkeiland-backend の OS ごとのツリーに移します。こうすることで、コンポジタの設計がきれいになりますね。」
- 確認 3「libkeiui は、libkeiland（UI ツールキット）に吸収します。名前は、keiland_をプレフィックスとする関数名のAPI群にします。WSに移行計画を立ててください。」
- D2 と進め方「はい、GPUバッファ管理もbackendに移したいです。ただし、backend化は一気に実装せず、1つずつ移行することで、正確に、確実に作業した方がいいと思います。」
- 名前の規則「KUI_* については、KL_というプレフィックスに、最終的には変更しましょう。段階的な移行でいいです。Keiland関連はすべて、KL_に統一します。keiland-ui.hも、最終的には、keiland.hにまとめます。これも段階的な移行でよいです。マクロでないシンボル名は、kui_はkeiland_にマップします。」
- 名前の規則の更新（最新）「シンボル名のkeiland_も、kl_にまとめる方針に変更します。…enumもKL_にします。KEILAND_BINDIR,KEILAND_LIBEXECDIRはそのままでOKです。ZWL_, zwl_はKWL_, kwl_がいいです。変更しましょう。」
- ABI「まだベータ版ですらないので、ABIは変更して問題ないです。ABIバージョンも上げなくていいです。」
- D14 の残り「X server が、Wayland の規格のキーコードの定数を自分で持つようにしましょう。」「xattrは情報のパネルを名前だけを出す簡単な表示にします。」
- p002 第 2 版のレビュー「D4ですが、compositorはタッチIMEを実装しますので、そのUIの構築のためにlibkeilandをリンクしてOKです。不要ならリンクしなくてもOKです。」「D7は、browserのshellの窓が何のことなのかわかりません。」「D9は、この移行が完了してからでOKです。」「その他は承認します。」
- D7「D7,browserのshellもlibkeilandで書きましょう。」→ p025。
- 時期「backend への移行はP1が今やりましょう。P2はシステムモニタを作っていますが、これは衝突しないように思えます。」
- FreeBSD「FreeBSDでのテストはあとまわしにします。ホストがメモリを使い尽くしているためです。…一度適切なタイミングでホストの再起動が必要な見込みです。」「FreeBSDでのビルド確認は後回しで、書くだけにします。Linuxでのビルドはネイティブで行いましょう。再起動は、P4がLCDの問題を解決し、P2がTerminal改善を終了し、P1も作業を終了したあとにします。再起動後にP3を実行しましょう。」
- 2026-10-06（クリックの回答）「sessiond に口を足す（別の Phase）」→ p027。
- 2026-10-04: 公開の header を `userland/desktop/include/` に（p026）。
- 2026-10-08「規約の整形（各 WS の全文規約の Phase）はベータ3」→ p024 は WS の外の一括の見直しへ。

## 結果

- libkeiland-backend を 1 領域ずつ分離した: network（p003）→ 音声（p004）→ 電源（p005）→ seat・session（p006）→ 入力（p007）→ 表示（p008）→ GPU の buffer と境界の確定（p009）。
- 拡張の protocol `kl_system_manager_v1` と設定の記録（p010）、Settings を拡張へ移し libkeiland の OS の依存を 0 に（p011）。設定（preferences）の部分は WS135 が引き取った。
- libkeiui を libkeiland へ移し（p012）、名前を `kl_`・`KL_`（p013・p014）、compositor の内部を `kwl_`・`KWL_`（p021）、log の接頭辞を `KWL `（p022）に。
- app の骨組みの API `kl_app`（p015）と app の移行: Text Editor（p016）、PDF Viewer・Image Viewer（p017）、Terminal・Notes（p018）、Settings（p019）、Files（p020）、browser の shell の窓（p025）。互換の除去と `keiland.h` の一本化・PnP の接続（p023）、公開の header の `userland/desktop/include/`（p026）。
- 証拠は QEMU の T1・T2（T1-040・T1-042・T1-044・T1-054・T1-059〜T1-062・T1-116・T1-311・T2-009・T2-013・T2-019〜T2-027 ほか）と host の試験。FreeBSD の build と audit は T1-116 で PASS。

## 制限・移管

- p002 の設計の Phase は in-progress の印のまま閉じた（D7 は p025 の実施で決着）。
- 全文規約と 3 OS の回帰（p024）は WS の外の一括の見直しへ。
- WS090 の描画の層・scroll・規約は WS131 の完了の後に `kl_` の名前で扱う（D9）。
- host 試験（`tests/`）は `plan/tools/keiland-freebsd/backend-test.sh`・`plan/tools/keiland-machine/` から使われる。plan/tools/ へ移す案（Q1 の判断）。

## Phase 一覧

| Phase | 目的 | 結果 |
| --- | --- | --- |
| ws131-p001 | 実現性の検討（study.md） | cleared |
| ws131-p002 | 移行計画（design.md 第 2 版） | in-progress のまま WS を閉じた（2026-10-03 ユーザーのレビュー済み、D7 は p025 で決着） |
| ws131-p003 | backend の土台と network | cleared（2026-10-03 Q1） |
| ws131-p004 | backend の音声 | cleared（q650、T2-009） |
| ws131-p005 | backend の電源 | cleared（q650、T1-040） |
| ws131-p006 | backend の seat・session | cleared（q650、T2-007・T2-011・T1-042） |
| ws131-p007 | backend の入力 | cleared（q650、T1-044・T1-059） |
| ws131-p008 | backend の表示 | cleared（q650、T2-013・T1-054） |
| ws131-p009 | backend の GPU の buffer と境界の確定 | cleared（q659、T2-019・T1-060） |
| ws131-p010 | 拡張の protocol `kl_system_manager_v1` | cleared（q659、T2-020・T1-061・T1-062） |
| ws131-p011 | Settings を拡張へ、libkeiland の OS を 0 に | cleared（q659） |
| ws131-p012 | libkeiui を libkeiland へ | cleared（q673、T2-027） |
| ws131-p013 | 旧 libkeiui の名前を `kl_`・`KL_` に | cleared（2026-10-08 Q1 の判定） |
| ws131-p014 | 旧 libkeiland の名前を `kl_`・`KL_` に | cleared（2026-10-05、T1-116） |
| ws131-p015 | app の骨組みの API `kl_app` | cleared（2026-10-05 Q1） |
| ws131-p016 | Text Editor の移行 | cleared（2026-10-07） |
| ws131-p017 | PDF Viewer・Image Viewer の移行 | cleared（2026-10-07） |
| ws131-p018 | Terminal・Notes の移行 | cleared（2026-10-07） |
| ws131-p019 | Settings の窓の移行 | cleared（2026-10-07） |
| ws131-p020 | Files の移行 | cleared（2026-10-07） |
| ws131-p021 | compositor の内部の名前を `kwl_`・`KWL_` に | cleared（2026-10-07） |
| ws131-p022 | compositor の log の接頭辞を `KWL ` に | cleared（2026-10-07） |
| ws131-p023 | 互換の除去・`keiland.h` の一本化・PnP の接続 | cleared（2026-10-07） |
| ws131-p024 | 全文規約と 3 OS の回帰 | planning のまま WS を閉じた（WS の外の一括の見直しへ） |
| ws131-p025 | browser の shell の窓 | cleared（2026-10-07、T1-311） |
| ws131-p026 | 公開の header を `userland/desktop/include/` に | cleared（2026-10-07） |
| ws131-p027 | sessiond が session の socket で Power Off・Restart を受ける | cleared（2026-10-06 Q1、T1-227b。Restart は未実施） |

## 記録の所在

Phase の計画・結果・試験（`phaseNNN/`・`tests/`）は削除する（Q1 の pipeline）。git の commit `fb1f6afee` の `plan/ws131/` にある。Queue の履歴は [plan/history](../history/index.md)。
