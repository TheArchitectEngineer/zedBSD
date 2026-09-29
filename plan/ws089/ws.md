<!-- awesome-plan project=zedbsd record=ws089 -->

# WS089: 設定のアプリ（Settings）

<!-- awesome-plan-current:start -->
Status: incomplete
Primary Milestone: MG006
Related Milestones: MG002
Objectives: O2
Parent: [Master](../master.md)
Queue: なし
Resume point: p002（骨格と About）から。p001 は cleared（[design.md](design.md)、main の判断は design.md §9 と [proposed/](proposed/)）。OSC のデモ（fg010）の優先事項
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
- **受け入れ（p001 で確定、2026-09-29）**: [design.md](design.md) §2 の表。案からの差: Display は読むだけ（拡大の変更は D1）、
  Appearance は窓の透明度（accent の色は D2）、Storage（読むだけ）と Keyboard（repeat）を足す。Appearance・Wallpaper・Mouse・Touchpad・
  Keyboard の desktop への反映は p007（main の許可）が要る。

## Phase

設計は [design.md](design.md)。他の WS の source への変更の案は [proposed/](proposed/)（main の許可が要る）。

| Phase | 目的 | Status | 依存 |
| --- | --- | --- | --- |
| [ws089-p001](phase001/phase.md) | 設計: 項目の一覧とデモで働かせる範囲、画面の構成（Files の pane・card の部品の再利用）、各項目の backend（networkd・audiod・sessiond・/dev/system・compositor の設定）との接続の方法、設定の保存先 | cleared（2026-09-29） | — |
| ws089-p002 | アプリの骨格: 窓、左の項目の pane、右の頁、戻る・進む・breadcrumb・検索、Home・About・準備中の頁、App Home の行 | planned | p001 |
| ws089-p007 | desktop の設定の仕組み: libkeiland の `keiland_preferences_*` と zdesktop の反映（[案](proposed/desktop-preferences.md)、main の許可が要る） | planning | p001、main の許可 |
| ws089-p003 | Network・Wi-Fi・Ethernet の頁（networkd の状態と Wi-Fi の接続、link の詳細は [案](proposed/libkeiland-network-link.md)） | planning | p002（link の詳細は main の許可） |
| ws089-p004 | Appearance・Wallpaper・Display・Storage の頁 | planning | p002, p007 |
| ws089-p005 | Sound・Mouse・Touchpad・Keyboard の頁（音量は [案](proposed/libkeiland-audio.md)） | planning | p002, p007（音量は main の許可） |
| ws089-p006 | 規約の全文との照合、回帰、デモの通し、App Home の絵（[案](proposed/app-home-icon.md)） | planning | p003〜p005 |
