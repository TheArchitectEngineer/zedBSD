<!-- awesome-plan project=zedbsd record=ws089 -->

# WS089: 設定のアプリ（Settings）

<!-- awesome-plan-current:start -->
Status: completed（2026-10-27 P3 の精査 q1608。ユーザー「…masterとWSを修正して、completedやcanceledにしてください。」: p001〜p017・p019〜p027 cleared（p011・p014 は 2026-10-13 ユーザー「UATの結果待ちのものは、すべてテスト済みなのでclearedにして進めたりcloseしたりしてください。」）、p015 canceled（WS158）。p018 の規約の全文は WS の外の一括の見直しへ）
Completed: 2026-10-27（q1608 の精査）
Primary Milestone: MG006
Related Milestones: MG002
Objectives: O2
Parent: [Master](../master.md)
Queue: なし
Resume point: なし（新しい要求は新しい WS として立てる。この WS は再開しない）
<!-- awesome-plan-current:end -->

## 目標

設定のアプリ（`userland/desktop/settings/`、画面の名前 Settings）を作る。左の項目の pane と右の設定の pane を浮いたすりガラスの pane として分け（Files（WS071）と同じ作り）、窓は Keiland の浮いたタイトルバー。見本は [mockup-network.webp](mockup-network.webp)、設計は [design.md](design.md)、他の WS への変更の案は [proposed/](proposed/)、手引きは [guide.md](guide.md)、ベータ1 の候補は [beta1-candidates.md](beta1-candidates.md)。

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

## ユーザーの決定

- 2026-09-29: 「これもOSCデモで使う優先事項にしたいですが、設定画面のアプリを作ってほしいです。添付がイメージです。あくまでもイメージなので、この通りでなくていいです。左側に項目のペイン、右側に設定項目。フローティングでセパレート。」
- 2026-09-29 夜（p001 の問いへ）: 「設定項目は、ネットワークを中心にしてください。ディスプレイはまだスタブでいいです。」
- 2026-09-29（D8 の質問への回答、main 経由）: 「生成の壁紙を入れる」（p009）。
- 2026-09-29: 「Settingsはある程度動いたらブラッシュアップは後回しにします。」→ 2026-10-02: 「Settingsも重点的に」で置き換え、ベータ1 の到達目標 S-B1〜S-B6 と p010〜p018 を足した。
- 2026-10-02: 読み取り専用の Display の stub を、外部 display・全拡張・全 mirror・drag の配置ができる [WS113](../ws113/ws.md) で後日実装する。Settings は libkeiland の公開 API だけから compositor の拡張へ接続する。
- 2026-10-04（ユーザーの要望）: Wi-Fi の自動の scan と Disconnect の icon（p021）、Ethernet の設定の取得・変更（p022）、Storage の解析と Trash（p023）、Mouse の頁を device ごと・加速・自然な方向（p024）、Sharing の SSHD の ON/OFF（p025、後でクラウドストレージの設定も）、Users の頁（p026）。
- 2026-10-05 夜: 日本語の UI（p015）は WS158 に吸収。
- 2026-10-08: 「規約の整形（各 WS の全文規約の Phase）はベータ3」（p018 は WS の外の一括の見直しへ）。
- 2026-10-13: 「UATの結果待ちのものは、すべてテスト済みなのでclearedにして進めたりcloseしたりしてください。」
- 2026-10-27（q1608）: 「…masterとWSを修正して、completedやcanceledにしてください。」

## 結果

- 骨格（窓・glass の 2 枚の card・titlebar の履歴と breadcrumb・System Menu・Home の tile・About・準備中の頁、p002）、検索と Home の tile の状態（p008）、desktop の設定の仕組み（libkeiland の `keiland_preferences_*` と zdesktop の反映、p007）。
- 頁: Network・Wi-Fi・Ethernet（p003、Wi-Fi の自動の scan p021、Ethernet の設定の変更 p022）、Appearance・Wallpaper・Display・Storage（p004、Storage の解析と Trash p023）、Mouse・Keyboard・Sound（p005、device ごとの Mouse・Touchpad と加速 p024）、Sharing の SSHD（p025）、Users（p026）、About の版の名前（p027）と memory（p013）。生成の壁紙 5 枚（p009）。
- 操作性: 検索の key 操作と頁の touch の scroll（p012、titlebar の検索欄からの Down・Up は p019）、単一の instance（p016）、accent の色と dark（p017）、Wallpaper の頁の縮小表示を別の thread で（p020、BUG-152）。
- 回帰・通し（p006・p010）。証拠は QEMU（Venus）と host が中心。実機は 2026-10-13 のユーザーの UAT（p011 の 5330 の S7、p014 の 5330 の Wi-Fi）。

