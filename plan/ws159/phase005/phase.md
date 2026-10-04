<!-- awesome-plan project=zedbsd record=ws159-p005 -->

# ws159-p005: 実機（Latitude 5330）の UAT

Status: planned（2026-10-05 P1 generation17: 手順書と image の config を用意した。image の build と UAT は Q1 が朝にユーザーと行う）
Disposition: normal
Parent: [WS159](../ws.md)
Queue: Q1 の指示（2026-10-05、UAT の準備）

この UAT は WS159 の受け入れ（touchpad の driver・割り込み・規則）に加えて、同じ夜に入った物をまとめて実機で見る:
WS132（電池・AC・蓋・電源ボタンの事象、bar の電池）、WS142 p002（Windows キー）、WS099 p032（network の詳細）、
WS160（su・sudo・passwd、Settings の Users）、WS089 p027（About の版の名前）、q722（bar の電池の場所）。
結果は項目の番号ごとに ok・NG・未実施で記録し、NG は画面の写真か log の行を添える。

## 0. image（Q1）

- config: [`plan/ws159/tests/config-amd64-uat.mk`](../tests/config-amd64-uat.mk)。release の config（root は lock、user は kei、password は `kei`、sshd あり）を開発 build にし、事象の reader（`systemevents`）を足しただけ。
- build（main の最新で）: `plan/tools/guest/test-image.sh --no-harness plan/ws159/tests/config-amd64-uat.mk build/uat-0505`。出力は `build/uat-0505/hdd-image.img`。USB に書いて 5330 を UEFI から起動（[hw5330 の手引き](../../tools/hw5330/README.md) の 2 節、書き方はユーザー）。
- 証拠の回収（任意、UAT の後に）: 5330 の address が分かれば centris から `plan/ws159/tests/uat-collect.sh HOST [OUTDIR] [PASSWORD]`（kei で SSH、root の log は sudo）。`OUTDIR/summary.txt` に下の項目が見る行がまとまる。password を変えた後は 3 番目の引数に新しい password。
- 端末の操作は Terminal の app（kei）で行う。

## 1. 起動と touchpad の driver（WS159 p002・p003・p006）

| # | 操作 | 期待 | 証拠（uat-collect の summary） |
| --- | --- | --- | --- |
| 1.1 | 起動して自動 login を待つ | desktop が出る。touchpad で pointer が動く | — |
| 1.2 | （log）kernel の log | `lpss-i2c` の attach、`i2c-hid: \_SB.PC00.I2C1.TPD0 … reads on its interrupt (\_SB.GPI0 pin 327)`、`intel-gpio: pin 327 interrupts through irq 14`。**`irq 14 fired for no pad` が無い**。`reads on its line (…; interrupt: N)` なら割り込みは使えず 4 ms の監視（NG として N を記録） | touch pad (kernel) |
| 1.3 | （log）compositor | `ZWL INPUT device=/dev/input/eventN kind=touchpad abs=0 resolution=12,12` | touch pad (compositor) |
| 1.4 | pointer を動かし続け、止める | 遅れ・飛びが無い。止めた後に pointer が勝手に動かない。PS/2 の pointer が二重に動かない | — |
| 1.5 | 数分触らずに置き、また触る | すぐ反応する（割り込みの取りこぼしが無い） | — |

## 2. touchpad の規則（BUG-166・167・178・190・156、ws159-p004）

| # | 操作 | 期待 |
| --- | --- | --- |
| 2.1 | ゆっくり・速く 1 本指で動かす | ゆっくりは細かく、速いと大きく（5〜15 px/mm） |
| 2.2 | 1 本指で軽く tap | 離した時に click（Home の icon・button が押される）。BUG-190 |
| 2.3 | 窓の title bar を tap してすぐ触れ直して動かす（tap-drag） | 窓が付いて動き、指を離すと止まる。BUG-166・178 |
| 2.4 | Files の folder を 2 回 tap | 開く（double click） |
| 2.5 | 2 本指で tap | 右 click（context menu） |
| 2.6 | pad を押し込む（物理の click） | 左 click。押した瞬間に pointer が動かない（1 mm の遊び）。BUG-167 |
| 2.7 | 押し込んだまま動かす | drag（title bar なら窓が動く） |
| 2.8 | 2 本指を置いて押し込む | 右 click |
| 2.9 | Settings・Files で 2 本指で上下に滑らせる | scroll（指の向きに中身が動く natural）。BUG-156 |
| 2.10 | 3 本指で tap | 今は中 click（WS142 p003 で switcher に変わる予定、D1。この UAT では中 click のまま） |

## 3. 電池・AC・蓋・電源ボタン（WS132 p002・p003、q722）

