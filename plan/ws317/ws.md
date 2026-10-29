<!-- awesome-plan project=zedbsd record=ws317 -->

# WS317 hotplugd: hot-plug の device の通知・承認・登録と driver の開始と終了（volumed を置き換える）

- Master: [plan/master.md](../master.md)（2026-10-30 作成）。Primary Milestone: MG005。
- Status: planning（2026-10-30 Q1 が作成。ユーザー「今は取り組みません」）
- 目標: PCIe の hot-plug（USB4・Thunderbolt の tunnel を含む）と USB の device とディスクを 1 つの daemon（hotplugd）で扱い、通知・承認・拒否・
  登録と削除・driver の開始と終了を行う。Keiland の compositor は hotplugd と話し、承認の選択を利用者に出す。

## ユーザーの決定（2026-10-30、原文）

1. 「またhotplugについて、PCIeデバイスを自動で使用開始してしまうと、ホストメモリにアクセスできるというセキュリティの懸念があるという認識で正しいですか？
   ディスクのホットプラグはvolumedで管理していると思いますが、これをdevicedに変更して、PCIe hotplugやディスクも含めて、通知、機器の登録と承認と削除、自動承認、デバイスドライバの開始と終了、を実装するのがいいと思います。Keilandコンポジタにはvolumedと通信する部分があるので、devicedとの通信に変更して、デバイスのアタッチが通知されたとき、承認、今後は自動承認、拒否、今後は自動拒否、みたいな選択を出す仕組みにしたいです。PCIe機器の登録は、再起動したら消えて、必ず再登録が必要な仕組みでいいと思います。USBディスクは、起動パーティション以外はかならず承認が必要という仕組みでいいと思います。」
2. 「USB の keyboard・mouseは許可不要で自動アタッチにするしかないです。USBディスクは、マウントは承認制にして、アタッチは自動でもいいかもしれないです。今後は自動、はなくていいかもしれないです。再起動したら自動承認は消えてOKです。
   volumedはdevicedだとわかりづらいのでhotplugdでもいいです。」
3. 「この内容でよいでしょうか。よさそうならWSに記録しておいて、今は取り組みません。」

## 決まった方針（Q1 のまとめ、上の原文が正）

- 名前: hotplugd（volumed を置き換える）。compositor の volumed との通信を hotplugd との通信に置き換える。
- 既定は拒否を kernel が守る: 承認の前は device を見つけるだけで driver を付けず、PCIe は bus master を切ったまま（hotplugd が落ちていても、起動の途中・ログインの前でも安全）。
  IOMMU（VT-d）で承認の後の DMA も driver の buffer に限る。
- 起動の時からある内蔵の device（hot-plug でない slot）は自動。承認を要するのは hot-plug の口の下（USB4・Thunderbolt の tunnel の root port、ACPI の外向きの口）の PCIe と、USB のディスク。
- USB の keyboard・mouse（HID）は承認なしで自動で attach。
- USB のディスク: attach（driver・block device）は自動、mount は承認制。起動した media（起動の partition、live の USB）は自動。
- 選択は 承認・拒否（「今後は自動」は無くてよい）。承認は再起動で消える（PCIe も USB も再登録が要る）。
- 承認できるのは画面の前でログインしている利用者。lock の間とログインの前は保留して解除の後に出す（Q1 の案、未確認）。
- USB4・Thunderbolt の security level（承認してから tunnel を張る）はこの承認に乗る（[WS316](../ws316/ws.md)）。

## Phase

| Phase | 目的 | 状態 | 依存 |
| --- | --- | --- | --- |
| ws317-p001 | 設計: kernel の既定の拒否（driver の attach の保留、bus master、IOMMU）、hotplugd の protocol、compositor の承認の UI、volumed からの移行 | planning | — |

## Resume point

- 2026-10-30 Q1: 方針を記録。ユーザーの指示で今は取り組まない。
