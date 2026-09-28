<!-- awesome-plan project=zedbsd record=ws081 -->

# WS081: touch の操作の質（慣性のある scroll と、低い fps の touch の補間）

<!-- awesome-plan-current:start -->
Status: incomplete
Primary Milestone: MG006
Related Milestones: MG002
Objectives: O1
Parent: [Master](../master.md)
Queue: なし
Resume point: p001〜p005・p011〜p014 は cleared（2026-09-29）。次は p010（Files、p014 の指の drag and drop の後）。HID の driver・Wayland の compositor・ブラウザ（と Keiland の app）にまたがる計画はこの WS の 1 か所で行う
<!-- awesome-plan-current:end -->

## 目標（2026-09-28 ユーザー）

「タッチについては、ただマウスのように操作するのではなく、スクロールの操作みたいに余韻のあるやつとかも実装が必要で、これはHIDドライバ、
Waylandコンポジタ、ブラウザの3つに渡る実装と調整が必要な作業だと思います。計画は1カ所でやるのがいいですね。また、タッチのfpsが低い廉価な機種でも、
ある程度数式で補間して利用できるようにすることを目標に入れましょう。iPadみたいに120Hz駆動のタッチは、安い端末にはついてませんからね。」

1. **慣性のある scroll**（指を離した後も速度に応じて減速しながら続く、端での戻り）を、touch と touchpad で、ブラウザ・Files・Terminal・PDF Viewer 等で
   一貫した手触りにする。
2. **低い fps の touch の補間**: 報告の間隔が長い（例: 60 Hz 以下、不揃い）安い touch panel でも、数式による補間・予測（速度の推定、平滑化、
   表示の frame の時刻への resampling と短い外挿）で、scroll・drag・Notes の線が滑らかに見えるようにする。
3. 担当の層を 1 か所で設計する: HID の driver（正確な時刻の付与、報告の率の測定）、compositor（resampling・予測・gesture の判定・app への渡し方）、
   app（ブラウザ・Keiland の app の慣性の scroll）。

## 設計で決めること（p001）

- **時刻**: kernel が HID の報告に付ける時刻の精度（割り込みの時刻、USB の frame の番号）。evdev の event の時刻の意味。
- **補間・予測の数式**: 速度の推定（直近の点への最小二乗、1€ filter、Kalman 等の比較）、表示の vsync への resampling、予測の長さの上限と過剰な
  外挿の抑制。報告の率を自動で測り、率ごとに係数を選ぶ。
- **app への渡し方**: (a) compositor が生の `wl_touch` を渡し、app が慣性を計算する（GTK 等の形）、(b) compositor が scroll を
  `wl_pointer.axis`（`axis_source=finger`・`axis_stop`）に変換して app が慣性を付ける、(c) compositor が慣性まで作る。Wayland の標準の範囲で選ぶ。
  resampling・予測した点を app に渡すか、生の点を渡して app 側の共通の library で補間するか。
- **慣性の物理**: 減速の曲線（指数の減衰・摩擦）、速度の閾値、端での rubber band、二本指の scroll、fling の中断。Keiland の全 app で共通の
  library（libkeiland）にする。
- **試験**: touchinject（WS079 p012）で報告の率・jitter を変えた合成の入力（30・60・90・120 Hz、不揃い）を作り、補間の誤差と見た目を測る。

## Phase

2026-09-29 main の判断（design §8・§11）: p002 の範囲の変更を承認、注入の device と touchinject の Scan Time は p002 に、library の置き場所を承認、
p005 を scroller・gesture の library と app ごとの適用に分ける、p006 は WS074 に依存。design §10 のユーザーの判断は main が伝え、返事までは既定の案で進める。

| Phase | 目的 | Status | 依存 |
| --- | --- | --- | --- |
| [ws081-p001](phase001/phase.md) | 設計（上の項目、数式の比較の host の試験を含む）→ [design.md](design.md) | cleared | WS079 p012・p013 |
| [ws081-p002](phase002/phase.md) | kernel: 1 報告 1 時刻（URB の完了の時刻）と Scan Time の `EV_MSC`/`MSC_TIMESTAMP`、input の層の `EV_MSC`、注入の device と touchinject の Scan Time・µs の間隔（率の測定は p003）、WS079 p009 から移管の規約の是正 | cleared | p001 |
| [ws081-p003](phase003/phase.md) | 補間・予測の library（host で試験、率・jitter ごとの誤差の測定）→ `userland/desktop/libkeiland/motion.c`（公開は p004） | cleared | p001 |
| [ws081-p004](phase004/phase.md) | compositor: resampling・予測の適用（window の drag・端のジェスチャー）、wl_touch の時刻（Scan Time の τ）、library の公開（Makefile・exports.map・keiland.h） | cleared | p002、p003 |
| [ws081-p005](phase005/phase.md) | 慣性の scroll と touch の gesture の共通の library（libkeiland の scroller・gesture、host 試験、`KEILAND_VERSION` 10） | cleared | p004 |
| [ws081-p014](phase014/phase.md) | 指の drag and drop（compositor の `data.c`・`touch.c`: wl_touch の down の serial で start_drag を受け、drag を指に付ける）と Terminal の選択の文字の指の drag（main の判断 (1)、2026-09-29） | cleared | p011 |
| [ws081-p010](phase010/phase.md) | Files への適用（tap・長押し・scroll と慣性、指の drag and drop） | planned | p005、p014 |
| [ws081-p011](phase011/phase.md) | Terminal への適用（scrollback の px 単位の慣性の scroll、tap の click、長押しからの単語の選択） | cleared | p005 |
| [ws081-p012](phase012/phase.md) | PDF Viewer への適用（scroll と慣性、二本指の拡大、page mode の swipe） | cleared | p005 |
| [ws081-p013](phase013/phase.md) | Notes の指の scroll・pinch・double tap・toolbar の tap と掌の判定（ペンは線。design §3.8 の指の線は main の指示で取りやめ、§5.6） | cleared | p005 |
| ws081-p006 | ブラウザの慣性の scroll と touch の入力（browser の shell） | planning | p005、WS074（`browser.h` の scroll の範囲・overscroll） |
| ws081-p007 | 実機の 10 インチの touch LCD での調整（報告の率の実測、係数の調整） | planning | p005、p006、touch の USB |
| ws081-p009 | 全文規約確認と回帰（必須の最終確認） | planning | 全 Phase |

touchpad の二本指の scroll（目標 1）の Phase は、design §10 の 4 のユーザーの判断の後に置く。
