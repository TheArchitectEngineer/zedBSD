<!-- awesome-plan project=zedbsd record=ws099-p034 -->
# ws099-p034: 上部の system bar のデザインの調整（グループの pill と黒い地）

Parent: [WS099](../ws.md)
Status: planning（2026-10-06 ユーザーの追加と決定。参考の画像 4 枚。まず mock を見せる）
Disposition: normal

## 由来

ユーザー（2026-10-06）「Keilandコンポジタにコメントです。上部バーのデザインを調整したいです。添付が、1つめは上部バー左端でのアイコンのまとめかたと、黒いテクスチャをベースにするというアイディアです。2つめは、仮想デスクトップ切り替えの表示です。バーの中央部に置きます。3つめは、通知アイコン領域のアイコンのグルーピングです。4つめは時刻表示のグルーピングです。」

## 参考の画像（ユーザーの添付、Q1 が読んだ要点）

| 画像 | 部分 | 要点 |
| --- | --- | --- |
| [bar-1](images/bar-1.png) | 左端 | 左端に Kei の logo（青い葉の形）、細い区切りの線、その右に **起動中の app の icon を 1 つの暗い（黒い半透明の質感の）pill の中にまとめる**。icon は角の丸い四角の色の地に白い記号（P・歯車・画像・計算機・メモ・M）。bar の地は**黒い texture を base** にするアイディア |
| [bar-2](images/bar-2.png) | 中央 | **仮想 desktop の切り替えを bar の中央**に。小さな丸い横長の点（pill）が 4 つ並び、今の desktop だけが白い輪郭の大きめの pill。縮小の絵（今の thumbnail）ではない |
| [bar-3](images/bar-3.png) | 右 | 通知の icon の領域: 検索（虫眼鏡）・入力の方式（丸の中の A）・Wi-Fi・音量・電池を **1 つの pill にまとめる**。入力の方式の A は丸い地 |
| [bar-4](images/bar-4.png) | 右端 | 通知の icon の pill の右に、**時刻（Mon Oct 5 13:43）を別の pill** にまとめる。間に細い区切り |

## 範囲（案）

- bar の地を黒い texture（暗い glass）に。左・中央・右の要素を pill（角の丸い暗い半透明の帯）でまとめる。
- 左: logo ＋ 区切り ＋ app の icon の pill。中央: 仮想 desktop の点の pill（今の thumbnail の代わり、クリックで切り替え）。右: 状態の icon の pill ＋ 時刻の pill。
- light・dark（WS089 p017）、bar の高さ（ws099-p031）、Alt+Tab の preview（WS142・BUG-209）、USB の icon（ws132）、通知（WS156 の H7: bar の媒体の icon を通知に置き換え）との整合。
- app の icon は ws128-p012（デザインした icon への差し替え）と同じ絵を使う。

## ユーザーの決定（2026-10-06）

「plain dark glassだけど中央部が少し明るいような濃淡がいいですね。でもそれをベースにあなたが考えてくれてもいいです。ドットの明るさはウィンドウの有無で変えなくていいです。アイコンはラウンドでカラーをベースにする意見を反映してほしいです。」

- bar の地: **無地の暗い glass、中央が少し明るい濃淡**（横方向の緩い gradient）。これを base に担当が詰めてよい。light の外観でもこの暗い地を保つかは mock で示して確かめる。
- 中央の点: **窓の有無で明るさを変えない**（今の desktop だけが白い輪郭の大きめの pill）。
- app の icon: **角の丸い色の地に白い記号**（ws128-p012 にも反映）。

## mock（2026-10-06 P2、ユーザーの確認待ち、実装はまだ）

[mock-1.png](images/mock-1.png)（A〜D と拡大）、[mock-1-screen.png](images/mock-1-screen.png)（画面全体、Birch Lake）。QEMU を使わず host で描いた:
app の絵と Kei の mark は compositor の rasterizer（icons.c・mark.c）の出力、他は compositor の shape（glass・solid・ring・text）で
描ける物だけで描いた。道具 `plan/ws099/tests/p034-bar-mock.sh [OUT.png]`。

- 地: 壁紙の blur に暗い青みの tint（64%）、明るすぎる壁紙では暗い glass の上限（luma 0.22）まで暗くする（白い文字の contrast）。
  中央が少し明るい（横の緩い gaussian、+8.5%）、上から薄い sheen、下端に細い明るい線、bar の下に薄い影。
- 左: Kei の mark、区切りの線、起動中の app の icon（26 px、間 8）を 1 つの pill に。今の app は icon の下の短い白い線。
- 中央: 4 つの点の pill（点 18×7、今の desktop は 30×12 の白い輪郭）。窓の有無で変えない。画面の中央に固定。
- 右: 状態の pill（入力の方式の A は丸い地、USB は出ている時だけ、Wi-Fi は扇形、音量、電池）、その右に時計の pill。
- dock した時（C）: app の icon の代わりに、窓の icon と title の pill・窓の操作（戻る・進む・home・場所）、窓の button（最小化・
  元に戻す・閉じる）の小さな pill を中央の点の左に。
- light の外観（D）: 参考に明るい glass の版も描いた。提案は light の外観でも A の暗い bar（light・dark で bar を変えない）。

確かめたいこと（ユーザー）: (1) light の外観でも暗い bar（A）か、明るい bar（D）か、(2) 参考の bar-3 にある検索の虫眼鏡は今の bar に
機能が無いので mock では省いた（足すなら App Home の検索を開く）、(3) 今の app の印（icon の下の線）、(4) dock の時の配置（C）、
(5) Wi-Fi を今の棒から扇形に、(6) 中央の点の pill の大きさ。高さは ws099-p031 の 44 px のまま。
