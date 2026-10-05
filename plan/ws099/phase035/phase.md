<!-- awesome-plan project=zedbsd record=ws099-p035 -->
# ws099-p035: 設計 — App Home の stage と 2 層の animation

Parent: [WS099](../ws.md)
Status: planned（2026-10-06 Q1 の設計の第 1 版。最初の実装の段は montage（ユーザーが選ぶ）、その後に実装）
Disposition: normal
Related: [BUG-236](../../bugs/BUG-236.md)・[BUG-225](../../bugs/BUG-225.md)・[BUG-232](../../bugs/BUG-232.md)・[BUG-237](../../bugs/BUG-237.md)・[ws128-p012](../../ws128/phase012/phase.md)

## 由来（ユーザー、2026-10-06 UAT）

- BUG-236「アプリ一覧は、暗い背景のステージに、アプリアイコンが置かれ、ややスポットライトがそれぞれのアイコンに当たって、光沢のある床にアイコンが反射しているエフェクトがいいです。」
- BUG-225「アプリ一覧の表示がワンテンポ遅れる感じがします。0.7sくらい…最初に直ちに描画できるテクスチャで画面を覆うアニメーションを開始して…アイコンは別なアニメーションで後追いで浮かび上がる、2層性のアニメーションにすれば…」
- BUG-232 起動中の app は起動せず切り替える。

## 1. 見た目（stage）

- **地**: 暗い stage。画面全体を、今の desktop の blur（wallpaper と窓）の上に暗い glass（黒、不透明度 0.82 の目安）で覆い、中央の上が少し明るい放射状の濃淡（上部の bar の「中央が明るい」と揃える）。
- **床**: icon の行ごとに、icon の下端の少し下に**光沢の床の線**（横の細い明るい帯、端に向かって消える）。床より下に **icon の反射**（上下を反転した tile、不透明度 0.25 から下へ 0 に fade、高さは icon の 0.35）。
- **spotlight**: 各 icon の上から、icon の幅の 1.6 倍ほどの楕円の柔らかい光（白、不透明度 0.10〜0.14）を icon の後ろの床に。hover・選択の icon は光を強める（0.22）。
- **icon**: montage-4 の tile。地が暗いので穴は `GLASS_HOLE_GROUND`（暗い stage が記号に透ける、BUG-237 の穴の扱い）。名前の文字は白（不透明度 0.9）。
- dark・light の外観で同じ（stage はいつも暗い。上部の bar と揃える）。

## 2. 2 層の animation（BUG-225）

| 層 | 始まり | 中身 | 時間 |
| --- | --- | --- | --- |
| 第 1 層（覆い） | 入力（Super・App Home の button・gesture）の**次の frame** | 暗い stage の地と濃淡だけ（準備の要らない単色＋既にある blur の texture） | 120 ms の fade in |
| 第 2 層（中身） | 準備が終わった frame（遅くとも 400 ms で第 1 層だけでも見える） | tile・spotlight・床・反射・名前 | icon ごとに 30 ms ずらして下から 12 px 浮き上がり＋fade（180 ms） |

- 準備: tile は起動時の atlas（ws128-p012、20/26/48/72 px）にあるので、App Home の開きで要るのは名前の文字と配置と反射の texture。**起動時と app の一覧の変わった時に先に作っておく**（反射は tile の atlas を上下に反転して描くだけなので texture は要らない、shader の uv の反転）。それでも 0.7 秒の遅れが残るなら、その内訳（文字の rasterize・blur の再計算）を測り、文字は起動時に cache、blur は今の frame の物を使い回す。
- 閉じる: 第 2 層を 100 ms で fade out、第 1 層を 120 ms で fade out。
- 遅れの目標: 入力から第 1 層の最初の frame まで 1 frame（16 ms）以内、第 2 層の始まりまで 150 ms 以内。

## 3. 起動中の app（BUG-232）

App Home で app を選んだ時、その app id の窓が既にあれば起動せず `shell_switch_to(最後に使った窓)`（[ws142-p007](../../ws142/phase007/phase.md) の関数、最大化の session の状態に合わせる）。起動中の app の tile の下に小さな点（bar の今の app の下線と同じ色）。

## 4. 試験

- host: stage の描画の host render（hole-host と同じ形、`plan/ws099/tests/`）。第 1 層の始まりの frame の数の計算。
- QEMU（T1・AAT）: `desktop.home.open-latency`（入力から第 1 層の log の行までの時間、`ZWL HOME layer=cover` と `layer=content`）、`desktop.home.switch-running`（起動中の app を選んで新しい窓が増えない）。
- 実機（ユーザー）: 見た目と速さの感触。

## 5. Phase の分け方（ベータ2）

| Phase | 内容 | 見積もり |
| --- | --- | --- |
| p035a | montage（spotlight・反射の強さ 2〜3 案）をユーザーに見せて選ぶ | 0.3 LW |
| p035b | 実装: stage の地・床・反射・spotlight の描画（glass の shader の mode の追加は避け、既存の image・glass の mode の組み合わせで） | 1 LW |
| p035c | 2 層の animation と準備の先回り、遅れの測定、BUG-232 | 0.7 LW |
| p035d | AAT のシナリオ・T1・規約の見直し | 0.3 LW |

## 6. 未決（ユーザー）

- spotlight・反射の強さ（p035a の montage で選ぶ）。
- light の外観でも暗い stage でよいか（案: よい、bar と揃える）。
