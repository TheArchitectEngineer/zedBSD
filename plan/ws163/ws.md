<!-- awesome-plan project=zedbsd record=ws163 -->
# WS163: 数字 6 桁の login

Status: incomplete（2026-10-05 夜のユーザーの決定で **WS172 に吸収**。目標は WS172 p002 が達した物とする。WS172 p002 の QEMU の試験 T1-203 は FAIL（greeter の password の login が起きない）なので、受け入れはまだ。WS172 p002 が cleared になった時に Q1 が completed にする。2026-10-05 Q1 が P1 の completed を直した）
Master: [master](../master.md)
Primary Milestone: MG006

## 由来

ユーザー（2026-10-05）「数字6桁のログイン」

## 結果

- greeter と lock の画面で 6 桁の PIN で login・unlock できる: WS172 p002（[phase](../ws172/phase002/phase.md)）。PIN の hash は `/sbin/passkey` が root だけの
  `/etc/passkey` に置き、sessiond が失敗の数を memory に持つ（5 回で止め、sessiond の起動の後は password で一度入るまで出さない）。sudo・su・SSH・console の
  login は password だけ。Settings の Users の頁で設定・削除（今の password が要る）。
- 途中の mock（2026-10-05、ユーザー「~/.configの中にPINを保存してOKです」）: ws163-p002・p003 で compositor の `pin-store.c` と lock の画面の自前の確かめ、
  Settings の `set_pin`（manager v10）を作り（d9ab018d、T1-194 で lock の画面の PIN を確かめた）、WS172 の決定（判断 P6）で p002 が mock を外した
  （`pin-store.c` と WS163 の試験を削除、`set_pin` は sessiond の `ENROLL pin` へ、`~/.config/keiland/pin` は取り込まず消す）。

## Phase（記録。directory は削除した。git の履歴に残る）

| Phase | 内容 | 結果 |
| --- | --- | --- |
| ws163-p001 | 要件と設計（第 1 版 q733、mock の設計 §9 q769） | 設計は WS172 p001 に置き換わった（G1 は問わない） |
| ws163-p002 | mock: `~/.config/keiland/pin` と lock の画面の PIN | 実装・host 試験・T1-194 済み。WS172 p002 が外した |
| ws163-p003 | mock: Settings の `set_pin` | 実装・host 試験済み。protocol の口は残り、WS172 p002 が sessiond に繋ぎ替えた |
| ws163-p004 | 全文規約の見直し | canceled（WS172 p006 が持つ） |

## 制限・移管

- 受け入れの証拠は WS172 p002 の T1（未了の間は QEMU の証拠が無い）。実機の UAT は WS172 の範囲。
