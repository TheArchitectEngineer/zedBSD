<!-- awesome-plan project=zedbsd record=ws039 -->

# WS039: AMD RDNA GPU対応

<!-- awesome-plan-current:start -->
Status: completed（2026-10-27 P3 の精査 q1608。ユーザー「…masterとWSを修正して、completedやcanceledにしてください。」: amdrdna は main、RX 5600 XT で Keiland の desktop が描ける（p009 ユーザーの目視「RDNAは黄色の問題が解決しました。目視です。」）。速さは WS291）
<!-- retained-after-completion --> `tests/` は未完の WS291（P2、AMD の速さ）が host 試験に使い、その場で広げている（plan/ws291/phase005/phase.md 20 行「Q1 の了解（2026-10-27）: plan/ws039/tests の GFX10 の interpreter…をその場で広げてよい。」）。plan/tools/ への移動は WS291 と合わせて Q1 が決める。
Completed: 2026-10-27（q1608 の精査）
Primary Milestone: MG006
Related Milestones: なし
Objectives: O2
Parent: [Master](../master.md)
Queue: なし
Resume point: なし（新しい要求は新しい WS として立てる。この WS は再開しない）
<!-- awesome-plan-current:end -->

## 目標

AMD RDNA 世代の GPU の driver **amdrdna**（`src/drivers/gpu/amdrdna/`）を作る。最初の対象は RX 5600 XT（Navi 10、RDNA1、gfx1010）、最初の到達点は「デスクトップが描ける」。RDNA2・3 は認識だけ。設計は [design.md](design.md)（改訂 2、16 節の表が Phase の正）、review は [design-review.md](design-review.md)・[design-review-2.md](design-review-2.md)、license の監査は [amdrdna-license-audit.md](amdrdna-license-audit.md)・[amdrdna-compiler-license-audit.md](amdrdna-compiler-license-audit.md)、HAL の外の口の案は [proposed/acpi-table-lookup.diff](proposed/acpi-table-lookup.diff)。

## ユーザーの決定

- 2026-10-05: ベータ4 以降（「WS037, WS044,WS048,WS141, WS112, …, WS039, …は、ベータ4以降としてください。」）、要検討でブロック。2026-10-15 に解除して開始。
- 2026-10-15: ユーザー（原文）:「今日この後、AMD RDNAのGPUが届きます。amdrdnaドライバを作成します。すでにWSはあるはずです。実機が届くまで、NVIDIAのときと同じような手順でMesaベースでコードを作成し、動作確認なしで書き進めます。GPUが届いたらテストを開始します。GPUはRX 5600 XTです。この作業はまず計画を立ててください。NVIDIAのときの作業を参考に計画を立てます。まずは計画だけお願いします。」
- 2026-10-15（原文）: 「参考の source の扱いですが、値は事実として使い、名前・構造・comment は写さない、をこの WS にも広げます。」「RX 5600 XT を挿す機械と、試験の道は、ベアメタルでOKです。」「最初の到達点を「デスクトップが描ける」までにします。動画（VCN）は後にします。」「まず RDNA1 だけを動かし、RDNA2・3 は認識だけ先に入れます。」「担当を 2 人（driver と compiler）で並行してよいです。」
- 2026-10-15 夕（原文）: 「もう1つサブエージェントを立てます。AMD RDNAドライバを書く既存WSの作業をします。このWSにも参照元などに関するガードレールをしっかり適用していきます。値の引用は明示すればOK、コード・シンボル名の引用は不可、ハードウェア操作手順の引用は明示すればOK、GPLなコードを引用しても、引用ならライセンスは影響しない、です。」→ [Guardrail](../guardrail.md) の「MIT・GPL の出典のデータの引用」。
- 2026-10-15 夜: 「AMDに集中しましょう。N=1にします。」、「RTX機 10.0.10.3 は RDNA機に変わりました。」
- 2026-10-16: 「進めてください。コールドブートは言ってくれればやります」、「p006, p009, p012の順でどうですか？」、「p013,p014,p015,p016を現在のサブエージェントで継続してください。」、p015 は「TMDS（DVI/HDMI）を先に」。
- 2026-10-16 夜（BUG-331）: 「原因がわからなければ、あとで調べるフェーズを作って、先に進めていいです。」「RDNA機はフリーズはいったんおいておいて、先にフェーズをすべて終わらせましょう。」
- 2026-10-27（q1608）: 「…masterとWSを修正して、completedやcanceledにしてください。」「実機確認だけが残っているWSやPhaseは、古いものはclearedにしてしまいます。」

