<!-- awesome-plan project=zedbsd record=ws089-p021 -->

# ws089-p021: Wi-Fi の画面の自動の scan（Scan のボタンを無くす）と Disconnect の icon

Status: planning
Disposition: normal
Parent: [WS089](../ws.md)
Queue: 未定（q700 の Settings の Wi-Fi の Bug と一緒か、その後）

## ユーザーの要望（2026-10-04 夜、UAT-3 の後、原文）

「・WiFi設定画面で、Scanボタンは不要にしたい。この画面を表示しているならスキャン開始されているべき。画面表示時点ではスキャンのキャッシュがリストされればよい。画面を閉じたらスキャン停止でOK。ただ、アプリが多重起動されていたり、Settingsと右上WiFiメニューが両方表示されている場合は、考慮が必要。スキャン要求はコンポジタがカウントして、libkeiland-backend経由でnetworkdにスキャンオンオフを要求すればいいかも。
・WiFi APの項目のDisconnectボタンは、アイコンにしたい。」

## 範囲

1. Settings の Wi-Fi の頁の **Scan のボタンを無くす**。頁を表示した時点では networkd の scan の cache の一覧を出し、表示している間は scan を続ける。頁を閉じたら（別の頁へ移る・窓を閉じる・最小化は設計で決める）scan を止める。
2. **scan の要求の数え上げ**: Settings の複数の instance や、Settings と system bar の Wi-Fi の menu が同時に表示されている場合を考える。案（ユーザー）: compositor が scan の要求を client ごとに数え（参照の数）、0→1 で libkeiland-backend 経由で networkd に scan の on、1→0 で off を要求する。client が切れた時（crash を含む）は compositor がその client の要求を外す。拡張の protocol（kl_system_manager_v1）に「scan の要求の開始・終了」を足すか、既存の要求で足りるかを設計する（WS131 の境界の規則: app は libkeiland の kl_system_* だけ、OS 固有は libkeiland-backend だけ）。
3. networkd の scan の on・off の口（未接続の時の自動の探索（BUG-158 の 5 秒ごとの search）との関係、接続中の scan の扱い）。
4. AP の行の **Disconnect のボタンを icon に**する（文字の button をやめる、tooltip か accessible な名前は残す）。system bar の menu の同じ物も揃えるかは設計で決める。
5. [BUG-184](../../bugs/BUG-184.md)（オフのクリックが Scan に取られる）は 1 で Scan のボタンが無くなるので、この Phase と一緒に直す（hit の判定の食い違いの根も確かめる）。

## 受け入れ（案）

- 頁を開くと cache の一覧がすぐ出て、scan が回り一覧が更新される。頁を閉じると networkd の scan が止まる（networkd の状態で確かめる）。
- Settings を 2 つ開く・Settings と system bar の menu を同時に開く・片方を閉じる・client を kill する、の各場合で scan の on・off が数え上げどおり。
- Disconnect は icon で、押すと切断する。
- C の全文の規約、build warning 0、OS の境界の checker（`plan/tools/keiland-os-boundary/check.sh`）。QEMU は T1（RTL8822BU の USB passthrough か networkd の模擬）、実機は UAT。

## 依存

WS131 の libkeiland-backend・kl_system_manager_v1（済み）、WS005 の networkd。BUG-183・185・186・187・188（q700）と同じ経路なので、同じ担当が続けて行う。
