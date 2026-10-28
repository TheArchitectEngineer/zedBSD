<!-- awesome-plan project=zedbsd record=ws291 -->

# WS291 GPU の driver の描画の速さ: desktop 60 fps と重い 3D（AMD RDNA → NVIDIA → PowerVR）

- Master: [plan/master.md](../master.md)（2026-10-26 23:05 作成）。Primary Milestone: MG006。前提: [WS039](../ws039/ws.md)（amdrdna の driver、display・executor・desktop は実機で動く）。手本: [ws270-p007](../ws270/phase007/phase.md)（VF2 の PowerVR の desktop の速さ）。
- 追加（2026-10-26 23:08 ユーザー「WS291にさらにフェーズを追加して、DebianマシンでGPUを入れ替えて、NVIDIAでも同様に、デスクトップの60fpsと、モデルの50fpsをやります。これはP2がamdrnaの作業を終えてからです。また、さらにフェーズを増やして、PowerVRのドライバでも、50fpsをやります。VF2での作業は、RDNAで作業手順を確立したあとで、P3に依頼します。」）: NVIDIA（同じ Debian の機械で GPU を入れ替え、P2、RDNA の後）と PowerVR（VF2、P3、RDNA で手順を確立した後）を同じ WS に足す。
- Status: incomplete（2026-10-26 23:05 新設、p001 cleared）
- 出典: 2026-10-26 23:05 ユーザー「P2も立てます。ssh 10.0.10.3で、RDNAの接続されたDebianに入れます。USBメモリにはzedBSDが入っています。この環境でカーネルをアップデートしてUEFIの起動項目を変更しながら再起動し、amdrdnaドライバの改善を行ってほしいです。要領としては、PowerVRのドライバの改善のときと同じようにコンポジタを操作して時間を計測し、改善できる部分を改善してほしいです。デスクトップで60fpsをまずは達成します。スクリプトは既存のものがあると思います。もしかしたらすでに60fpsになっているかもしれません。そのあと、Vulkan Model Viewerを使ったり、その改良版を作ったりして、重たい3DシーンをLinuxドライバと同じfpsで描画できるように調整します。一つのやり方として思うのは、Linuxで60fpsを切るような重さに3Dモデルの量を調整し、たとえば50fpsにします。これをzedBSDでも50fps達成できるように調整し、できれば55fpsくらいまで、Linuxよりよくなるまでプロファイルベースで改善します。できそうですか？」

## 単一の到達目標

GPU ごとに同じ手順（desktop の 60 fps、重い 3D の model の 50 fps）で driver の描画の速さを揃える。最初は AMD RDNA で手順を確立し、NVIDIA・PowerVR に当てる。

- AMD RDNA: 実機 10.0.10.3（Navi10 の RX 5600 XT、Debian 13 と USB の zedBSD）で、(1) Keiland の desktop の操作（ws270-p007 の frame-ops の段）が 60 fps、(2) Linux（Debian の RADV）で約 50 fps になる重さの 3D の scene（mview か改良版）を、zedBSD の amdrdna で Linux と同じ fps、できれば 55 fps 前後の Linux 超えまで、profile を見て改善する。
- NVIDIA（nvrtx、10.0.10.3 で GPU を入れ替え）: desktop 60 fps と model 50 fps（RDNA と同じ形）。
- PowerVR（VF2）: model 50 fps（desktop の 60 fps は ws270-p007 で済み）。

## 手順と道具

- 実機の操作: Debian（ssh 10.0.10.3）から USB の zedBSD の /boot に vmunix・package を入れ、`efibootmgr --bootnext`（zedBSD USB の Boot0001 か KIOXIA の項目）で再起動、終わったら Debian へ戻る（zedBSD の BootOrder は変えない、BootNext の 1 回だけ）。zedBSD の側は ssh か serial の対話。console・serial の log で判定しない。画面の判定は keiland-shot の PNG。
- 計測: `plan/tools/frame-ops/`（frame-ops.sh・frame-times.py・key-latency.py・shot-diff.py）、mview の `--spin=N`（fps の報告）。Linux の基準は Debian の上の同じ mview（Wayland の上の Vulkan、RADV）を同じ解像度・同じ model で。
- 試験の方針（AGENTS.md）の例外: 実機の計測はこの WS の本体なので担当が直に行う（WS039 と同じ）。QEMU は使わない。

## 計測の決定

