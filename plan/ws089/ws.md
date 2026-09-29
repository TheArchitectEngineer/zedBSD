<!-- awesome-plan project=zedbsd record=ws089 -->

# WS089: 設定のアプリ（Settings）

<!-- awesome-plan-current:start -->
Status: incomplete
Primary Milestone: MG006
Related Milestones: MG002
Objectives: O2
Parent: [Master](../master.md)
Queue: なし
Resume point: **2026-09-29（2 人目のサブエージェント）**: p001〜p005・p007・p008・**p009** は cleared（p009 は Venus の guest で PASS、デモの script は `make -n` で確認、commit 済み）。次は最後の **p006**（規約の全文との照合、回帰、デモの通し）。proposed の状態: desktop-preferences は適用済み（p007）、audio は適用しない、system は未適用。試験の手順は各 phase.md と `plan/ws089/tests/`（`build-settings-image.sh`・`settings-guest.sh`・`settings-wait.sh`・`settings-p002.sh`〜`p005.sh`・`settings-p007.sh`〜`p009.sh`・`host-build.sh`・`host-preferences.sh`）。壁紙の生成は `userland/desktop/wallpapers/generate.py`
<!-- awesome-plan-current:end -->

## 目標（2026-09-29 ユーザー）

「これもOSCデモで使う優先事項にしたいですが、設定画面のアプリを作ってほしいです。添付がイメージです。あくまでもイメージなので、この通りでなくていいです。
左側に項目のペイン、右側に設定項目。フローティングでセパレート。」

- 見本: [mockup-network.webp](mockup-network.webp)（Network の頁: 左に項目の一覧（Wi-Fi・Ethernet・Bluetooth・VPN・Network・Appearance・Wallpaper・
  Notifications・Sound・Display・Storage・Battery・Keyboard・Mouse・Touchpad・Printers・Sharing・Users・Privacy・Security・Accessibility・
  Updates・About）、上に戻る・進む・Home・breadcrumb・検索・表示の切り替え、右に card の群（接続の状態、Wi-Fi の一覧、Ethernet、VPN、
  DNS と Proxy、通信量の graph、初期化の button））。見本の通りでなくてよい。
- 形: 左の項目の pane と右の設定の pane を、それぞれ浮いた（floating）すりガラスの pane として分ける（Files（WS071）の付箋の pane と同じ作り）。
  窓は Keiland の浮いたタイトルバー（System Menu・CONTROLS）。名前の規則: 画面の文字は「Kei」、Keiland・libkeiland は出さない。
- デモの受け入れ（案、p001 で確定）: 起動して項目を選ぶと頁が替わり、少なくとも About（OS の名前・版・機械）、Network（networkd の実際の状態:
  interface・address・Wi-Fi の一覧と接続）、Display（解像度・拡大）、Appearance・Wallpaper（見た目と壁紙の変更が desktop に反映）、Sound（音量、
  audiod）、Mouse・Touchpad（速さ、慣性の scroll の on/off）が実際に働く。それ以外の項目は頁の枠と「準備中」の表示でよい。App Home から起動できる。
- **受け入れ（2026-09-29 確定）**: ユーザーの回答「設定項目は、ネットワークを中心にしてください。ディスプレイはまだスタブでいいです。」と
  main の判断による。
  1. 起動して項目を選ぶと頁が替わる（左の項目の pane と右の頁の pane、戻る・進む・breadcrumb）。App Home から起動できる。
  2. **Network が中心**: 接続の状態、Wi-Fi の一覧と接続・切断（**新しい network への鍵の入力を含む**）、Wi-Fi の入り切り、Ethernet、
     address・netmask・MAC・DNS、通信量（見本の Network の頁に近い内容）。networkd の protocol に最小の追加が要るなら
     [proposed/](proposed/) に案を置いてから実装する。
  3. About（OS の名前・版・機械）。
  4. Display は stub（読むだけの情報、または準備中）。accent の色と Touchpad は出さない（準備中）。
  5. Appearance・Wallpaper・Sound・Mouse・Keyboard は Network の後の優先度（p004・p005・p007）。デモの受け入れの必須は 1〜4。
  6. その他の項目は頁の枠と「準備中」。