## 制限・移管

- Display の頁（複数 display）は WS113 p006。日本語の UI は WS158。
- 2026-09-29 の後回しの候補のうち、音量（Sound の頁は audiod の有無の表示だけ、[proposed/libkeiland-audio.md](proposed/libkeiland-audio.md) は未適用）、通信量の 24 時間の履歴、準備中の頁の一部（Bluetooth・VPN・Notifications・Battery・Printers・Privacy・Security・Accessibility・Updates）は、この WS では実装していない（各頁の今の状態は未確認）。
- BUG-213（Network の表示）・BUG-214（透明度の slider の初期値）は各 ticket で。
- 規約の全文（p018）は WS の外の一括の見直しへ。
- 試験（`tests/`）は Settings の試験の guest と host 試験として `plan/tools/settings/`・`plan/tools/wallpaper/`・`plan/tools/keiland-linux/` が使うので `plan/tools/` へ移す（Q1 の OK の後）。

## Phase 一覧

| Phase | 目的 | 結果 |
| --- | --- | --- |
| ws089-p001 | 設計 | cleared |
| ws089-p002 | アプリの骨格 | cleared（2026-09-29、Venus） |
| ws089-p003 | Network・Wi-Fi・Ethernet の頁 | cleared（2026-09-29、Venus） |
| ws089-p004 | Appearance・Wallpaper・Display・Storage の頁 | cleared（2026-09-29、Venus） |
| ws089-p005 | Mouse・Keyboard の頁と Sound | cleared（2026-09-29、Venus） |
| ws089-p006 | 規約の全文との照合、回帰、デモの通し | cleared（2026-09-29、Venus） |
| ws089-p007 | desktop の設定の仕組み | cleared（2026-09-29、Venus） |
| ws089-p008 | 検索と Home の tile の状態 | cleared（2026-09-29、Venus） |
| ws089-p009 | 生成の壁紙をデモの image に | cleared（2026-09-29、Venus） |
| ws089-p010 | 現在の main での回帰・S7・候補の一覧 | cleared（2026-10-03、q617-i01） |
| ws089-p011 | 5330 の passthrough で S7 | cleared（2026-10-13 ユーザー「UATの結果待ちのものは…」） |
| ws089-p012 | 検索の key 操作・頁の touch の scroll | cleared（2026-10-08 Q1） |
| ws089-p013 | About の memory と Storage の使用量 | cleared（2026-10-08 Q1、T1-278） |
| ws089-p014 | Network の頁を 5330 の Wi-Fi で | cleared（2026-10-13 ユーザー「UATの結果待ちのものは…」） |
| ws089-p015 | 日本語の UI | canceled（2026-10-05 夜、WS158 に吸収） |
| ws089-p016 | 単一の instance | cleared（2026-10-05 Q1、T1-185） |
| ws089-p017 | accent の色・dark の外観 | cleared（2026-10-05 Q1、T1-195・T1-198b） |
| ws089-p018 | 全文規約と回帰 | planning のまま WS を閉じた（WS の外の一括の見直しへ） |
| ws089-p019 | titlebar の検索欄から Down・Up で結果へ | cleared（2026-10-08 Q1、T1-238） |
| ws089-p020 | BUG-152: Wallpaper の頁の縮小表示 | cleared（2026-10-03 Q1、T1-011） |
| ws089-p021 | Wi-Fi の自動の scan と Disconnect の icon、BUG-184 | cleared（2026-10-05） |
| ws089-p022 | Ethernet の設定の取得・変更 | cleared（2026-10-05 Q1、T1-162） |
| ws089-p023 | Storage の解析と Trash | cleared（2026-10-05 Q1、T1-159） |
| ws089-p024 | Mouse の頁を device ごと、加速 | cleared（2026-10-05 Q1、T1-152・T1-154） |
| ws089-p025 | Sharing の SSHD の ON/OFF | cleared（2026-10-05 Q1） |
| ws089-p026 | Users の頁 | cleared（2026-10-05 Q1、T1-183b） |
| ws089-p027 | About に版の名前 | cleared（2026-10-05、T1-108） |

## 記録の所在

Phase の計画・結果は削除する。git の commit `fb1f6afee` の `plan/ws089/` にある。Queue の履歴は [plan/history](../history/index.md)。