- 2026-10-26 23:10 ユーザー「zedBSDでは、graphicalログインは必要なく、graphicalログインをするなら、direct scanoutで限界まで3D性能を正しく計測しましょう。USBはまるごと書き換えてOKです。」→ login=graphical の起動は要らない。3D（mview）の計測は fullscreen の direct scanout（compositor が合成しない）で限界の性能を測り、direct scanout であることを確かめて記録する。Linux 側も同じ条件で比べる。10.0.10.3 の USB は丸ごと書き換えてよい。VF2（p020）でも同じ。

- 2026-10-26 23:52 ユーザー「そのベンチマークですが、command組み立ては毎回やってたらVF2の方がCPUが遅いので確実に遅くなります。頂点データやインデックスはバッファに入れておいて、描画コマンドの組み立て時間が最小限になるように変更してください。でないと実際のユースケースと合わないです。ただ、別途、たくさんのコマンドをキューするようなベンチマークも、必要かもしれませんね。」→ 主の 3D の benchmark は、頂点・index を device の buffer に置き、毎 frame の command の記録を最小にする形（instancing か記録済みの command buffer の再利用）に変え、Linux の基準を測り直す。別の benchmark として、毎 frame たくさんの command を記録・submit する mode も残して測る。
- 2026-10-26 23:54 ユーザー「コマンド投入がzedBSDでは遅い可能性もあるので、コマンドが大きいか、コマンドがたくさんあるか、は、RDNAのLinuxとzedBSDで比較できるといいですね。」→ command の投入の benchmark を 2 軸に: (a) 大きな command buffer（1 回の submit に多数の draw）、(b) 多数の submit（小さな command buffer）。量を段で振り、frame あたりの記録・submit・GPU の待ちの ms を RDNA の Linux と zedBSD で並べる。

## 受け入れ

- desktop: frame-ops の各段が 60 fps（ws270-p007 の基準の表と同じ形で記録）。
- 3D: Linux で約 50 fps の scene を定め（model・数・解像度・shading を記録）、zedBSD の fps が Linux 以上。各改善の前後の数値と profile を記録。
- amd64 の vmunix・変えた userland が warning 0。実機の証拠と未実施の事を分けて記録。

## Phase

| Phase | 内容 | 依存 | 状態 |
| --- | --- | --- | --- |
| [p001](phase001/phase.md) | desktop の計測と 60 fps（既に 60 fps なら記録して閉じる） | WS039 p009 | cleared（2026-10-26、main bad29b52f、全段 60 fps） |
| [p002](phase002/phase.md) | Linux で約 50 fps の 3D の scene を決める（mview の拡張を含む）、zedBSD との差を測る | p001、p002a | cleared（2026-10-27 00:43 Q1。qs40 ×6500 instanced で zedBSD 20.1・Linux 50.1 fps、command の重さの組 big-N・subs-N も） |
| [p002a](phase002a/phase.md) | amdrdna の executor に depth・index の draw・mip の texture（mview が動くまで） | p002 の Linux の基準 | cleared（2026-10-27 00:43 Q1。実機で Linux と pixel 一致） |
| [p003](phase003/phase.md) | RDNA: 投入の経路（vkQueueSubmit が GPU の完了を待たない非同期、record の 1 command と submit の費用を削る）。big-N・subs-N で Linux に近づける | p002 | cleared（2026-10-27 04:31 Q1） |
| [p004](phase004/phase.md) | RDNA: PA の small primitive filter・back-face cull・VGT の vertex reuse を RADV の値に、scene-6500 の GPU の ms | p003 | cleared（2026-10-27 08:34、driver の変更なし、差は NGG の culling） |
| [p005](phase005/phase.md) | RDNA: NGG A（passthrough、wave32 1 つの subgroup） | p004 | cleared（2026-10-27 13:52 Q1 判定、q1611。受け入れ: export の EXEC の wait の修正、1 体 pixel 差 0、6500 体 8 pixel、hang 回復、GPU 41.4 ms） |
| [p006](phase006/phase.md) | RDNA: NGG B+C（primitive の culling と頂点の詰め直し・属性の後回しを Mesa の形で一緒に。2026-10-27 14:50 Q1: compaction 無しの culling は GFX10 で止まる（仮説 A）ので p007 と統合） | p005 | in-progress（q1639: VRAM の page entry の fragment（eb0ec22fd）で scene-6500 30.7 → 44.7 fps・GPU 30.6 → 20.5 ms（Linux 49.7 fps・19.7 ms）。残りは頂点の側 4 % と CPU 側 1.2 ms） |
| p007 | RDNA: NGG C（vertex の詰め直しと属性の後回し、wave をまたぐ subgroup）、目標 GPU 約 20 ms | p006 | p006 に統合（2026-10-27 14:50 Q1。目標 GPU 約 20 ms は p006 の受け入れへ） |
| p008〜p009 | RDNA: HTILE、guard band、CPU 側の record（2.3 ms）の短縮 | p007 | — |
| p010 | NVIDIA: GPU の入れ替え（ユーザー）、desktop の計測と 60 fps | RDNA の Phase の全部、P2 | planned |
| p011 | NVIDIA: Linux で約 50 fps の scene での差の計測と 50 fps（p012〜p019 は改善の予備） | p010 | planned |
| p020 | PowerVR（VF2）: model の 50 fps（RDNA で確立した手順、P3） | RDNA の手順の確立（p002 と最初の改善） | planned |
| 最後 | 規約の全文の見直し | 実装の全部 | — |