## Phase

設計は [design.md](design.md)。他の WS の source への変更の案は [proposed/](proposed/)（main の許可が要る）。

| Phase | 目的 | Status | 依存 |
| --- | --- | --- | --- |
| [ws089-p001](phase001/phase.md) | 設計: 項目の一覧とデモで働かせる範囲、画面の構成（Files の pane・card の部品の再利用）、各項目の backend（networkd・audiod・sessiond・/dev/system・compositor の設定）との接続の方法、設定の保存先 | cleared（2026-09-29） | — |
| [ws089-p002](phase002/phase.md) | アプリの骨格: 窓、present、glass の 2 枚の card、titlebar（Back・Forward・Home・Breadcrumb・Sidebar）と履歴、System Menu、左の項目の pane、右の頁の scroll、Home（tile）・About・準備中の頁、App Home の行 | cleared（2026-09-29、Venus の guest） | p001、link の規則（main の許可済み） |
| [ws089-p008](phase008/phase.md) | 検索（titlebar の欄と結果の頁）と Home の tile の今の状態。App Home の歯車の絵（許可済み D4）も同時に | cleared（2026-09-29、Venus の guest。実機は未実施） | p002 |
| [ws089-p007](phase007/phase.md) | desktop の設定の仕組み: libkeiland の `keiland_preferences_*`（KEILAND_VERSION 13）と zdesktop の反映（[案](proposed/desktop-preferences.md)、許可済み D4） | cleared（2026-09-29、Venus の guest。実機は未実施） | p001、main の許可 |
| [ws089-p003](phase003/phase.md) | Network・Wi-Fi・Ethernet の頁（networkd の状態、Wi-Fi の一覧・接続・切断・新しい network の鍵、address・DNS・通信量。[案](proposed/libkeiland-network-link.md)、networkd の protocol は変えない） | cleared（2026-09-29、Venus の guest。実機は未実施） | p002 |
| [ws089-p004](phase004/phase.md) | Appearance・Wallpaper・Display・Storage の頁 | cleared（2026-09-29、Venus の guest。実機は未実施） | p002, p007 |
| [ws089-p005](phase005/phase.md) | Mouse・Keyboard の頁と Sound（audiod の有無の表示だけ、main の依頼）。Touchpad は準備中のまま | cleared（2026-09-29、Venus の guest。実機は未実施） | p002, p007 |
| [ws089-p009](phase009/phase.md) | 生成の壁紙（5 枚、1920x1080、`userland/desktop/wallpapers/generate.py`）を build の時に作り、デモの image に同梱（2026-09-29 ユーザーの D8 の判断、main の許可） | cleared（2026-09-29、Venus の guest。デモの image は `make -n`。実機は未実施） | p004 |
| ws089-p006 | 規約の全文との照合、回帰、デモの通し、App Home の絵とデモの image（[案](proposed/app-home-icon.md)、main の許可） | planning | p003〜p005, p008 |

## ユーザーの判断（2026-09-29 に master から移した）

| 項目 | 決定 | 記録先 |
| --- | --- | --- |
| 設定のアプリ（2026-09-29） | ユーザー:「これもOSCデモで使う優先事項にしたいですが、設定画面のアプリを作ってほしいです。添付がイメージです。あくまでもイメージなので、この通りでなくていいです。左側に項目のペイン、右側に設定項目。フローティングでセパレート。」 | [WS089](ws089/ws.md) |
| 設定のアプリの範囲（2026-09-29 夜） | ws089-p001 の問い（Display の拡大は表示だけ・accent の色は出さない・Touchpad は準備中）にユーザー:「設定項目は、ネットワークを中心にしてください。ディスプレイはまだスタブでいいです。」→ Network（Wi-Fi・Ethernet・状態・新しい Wi-Fi への鍵の入力を含む）を中心に作り込む。Display はスタブ、accent と Touchpad は出さない（準備中） | WS089 |