## 結果

- 設計（p001、改訂 2 と review 2 回、§15 の回答の反映）。
- driver: 骨格と認識 A0・A1・A1v（p002）、BAR0 の窓・GMC・IH A2（p003）、SMU・GMC・IH・fence A3t・A3・A4 Tier S（p004）、CP・KIQ・compute の queue・SDMA・fence・最初の copy A5（p005）、session・pasid・動的な VMID・page table・BO・GPU device A5s（p011）、取り消しと復帰 A5c（p013）、mode set の 1 A10（EDID・AtomBIOS の interpreter、p014）、mode1 reset と設定し直し（p016）、VRAM の 256 MiB の窓の固定の解除（p017）。全て実機 10.0.10.3（RX 5600 XT）で ok（Q1 の判定）。
- display の引き継ぎ A6〜A8（p006、ユーザーの目視「テストは成功です」）、mode set の A11〜A13（p015、実機で ok、DP は host 試験まで）、SMU の fan・thermal Tier F（p012、実機 ok）。
- shader の compiler（p007、SPIR-V → GFX10）と Vulkan の executor（p008、A9・vkquad 11 回 ok）。
- desktop（p009）: compositor が amdrdna で描き、ユーザーの目視「RDNAは黄色の問題が解決しました。目視です。」。AMD の firmware は CI・release の amd64 の image に入る（`config/ci/config-amd64.mk` の `amd-navi-firmware`）。

## 制限・移管

- 描画の速さ（p019）は未完のまま [WS291](../ws291/ws.md)（P2）へ。
- BUG-331（RDNA 機の desktop の停止と reset、p018）は ticket で追う。p018 は planned のまま。
- DP の mode set は host 試験まで（DP の monitor がつながったら実機で）。VCN（動画）・RDNA2・3 の動作は範囲の外。
- 規約の全文（p010）は WS の外の一括の見直しへ。
- 試験（`tests/`）は WS291 が使うので残す（上の retained の印）。

## Phase 一覧