- 2026-10-27 04:44 ユーザー（Q1 の問い「NGG の A〜C をどう進めますか？」へのクリック）「A〜C を今進める（推し）」。

- 再開点（2026-10-27 16:55 P2、q1623）: RDNA の次の改善の候補と計測は [rdna-next.md](rdna-next.md)（属性の後回し、BAR の WC の map、acquire の待ち、画素の側の小物の順）。
- WS306 との順（2026-10-27 ユーザー「WS306ですが、AMD RDNAドライバの最適化をWS291でやりきったあとに、要素の最適化をオプションに移して実装し直す作業という認識なので、両方のWSに記録してください。」）: RDNA の最適化はこの WS291 で最後までやりきる（NGG の culling などは今の形で入れる）。その後に [WS306](../ws306/ws.md) で、各最適化の要素をゲーム最適化の option に移して実装し直す。

## VF2（p020）の実機

- 2026-10-26 23:09 ユーザー「VF2はssh kei@10.0.30.2で、パスワード（略）です。VF2はSwitchbotで電源を入れられます。」→ VF2 は ssh kei@10.0.30.2（password は tree に書かない、Q1 が担当に渡す）。電源は SwitchBot のプラグ（AC276E42B252）で Q1 が `plan/tools/switchbot.py` で入れ切りする（担当は Q1 に頼む）。serial は host の /dev/ttyUSB0。
- 2026-10-26 23:09 ユーザー「VF2には現在、PowerVRドライバのカーネルやファームウェアが入っていないと思うので、SSH経由で更新が必要です。」→ p020 の最初に、main の config-visionfive2.mk（desktop の構成、PowerVR の driver あり）で vmunix と PowerVR の firmware（zedBSD-firmware から）・desktop の package を作り、ssh で VF2 の /boot/boot0/vmunix などを入れ替える（今の vmunix は WS288 の GHCI の kernel、P3 の ac7ee7d64 の物。入れ替える前の物は .old 等で残す）。起動の後に PowerVR の attach と firmware の load を確かめてから計測する。
- WS288 の作業で VF2 の zedbsd.cfg から login=graphical を外したまま（元は zedbsd.cfg.p044orig）。上の計測の決定で login=graphical は要らないので戻さない。

## 未決（その Phase の前に確かめる）

- NVIDIA の Linux の基準の driver（NVIDIA の proprietary か nouveau/NVK か）と、入れる GPU の型番。GPU の入れ替えはユーザー。
- PowerVR の 50 fps の基準: Linux との比較か、50 fps の絶対値か（VF2 の Linux の PowerVR の driver の有無で決まる）。

## 2026-10-28 の再開（q1635、P2）

- 2026-10-28 ユーザー「P2はRDNAマシンで作業します。RDNAの高速化をお願いします。まずNGGなどの高速化を取り入れてLinuxと同等以上の描画速度を達成して、そのあとで、ゲーム用最適化をオプションに分けていく段取りになっているので、確認してください。」
- 確かめ（Q1）: 段取りは記録のとおり。WS291 で RDNA の最適化（NGG の culling・compaction・属性の後回しなど）を今の形で入れて Linux 以上（scene-6500 で GPU 約 20 ms 以下、fps が Linux 以上）にし、その後に [WS306](../ws306/ws.md) で各要素をゲーム最適化の option に移して実装し直す。q1635 は p006 の続き → p008〜p009（[rdna-next.md](rdna-next.md) の順）。