| # | 操作 | 期待 | 証拠 |
| --- | --- | --- | --- |
| 3.1 | 右上の bar | 電池の icon に残量（中の長さ）。AC を挿していて充電中なら右に「+」。電池があるので network の icon は詰めない（電池の場所がある） | — |
| 3.2 | Terminal で `systemevents -p` | `power lid=1 ac=1 battery=NN charging=1`（NN は Linux の残量と近い値） | state |
| 3.3 | Terminal で `systemevents -c power,lid,ac,battery -t 120000` を動かしたまま: AC を抜く → 挿す → 蓋を閉じて 5 秒後に開ける → 電源ボタンを短く押す | 1 行ずつ: `event N ac change 0 ac charging=0`・`… ac change 1 …`、`event N battery change NN battery0 charging=0/1`、`event N lid change 0 lid …`・`… lid change 1 …`、`event N power press 1 power-button -`。**電源ボタンで電源は切れない**（今は記録だけ、D1 の動作は WS132 p008） | session の `ZWL EVENT power button`・`ZWL EVENT lid closed/open`・`ZWL POWER source=…` |
| 3.4 | AC を抜いて数秒後の bar | 「+」が消える。挿すと戻る | — |
| 3.5 | 蓋を閉じて開けた後 | 画面が戻る（蓋で suspend はしない。D2 は WS132 p008） | — |
| 3.6 | USB メモリを挿して抜く（`systemevents -c disk,usb -t 60000`） | `usb add`・`disk add … removable=1`・`disk remove`・`usb remove` | — |

## 4. Windows キー（WS142 p002）

| # | 操作 | 期待 |
| --- | --- | --- |
| 4.1 | Windows キーを押して離す | App Home が開く |
| 4.2 | もう一度 | 閉じる |
| 4.3 | Windows+Tab | Wiseview（Home は開かない） |
| 4.4 | Windows を押したまま touchpad で click、または 1 秒以上押して離す | Home は開かない |
| 4.5 | Terminal で Windows キーを押す | Terminal に何も入らない（D9: client に渡さない） |

## 5. network の詳細（WS099 p032）

| # | 操作 | 期待 |
| --- | --- | --- |
| 5.1 | Wi-Fi に繋ぐ（Settings → Wi-Fi） | 繋がる |
| 5.2 | 右上の Wi-Fi の icon を Alt を押しながら click | menu ではなく詳細: Wi-Fi、Network（SSID）、Interface、Connected、IPv4 address・Subnet mask・DNS、MAC address、MTU、Signal（dBm）、Received・Sent |
| 5.3 | 詳細を開いたまま web を開くなどして通信する | Received・Sent が 1 秒ごとに増え、`(… KB/s)` が出る |
| 5.4 | Terminal で `ifconfig -a` | 5.2 の IPv4 address・MAC と同じ |
| 5.5 | どこかを click か Esc | 閉じる。普通の click は今まで通り menu |

## 6. su・sudo・passwd と Settings の Users（WS160）

| # | 操作 | 期待 |
| --- | --- | --- |
| 6.1 | `sudo id -u`（password は `kei`） | `0` |
| 6.2 | `sudo id -u` で間違った password を 3 回 | 拒まれる（各 2 秒待つ） |
| 6.3 | `su`（何を入れても） | `su: authentication failed`（root は lock） |
| 6.4 | `passwd`: 今の `kei`、新しい password を 2 回（8 文字以上） | `passwd: the password of kei is changed.`。7 文字以下は理由を出して拒む |
| 6.5 | log out して、新しい password で login | 入れる。古い `kei` では入れない |
| 6.6 | Settings → Users | 「Your account」に kei、「Password」に 3 つの欄 |
| 6.7 | Users で今の password（6.4 の物）と新しい password を 2 回、Change Password | 「Your password is changed.」。欄が空になる |
| 6.8 | Users で今の password を間違える | 「The current password is wrong.」 |
| 6.9 | Users で新しい password を 7 文字以下に | 「The new password is not accepted …」 |
| 6.10 | 最後に password を `kei` に戻す（`passwd` は 8 文字未満を拒むので `sudo passwd kei` で） | 戻る（次の試験のため） |

## 7. About（WS089 p027）

| # | 操作 | 期待 |
| --- | --- | --- |
| 7.1 | Settings → About | Operating system が `Kei/zedBSD 1.0.0 Beta 1 …`（`/etc/os-release` の PRETTY_NAME）、Kernel の行は uname |

## 8. 記録

- 項目ごとの ok・NG・未実施と、NG の写真・log の行を、この phase.md の下に「結果（2026-10-0x）」として Q1 が書く（または P1 に渡す）。
- WS159 の受け入れ: 1・2 が ok で、2.10 は WS142 p003 の後に再確認。
- 他の WS の Phase（WS132 p002・p003、WS142 p002、WS099 p032、WS160 p001・p002、WS089 p027）の実機の確認は、各 phase.md の「実機: 未実施」をこの結果で埋める（Q1）。
- 全文の規約の確認（p005 の後半）は、UAT の結果を受けて別に行う。

## UAT の image（2026-10-05 04:29 Q1）