| Phase | 目的 | 結果 |
| --- | --- | --- |
| ws039-p001 | 調べと設計 | cleared（2026-10-15 夜 Q1） |
| ws039-p002 | 骨格と認識 A0・A1・A1v | cleared（2026-10-16 Q1、実機） |
| ws039-p003 | A2（BAR0 の窓・GMC・IH の用意） | cleared（2026-10-16 Q1、実機） |
| ws039-p004 | A3t・A3・A4（Tier S） | cleared（2026-10-16 Q1、実機） |
| ws039-p005 | A5（CP・KIQ・compute の queue・SDMA・fence・copy） | cleared（2026-10-16 Q1、実機） |
| ws039-p006 | A6〜A8（display の引き継ぎ: flip・割り込み・cursor） | in-progress のまま WS を閉じた（実機 ok、ユーザーの目視「テストは成功です」、clearance の判定は未） |
| ws039-p007 | shader の compiler（SPIR-V → GFX10） | in-progress のまま WS を閉じた（host の段 H・I、desktop で使われている） |
| ws039-p008 | Vulkan の executor | cleared（2026-10-16 Q1、A9・vkquad 実機） |
| ws039-p009 | desktop（compositor が amdrdna で描く） | in-progress のまま WS を閉じた（ユーザーの目視で OK） |
| ws039-p010 | 規約の全文の見直し | WS の外の一括の見直しへ（Phase の dir は作られていない） |
| ws039-p011 | A5s（session・pasid・VMID・page table・BO・GPU device） | cleared（2026-10-16 Q1、実機） |
| ws039-p012 | A4 Tier F（SMU の fan・thermal） | in-progress のまま WS を閉じた（実機 ok、clearance の判定は未） |
| ws039-p013 | A5c 取り消しと復帰 | cleared（2026-10-16 Q1） |
| ws039-p014 | A10 mode set の 1（読むだけ＋AtomBIOS の interpreter） | cleared（2026-10-16 Q1） |
| ws039-p015 | A11〜A13 mode set（re-light、別の mode、hotplug） | in-progress のまま WS を閉じた（実機 ok、DP は host 試験まで） |
| ws039-p016 | mode1 reset と設定し直し | cleared（2026-10-17 Q1） |
| ws039-p017 | VRAM の 256 MiB の窓の固定の解除 | cleared（Q1） |
| ws039-p018 | BUG-331 の調べ（desktop の停止と reset） | planned のまま WS を閉じた（BUG-331 で追う） |
| ws039-p019 | desktop の描画の速さ | in-progress のまま WS を閉じた（WS291 へ） |

## 記録の所在

Phase の計画・結果は削除する。git の commit `fb1f6afee` の `plan/ws039/` にある。Queue の履歴は [plan/history](../history/index.md)。

## design.md §15 などへのユーザーの回答（原文の記録）

### design.md 改訂 2 の §15 への回答（2026-10-15 夜 ユーザー、1 件ずつクリック）

| # | 問い | 回答 |
| --- | --- | --- |
| 1 | mode set の時期 | 「desktop の後」 |
| 2 | 出力の順 | 「案のとおり」（DP 先・HDMI 後、別の connector は Keiland の指示の時だけ） |
| 3 | training 中の画面 | 「案のとおり（blank）」 |
| 4 | CTF（温度の危険域） | 「案のとおり」（GPU だけ fault、system は止めない） |
| 5 | Tier S の fan | 「案のとおり（50%）」 |
| 6 | compute の queue の範囲 | 「案のとおり」（kernel の試験と compute 専用の family の最小の経路） |
| 7 | resizable BAR | 「まず256MBで実装します。」（BAR0 の 256 MiB の窓で実装。UEFI の Re-Size BAR・OS の中の resize は後） |
| 8 | 試験の oracle に Linux の atom.c | 「案のとおり」（tarball の取得、host 試験だけ） |
| 9 | ACPI の table を署名で借りる公開の口 | 「口を足す」（p002 で差分、Q1 が確かめて入れる） |
| 10 | mode1 reset | 「案のとおり（後で）」（p016） |
| 11 | control の node /dev/amdrdna0 | 「開発の間だけ」（完成したら消すか config で off） |
| 12 | main に入れる条件 | 「案のとおり」（実機で確かめた段だけ） |
| 13 | 取り消しの間は desktop も止まる | 「この形でよい」 |
| 14 | 10.0.10.3 の Linux で amdgpu を load して dump | 「load してよい」（load の後 zedBSD を試す前に電源の入れ直し） |

- 2026-10-16 未明（ユーザー、クリック）: 10.0.10.3 の UEFI に USB の entry が無い時、`efibootmgr --create` で「zedBSD USB」の entry を足して BootNext（1 回だけ）にしてよい（「足してよい」）。BootOrder は変えない。

- 2026-10-16 朝（ユーザー、クリック）: 試験の image の GOP の大きさは「video=640x480 を外す」→ GOP を monitor の 1920x1080 のままにする（loader の設定だけ、mode set は予定どおり desktop の後）。p002 の実機で、640x480 の GOP は 1920x1080 の timing に DSCL で拡大されていた。