- 再開点（2026-10-28 04:45 P2、q1635、worktree p2-ws291b）: 実装と実機の数は [phase006](phase006/phase.md) の「q1635 の続き」以下。scene-6500 は
  29.3 → 30.7 fps（WC で record −1.9 ms）、GPU 30.6 ms は動かず。次は phase006 の「残りの見立て」の (1)（我々の IB を RADV の culling の IB と全部突き合わせる）。
  道具: `plan/ws291/tests/round.sh`（deploy・nggshape・shot・bench・hang）、`plan/ws291/tests/nowc-build.sh`（診断 patch を当てた tree の build）、
  build/ws291/linux-ibdump/ibdump.so（host で build、Debian の /tmp に置く）。機械: stick は vmunix-lat1 と ngg=2（cfg に診断の nggx=0 の行、無害）。

## 2026-10-28 自律の継続（q1639、P2）

- 2026-10-28 ユーザー「P2は自律的にループして、Linuxと同等以上の性能をゴールに、継続してください。hangが繰り返し起きるなら調査して修正してください。30分調査してわからなかった場合は、Fable 5.1 Highのサブエージェントを作成して、事情を説明した上で解決までどういう進め方をすればいいかのアドバイスを得て、さらに継続してください。どうしても解決できない場合、停止してユーザに判断を求めてください。これらはP2が自律的に行います。」

- 再開点（2026-10-28 07:55 P2、q1639、worktree p2-ws291c）: [phase006](phase006/phase.md) の「q1639」節。scene-6500 は 44.4〜45.7 fps（frame 22.2 ms、fence 20.5〜20.7、
  Linux 49.7〜50.2 fps・19.5〜19.7）。直した事: VRAM の PTE の fragment（eb0ec22fd、30.7 → 44.7 fps）、ring の空きの計算（d218e4dc7、hang の後の compute の EIO）、
  thermal fault で仕事を止め fan を 100 %（31129fc30・1b9cc7865）、SMU の電圧の feature（1b9cc7865）、guard.sh。次: (1) fence の約 1.3 ms の固定の遅れ（完了の通知の
  thread の起床 3 回、phase006 の見立て）、(2) 頂点の側の 0.8 ms（s_barrier の数、position の fetch）、(3) big-N・subs-N の CPU 側（executor の外の往復が大半）、
  (4) SMU が長い負荷で壊れないかを guard の下で見続ける（壊れたら mode1 の reset と shutdown の時の reset が候補）。長い試験は必ず round.sh か guard.sh の下で。
- 2026-10-28 ユーザー「P2は、USB NICが通信できない（DHCPが取得できないとか、ue0が異常状態）のとき、自分で再起動するように、serivceを登録すれば、とりあえず問題を回避して継続できると思いますので、P2の判断でこれを行ってよいです。」「P2は、5330と同じように、NVMeにzedBSDをインストールしてから作業を継続する方がいいような気がします。USBメモリが半導体供給不足で手に入りづらいからです。」 → P2 が NIC の監視の service（WS291 の image だけ）と、RDNA の機械の NVMe の zedBSD の区画への移行を行う（空きが無く ext4 を縮める時はユーザーに確かめる）。
- 2026-10-28 RDNA の機械の NVMe の区画: ユーザーの答え（クリック）「ext4 を縮め、root 32 GiB」→ live USB で Debian の ext4 を縮め、zedBSD の boot 256 MiB・root 32 GiB・swap 1 GiB（P2）。
- 2026-10-28 ユーザー「amdrdnaドライバが、初期化段階で画面に表示を行っていると思いますが、これをコメントアウトしてほしいです。…診断用の処理を関数に分けて、呼び出し元をコメントアウトして説明のコメントを残すのでもいいです。この作業のあと、次の作業を進めてください。」→ P2（q1645）。
- 2026-10-28 ユーザー「P2のテスト中に、OSの起動ロゴ画面がちらついて表示されています。GOPの内容でしょうか？もしかして、メッセージ表示のたびにコンソールを書き換えていたりしますか？GPU使用中はコンソール出力は完全に止めて、内容をアップデートしないようになっているかを確認していただき、さらに、GOPのバッファが一瞬ひょうじされてしまっている理由を解析し、修正してください。」→ P2（q1645）、案 D の前に。

## 2026-10-28 q1645（P2、worktree p2-ws291d）

- 承認: q1639 と同じユーザーの文に加え「P2は何度でもFableに聞いてください。」。ユーザーの指示（2026-10-28）: USB NIC が通信できない時に自分で再起動する
  service（nicwatch）、NVMe への zedBSD の移行（ext4 を縮め root 32 GiB）、amdrdna の初期化の画面の表示を診断用の関数に分けて呼び出しを comment に。
