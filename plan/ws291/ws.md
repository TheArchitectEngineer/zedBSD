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
| [p002](phase002/phase.md) | Linux で約 50 fps の 3D の scene を決める（mview の拡張を含む）、zedBSD との差を測る | p001、p002a | cleared（2026-10-27 00:43 Q1。in-progress（測定済み: qs40 ×6500 instanced で zedBSD 20.1・Linux 50.1 fps、command の重さの組 big-N・subs-N も。Q1 の判定待ち）） |
| [p002a](phase002a/phase.md) | amdrdna の executor に depth・index の draw・mip の texture（mview が動くまで） | p002 の Linux の基準 | cleared（2026-10-27 00:43 Q1。in-progress（実装済み、実機で Linux と pixel 一致。Q1 の判定待ち）） |
| [p003](phase003/phase.md) | RDNA: 投入の経路（vkQueueSubmit が GPU の完了を待たない非同期、record の 1 command と submit の費用を削る）。big-N・subs-N で Linux に近づける | p002 | cleared（2026-10-27 04:31 Q1） |
| [p004](phase004/phase.md) | RDNA: PA の small primitive filter・back-face cull・VGT の vertex reuse を RADV の値に、scene-6500 の GPU の ms | p003 | cleared（2026-10-27 08:34、driver の変更なし、差は NGG の culling） |
| [p005](phase005/phase.md) | RDNA: NGG A（passthrough、wave32 1 つの subgroup） | p004 | cleared（2026-10-27 13:52 Q1 判定、q1611。受け入れ: export の EXEC の wait の修正、1 体 pixel 差 0、6500 体 8 pixel、hang 回復、GPU 41.4 ms） |
| [p006](phase006/phase.md) | RDNA: NGG B（primitive の culling: w の符号・画面外・裏面・小さい三角形） | p005 | in-progress（q1611、2026-10-27 13:55） |
| p007 | RDNA: NGG C（vertex の詰め直しと属性の後回し、wave をまたぐ subgroup）、目標 GPU 約 20 ms | p006 | planned |
| p008〜p009 | RDNA: HTILE、guard band、CPU 側の record（2.3 ms）の短縮 | p007 | — |
- 2026-10-27 04:44 ユーザー（Q1 の問い「NGG の A〜C をどう進めますか？」へのクリック）「A〜C を今進める（推し）」。
| p010 | NVIDIA: GPU の入れ替え（ユーザー）、desktop の計測と 60 fps | RDNA の Phase の全部、P2 | planned |
| p011 | NVIDIA: Linux で約 50 fps の scene での差の計測と 50 fps（p012〜p019 は改善の予備） | p010 | planned |
| p020 | PowerVR（VF2）: model の 50 fps（RDNA で確立した手順、P3） | RDNA の手順の確立（p002 と最初の改善） | planned |
| 最後 | 規約の全文の見直し | 実装の全部 | — |

## VF2（p020）の実機

- 2026-10-26 23:09 ユーザー「VF2はssh kei@10.0.30.2で、パスワード（略）です。VF2はSwitchbotで電源を入れられます。」→ VF2 は ssh kei@10.0.30.2（password は tree に書かない、Q1 が担当に渡す）。電源は SwitchBot のプラグ（AC276E42B252）で Q1 が `plan/tools/switchbot.py` で入れ切りする（担当は Q1 に頼む）。serial は host の /dev/ttyUSB0。
- 2026-10-26 23:09 ユーザー「VF2には現在、PowerVRドライバのカーネルやファームウェアが入っていないと思うので、SSH経由で更新が必要です。」→ p020 の最初に、main の config-visionfive2.mk（desktop の構成、PowerVR の driver あり）で vmunix と PowerVR の firmware（zedBSD-firmware から）・desktop の package を作り、ssh で VF2 の /boot/boot0/vmunix などを入れ替える（今の vmunix は WS288 の GHCI の kernel、P3 の ac7ee7d64 の物。入れ替える前の物は .old 等で残す）。起動の後に PowerVR の attach と firmware の load を確かめてから計測する。
- WS288 の作業で VF2 の zedbsd.cfg から login=graphical を外したまま（元は zedbsd.cfg.p044orig）。上の計測の決定で login=graphical は要らないので戻さない。

## 未決（その Phase の前に確かめる）

- NVIDIA の Linux の基準の driver（NVIDIA の proprietary か nouveau/NVK か）と、入れる GPU の型番。GPU の入れ替えはユーザー。
- PowerVR の 50 fps の基準: Linux との比較か、50 fps の絶対値か（VF2 の Linux の PowerVR の driver の有無で決まる）。