- 2026-10-16（ユーザー）:「AMD の firmware を CI・release の amd64 の image に入れます。」→ p004 の merge で config/ci/config-amd64.mk に amd-navi-firmware。

→ p001 は判断が揃ったので cleared（Q1、2026-10-15 夜）。#7・#11 は design.md の該当の節を p002 の最初に直す。

### design.md §15 #9 の回答（2026-10-15 夕 ユーザー、クリック）
RX 5600 XT は「RTX 機と入れ替え（10.0.30.5）」（i5-13400F の機械で RTX 2070 と入れ替える。nvrtx の実機の試験とは交代）。§15 の他の項目は「個別に見直す」で 1 件ずつ回答（2026-10-15 夕、クリック）:
- #1 firmware: 「userlandfirmwareに入れます。既存のfirnware リポジトリに入れておいてダウンロードしたいですがライセンスは問題ないですか？」→ userland/firmware/ の package。取得元を既存の firmware の repository（どれかは確認中）にする案。LICENSE.amdgpu は改変しない binary の再配布を許す（copyright・許諾・免責の文を同梱、reverse engineering の禁止、輸出規制の注記）。
- #2 PSP の memory training: 「走らせる」（既定の案から変更。GOP の画面の退避と training を設計に入れる）。
- #3 SMU: 「fan の制御も最初から入れる」（既定の案から変更。thermal・fan の feature と pptable の扱いを設計に入れる）。
- #4 ring: 「compute の queue も作る」（既定の案から変更。compute の queue（KIQ か HQD の直接）を設計に入れる）。
- #1 の続き: 「すでにGutHub にzedBSD-firmwareがあり、すべてのドライバはそこからダウンロードしていた認識だったのですが」→ ユーザーが正しい（Q1 の誤り。WS233 で awemorris/zedBSD-firmware から取り、無ければ元の配布元、`userland/firmware/zedbsd-firmware.mk`）。amd-navi も同じ形で zedBSD-firmware に入れ、LICENSE.amdgpu を同梱する（再配布の条件は満たせる読み、法的な最終判断はユーザー）。
- #5 executor: 「既定の案で（写す）」。
- #6 reset: 「WS250 と同じく取り消しと復帰を入れる」（既定の案から変更。終わらない仕事はその session だけ DEVICE_LOST、queue の停止・作り直しで desktop は動き続ける、isolate、必要なら mode1 reset の実験）。
- #7 display: 「mode set も入れる」（既定の案から変更。別の解像度・hotplug まで。AtomBIOS・DML 相当の扱いを設計に入れる。Guardrail の scanout の規則（GOP の出力先以外に driver の判断で scanout を始めない）は守る）。
- #8 takeover: 「既定の案で」（最初は amdrdna.disp.own=1 の opt-in、合格後に既定 on）。
- #10 BAR: 「resizable BAR も使う」（既定の案から変更。PCI の層の resizable BAR の対応を設計に入れる）。
- #11 VMID: 「動的に割り当てる」（既定の案から変更）。
- #12 renderer だけの登録: 「既定の案で（許す）」。
→ design.md の改訂が要る（#2〜#4・#6・#7・#10・#11、firmware の取得元は zedBSD-firmware）。p001 は改訂とレビューの後に cleared。

### 試験の機械（2026-10-15 夜）

ユーザー「RTX機 10.0.10.3 は RDNA機に変わりました。」→ 10.0.10.3（i5-13400F の機械）に RX 5600 XT（`lspci`: Navi 10 `1002:731f` rev ca）。普段は Debian 13 の Linux（`ssh awe@10.0.10.3`、鍵で入れる）。zedBSD は USB で起動する（5330 と同じく efibootmgr の BootNext で 1 回だけ USB から、の手順が使える見込み）。Linux の amdgpu で register の dump を取って照合に使える。
