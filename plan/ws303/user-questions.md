<!-- awesome-plan project=zedbsd record=ws303-user-questions -->

# WS303 ユーザーへの問い（用語と概念から）

2026-10-29 P5。

## 前置き

- **どの WS か**: WS303「熱と fan」。
- **どんな WS か**: zedBSD に、温度の読み、fan の回転数の読み、熱と速さの方針（strategy）の選択、fan を強めに回す指定（boost）、危ない温度での電源断、を作る。最初の対象は 5330（Dell のノート）と Pi 5。
- **今の状態**: 設計の段（p001）。設計の文書は第 5 版（[design.md](design.md)）。これまでの答え（Q303-1〜14）は設計に入れた。実装はまだ。
- **決まっていない事**: 下の 5 つ。どれも実装の前に要るが、どれも推しで進めても後で直せる。

## 用語

- **strategy**: 「速さ優先か、省電力か」の全体の方針。ユーザーの決定（Q303-11）で、performance（速さ）・balanced（釣り合い）・powersave（省電力）・manual（手で 3 つの軸を決める）の 4 つ。
- **受け手**: strategy を受け取って、自分の hardware に当てはめる部分（driver）。例: CPU の速さの driver（WS301）は CPU の周波数の範囲に、Dell の driver は BIOS の熱の mode に、Pi 5 の fan の driver は fan を回し始める温度に写す。
- **firmware**: 機械に最初から入っている制御の program（BIOS、Dell の EC など）。5330 の fan は firmware が回す。
- **3 つの軸**（manual の時だけ、各 0〜100）: 実行速度（speed）、騒音（noise）、細粒度パワーゲート（gating: 使っていない部分の電源をこまめに切る事）。
- **直の設定**: WS301 で root が CPU の周波数の範囲を数で直に書く診断の口（WS301 の答え Q301-7 で「root の診断用だけ」）。

## Q303-15 起動した時の strategy

- 背景: 電源を入れた直後、zedBSD は strategy を何にするか。
- 決まると変わる事: A なら起動で何も書き換えない（BIOS の設定のまま動く）。B なら前回の選択が起動のたびに戻るが、Dell では起動のたびに BIOS の設定を書く処理（SMI という firmware の呼び出し）が走る。
- (A, 推し) firmware のまま（表示は「firmware」と firmware の今の mode の名前）。
- (B) 前回選んだ strategy を保存して起動の時に戻す（前と同じなら書かない）。
- 推しの理由: 起動を遅く・危うくしない。選びたい時は `/sbin/thermal` か設定の画面で 1 回選べばよい。

## Q303-16 細粒度パワーゲート（gating）の軸が何に効くか

- 背景: gating は「使っていない部分の電源をこまめに切る」度合い。効かせる先は、CPU の深い休み（C-state）、PCIe の link の省電力（ASPM）、NVMe の自動の省電力（APST）、GPU の部分の電源断など、別々の driver。今は CPU の深い休みの仕組みが無く、ASPM・APST は WS300（設計前）、GPU は各 GPU の WS。
- 決まると変わる事: A はこの WS の仕事が大きく増える。C は軸の値を記録して見せ、各 WS が自分の時に使う。
- (A) この WS で全部の受け手を作る。(B) CPU だけ。
- (C, 推し) 軸は保存して示すだけ。受け手は WS300 などがそれぞれ足す。
- 推しの理由: 受け手の hardware を扱う WS が別にあり、そこで作る方が正しく試せる。

## Q303-17 WS301 の直の設定と strategy の関係

- 背景: root が WS301 の直の設定で CPU の周波数の範囲を書いた後、strategy の表示と、次に strategy を選んだ時をどうするか。
- (A, 推し) 直の設定が strategy より優先し、表示に「custom（直の設定あり）」の印。次に strategy を選ぶと直の設定は消える。
- (B) 直の設定は manual の時だけ許す。
- 推しの理由: WS301 の答え（直の値は root の診断用）に合い、診断の後は strategy を選び直すだけで元に戻る。

## Q303-18 3 つの軸の向き（新しい問い）

- 背景: 0〜100 のどちらの端が何かを決めないと、driver ごとに逆に作られる恐れがある。特に「騒音」は「騒音の多さ」とも「静かさの度合い」とも読める。
- (A, 推し) どれも「100 = その物が多い」: speed 100 = 一番速い、noise 100 = 一番うるさくてよい、gating 100 = 一番こまめに電源を切る。
- (B) どれも「100 = 好ましい方」: speed 100 = 一番速い、noise 100 = 一番静か、gating 100 = 一番こまめに切る。
- 推しの理由: 軸の名前（実行速度・騒音・パワーゲート）をそのまま量として読める。

## Q303-19 firmware が fan を遅くする mode を選んでよいか（新しい問い）

- 背景: 5330 の BIOS の熱の mode は 4 つ（UltraPerformance・Optimized・Cool・Quiet）。Quiet は firmware が fan を遅くし、代わりに CPU の電力を下げる。ユーザーの決定は「必要な回転数を下回らせることはできない」「コントロールできないときはストラテジだけファームウェアに伝える」。OS は firmware の fan の曲線を変えられない。
- 決まると変わる事: A なら manual で騒音を低くした時に Quiet を選ぶ。B なら Quiet は使わず、静かさは CPU の上限を下げる事だけで得る。
- (A, 推し) よい。manual で noise ≤ 33 の時だけ Quiet（powersave は Cool）。firmware が自分の安全の範囲で fan を決める形。
- (B) 選ばない。
- 推しの理由: OS が fan を下げる指定をするのではなく、firmware に方針を伝え、firmware が自分の温度の判断で回す形で、決定の「ストラテジだけファームウェアに伝える」に当たる。