`plan/tools/guest/test-image.sh --no-harness plan/ws159/tests/config-amd64-uat.mk build/uat-0505` を main 7debc4ba（ws159-p006 の割り込み・i2c-hid の thread_start の直し・WS160 p001/p002・q722・WS132 p003 を含む）で build、exit 0、`check-amd64-native-image: OK`。`/home/awe/zedBSD-claude1/build/uat-0505/hdd-image.img`（2216689664 byte、sha256 830d2498f85213e8bda5ff0a0d9d1d938f05df92c57a13ebafdc5b1b115206c6）。QEMU の boot の確認は T1-124。video player（ws122）と release の license の直しはこの image に入っていない。

注（2026-10-05 Q1）: ws142-p003（d1a57d3a）が入った image では、項目 2.10 の 3 本指の tap は中 click でなく TAP3（switcher の gesture、UI は ws142-p005 まで log だけ）。中 click は 3 本指の押し込み（D1）。build/uat-0505 は 7debc4ba なので旧の動作（中 click）。

追加の確認（2026-10-05 Q1、ws113-p013）: main 780764c3 以降の image なら、compositor が一度描いた後に `backlight-probe`・`backlight-probe 30`・`backlight-probe 100` で内蔵の panel の明るさが変わるのを目視（描く前は EBUSY が正しい）。build/uat-0505 には入っていない。
追加の確認（2026-10-05 Q1、ws113-p012）: main caa7181e 以降の image なら `display-control` で REFRESH が 60 Hz 程度で進むこと、power OFF で panel が暗くなり ON で戻ること。
追加の確認（2026-10-05 Q1、ws132-p008 の蓋、main 4b4397ad 以降の image）: 蓋を閉じて画面が消える、5 秒で開けて password 無しで desktop、15 分を超えたら lock の画面、Super+L の後に閉じて開けたら lock の画面のまま（plan/ws132/phase008/phase.md）。
追加の確認（2026-10-05 Q1、main 1ba85bfd 以降の image）: BUG-173: Terminal で長い日本語の行を履歴から呼び出し、左右・Backspace で prompt が消えず崩れないこと。BUG-179: title bar の double click から中身が最大の大きさで描き直されるまでの時間（i915、目標 0.1〜0.2 秒）を目視・動画で。
追加の確認（2026-10-05 Q1、BUG-175、main 98f89042 以降）: Terminal を 20 以上開いて全部起動すること。

## 9. sleep の device の suspend・resume（WS052 p004・p009）

| # | 操作 | 期待 | 証拠 |
| --- | --- | --- | --- |
| 4.1 | 内蔵の panel で kei で login し、Terminal と、動き続ける app（Gears など）を開いておく。Terminal で `sudo sleepctl devices` | 画面が一瞬消えて**同じ panel に同じ session が戻る**（greeter に戻らない、login し直さない）。Gears が動き続け、Terminal に打てる。Terminal の行は `sleep result=21 resume=0 device=pci 0000:00:XX.X NAME`（21 = EOPNOTSUPP、NAME は suspend の口の無い driver。p005 の後は `result=0`）。`device=` が i915 より前（`0000:00:02.0` より前の bus 0 の番号、または bridge の先）なら i915 は通っていないので、その旨を記録 | `sudo dmesg` の `i915: park: the GT may idle`・`i915: DC9: entered`・`i915: suspend: the hardware is down (interrupts off, display in DC9)`・`i915: DC9: left`・`i915: dmc: program loaded again after the resume`・`i915: resume: N GGTT entries written again`・`i915: resume: the hardware is back`・`i915: unpark: the GT serves again`・`i915: resident display: ended PASS`（窓を出た分）、`nvme: suspended`・`nvme: resumed`、`pci: suspend of … failed (error 21)` |
| 4.2 | 4.1 を続けて 5 回 | 毎回同じ。disk の読み書き（Files で file を開く・保存）と network（Browser で頁を開く）が続く | 各回の dmesg の `system: sleep (devices): result … resume …` |
| 4.3 | 4.1 で画面が戻らない、または Gears が止まる | 失敗。電源ボタンの長押しで切る前に、可能なら SSH で `dmesg` を取る（`i915: resident display: the panel did not come up`・`i915: resume: a step failed`・`i915: DC9: not entered/left`・`i915: park: the worker did not park` を探す） | dmesg |
| 4.4 | （HDMI の monitor がある時）firmware が HDMI を点けた起動（HDMI を挿して電源を入れる）で 4.1 | 画面が**HDMI に**戻る（panel に移らない） | dmesg の `i915: display output: HDMI on DDI B …`・`i915: resident display: ended PASS` |

未実施にする項目（p006 の後）: sleep の間に HDMI を抜いて内蔵の panel に移る（決定 4）は、devices だけの mode では sleep が一瞬なので試せない。S0i3 に
入れるようになってから（p006）試す。RC6 の residency の増加と SLP_S0 も p006 の後。