- 再開点（2026-10-28 10:40 P2）: [phase006](phase006/phase.md) の「q1645」節。scene-6500 は 45.4 → 49.2 fps（Linux 50.0）、GPU の時間（fence 18.9〜19.2 ms）は
  Linux（19.56）より短い。scene-1500 は 70 → 180〜184 fps（Linux 201）。直した事: Keiland の game mode の present を thread に（5f9ec7f08）、libvulkan の
  Wayland の acquire を fd で待つ（9b876b5fe）、mview --images と bench 4 枚（632d172dc）、memory clock（UCLK）を最上段に固定（146fdfe5d）、初期化の
  画面の表示の無効化（dfcaa14e5）、NVMe の区画と道具（6574b457c）。次: 残りの CPU 側（acquire・record・present）、big-N・subs-N の record・submit
  （phase006 の「残り」）。機械は NVMe の zedBSD（Boot0004）。
- 2026-10-28 ユーザーの補足「P2のモデル表示のテスト中に、起動ロゴ画面が見えてしまう、というバグです」（mview の表示の試験の間に起動の logo の画面が見える）。
- 2026-10-28 ユーザー「どこかの経路で、ゲームモードに切り替えるときにGOPのバッファが表示されてしまうのが仕方がないとしても、そのバッファはクリアして黒にしておいてほしいです。」→ display を takeover した後（GPU の使用中）は GOP の buffer を黒で塗り、その後 console が書かないようにする（新しい P2 の最初の作業）。
- 2026-10-28 ユーザー「P2のCPU時間の問題ですが、案Dがだめだったことと、状況を、Fable 5.1 Highのサブエージェントを作って伝えて、今後の作業方針について助言をもらってOKです。」→ q1651 で logo の件の後に。
- 2026-10-28 ユーザー「P2が自律的に助言をもらってOKです」→ Fable の助言は P2 の判断でいつでも。

## 2026-10-28 q1651（P2、worktree p2-ws291e）

- 再開点（2026-10-28 11:15 P2、13 時までの待機の指示で返却）: 起動の logo の件は直して実機で確かめた（[phase006](phase006/phase.md) の「q1651」節:
  GOP の buffer を黒に塗り、lease の間の切り替えは黒、console は 3 s の rest と shutdown の時だけ再開。logo3 で console の画面の黒でない pixel 0）。
  未実施は 3 s の rest の後の resume と shutdown の実機、ユーザーの目。CPU 時間は Fable 5.1 High に聞いた（同じ節）: 次は M2（present の copy を
  飛ばす試し）→ 案 E（libvulkan の Wayland の直の present、Q1 へ案）、executor だけの push の inline と execute_draw の state の差分。
  機械: NVMe の zedBSD（kernel vmunix-logo2 = この節の build、amdrdnactl に watch）で idle、guard は止めた。今日の P2 の SwitchBot の入れ直しは
  11:04:25 が 6 回目。

## 2026-10-28 q1653（P2、worktree p2-ws291f）

- 再開点（2026-10-28 16:45 P2）: [phase006](phase006/phase.md) の「q1653」節。commit: 8e4463127（push の領域・draw の state の差分）、b93e355ea
  （audit の 'mapped'）、b4897a17b（案 1 swapchain +1 枚）、d162d6700（案 2 直の encode）、6ac6d3754（表の再利用）、2ff3d0c73・e27a16d15・53213c001
  （measure.sh timing・cfg、soak.sh、guard.sh の dmesg）、b113329f3（dedicated-host の試験の直し）、141ce391d（案 3a Reset・Begin を head に）、
  142fd7b88（SMU: memory 側の feature の既定を off、hang の mark、実験の memory_allow）。
- 分かった事: bench の big-N・subs-N は cfg の amdrdna.vk.timing の上乗せ込みだった（外して取る）。SMU の idle の停止は memory 側の feature（UCLK 固定）
  が原因（対照 40 分 0 回、あり 16・20 分で停止）、固定の後も DCN の force allow を残すと 40 分 0 回。既定は安全のため off（UCLK 100 MHz）。
- 次: (B) の長い soak の後に memory 側の既定を戻すかを Q1 と決める。その後に案 3b・4・5（phase006 の設計）。
- 2026-10-28 UCLK (B)（固定の後も DCN の p-state の force allow を残す）の 2 時間以上の soak: ユーザー（クリック）「夜間に流す (推し)」。それまで既定は off。
