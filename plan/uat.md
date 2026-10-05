<!-- awesome-plan project=zedbsd record=uat-2026-10-04 -->
# 実機の UAT（2026-10-04 13:00、Latitude 5330）

2026-10-04 ユーザー「実機は本日13時にUATとしてテスト予定です。テスト観点とテスト手順をまとめて、plan/uat.mdにしておいてください。」

S1（2026-10-03、[WS133](ws133/ws.md) の s1-procedure.md・s1-results.md）の後の直しと、10/03〜04 に QEMU だけで確かめた変更を、素の 5330（bare metal）で確かめる。結果は各項目の「結果」の欄に、QEMU の証拠と分けて書く（AGENTS.md「検証」）。FAIL は Bug にして元の WS へ。

## 1. image

| 項目 | 値 |
| --- | --- |
| path | `/home/awe/zedBSD-claude1/build/uat-2/hdd-image.img`（2,216,689,664 byte、**sha256 `f03920c31d83aa093c17de10d28c877e24c3b52808eed7446880691b3612aa64`**、2026-10-04 12:08 に作り直し（emacs の SKK の辞書の path の直し a1d84b8 を含む）: App Home に System Monitor と Emacs の tile（`plan/uat/uat-apps.conf`）、config-uat.mk が emacs・monitor・noto-color-emoji を足す。前の版 `4b643284…cb71f` は tile が無い） |
| 作り方 | `plan/tools/guest/test-image.sh --no-harness plan/uat/config-uat.mk build/uat-2` に build-demo-image.sh と同じ `--file`（apps.conf・壁紙・authorized_keys）と `I915_TEST_VBT=n`。config-uat.mk は demo の構成（その時の CI の amd64 の構成: emacs・clang 入り）に monitor と noto-color-emoji を足したもの。build/uat-1 はユーザーの新しい CI の構成で emacs が無いので使わない |
| boot の行 | `kernel=vmunix` `rootpart=PARTLABEL=zedBSD-root` `swap0=PARTLABEL=zedBSD-swap` `logo=logo.ppm` `login=graphical` `kmsg=quiet` `display=edp`（S1 と同じ。同じ名前の行を 2 回書かない） |
| 利用者 | root / root、kei / kei（kei は起動で自動の login） |
| QEMU の boot-test | 11:45 の image（8c3e51d0…）: Q1 の boot-test PASS（2026-10-04 12:00 頃、複写 `build/uat-2-boot/uat.img`、`build/boot-test/login.png`）。前の image（4b643284…）: T1-084 PASS（2026-10-04 10:57、QEMU KVM・framebuffer・GPU なし。GPU が無いので greeter の後に getty の `login:`。`/home/awe/zedBSD-worktrees/t1/build/t1-084/boot/login.png`）。SSH で `/bin/emacs`・`/bin/monitor` が在る |

## 2. 準備（ユーザー）

1. USB に書く: `sudo dd if=build/uat-2/hdd-image.img of=/dev/sdX bs=4M conv=fsync status=progress`。読み戻して sha256 を比べると確実。
2. 5330 を UEFI で USB から起動。有線は USB の LAN（RTL8156）をつなぐ。USB マウス（Logi Bolt の受信機）も挿す。
3. 起動したら有線の IP を Q1 に伝える（DHCP）。エージェントは `ssh -i plan/tmp/guest/id_ed25519 -o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null root@IP` で入り、log を集める。
4. WiFi の鍵はユーザーが画面で入れる（エージェントは鍵を扱わない）。

## 3. 観点と手順

「誰」: U = ユーザーが画面で操作・目視、A = エージェントが SSH で log・値を読む。

### A. 起動・login・電源（WS131 p005・p006、BUG-119）

| # | 手順 | 見ること | 誰 | 結果 |
| --- | --- | --- | --- | --- |
| A1 | 電源を入れる | 黒い画面や文字の console が出ずに desktop まで（LCD の引き継ぎ） | U | |
| A2 | system bar から Log Out → greeter で kei を選び login | 切り替えで黒い画面が出ない | U | |
| A3 | Log Out → greeter の Shut Down | **電源が切れる**（BUG-119: S1 では画面だけ消えて電源が残った） | U | |
| A4 | （A3 が切れない時）A が SSH で `dmesg` の ACPI と `sysctl` の電源の値を取る | ACPI S5 の経路の log | A | |

### B. 入力（BUG-105・156、ws099-p030）

| # | 手順 | 見ること | 誰 | 結果 |
| --- | --- | --- | --- | --- |
| B1 | USB マウス（Logi Bolt）を動かし、クリック・ホイール | 動く（BUG-105） | U | |
| B2 | タッチパッドで 2 本指のスクロール | スクロールする（BUG-156） | U | |
| B3 | Files の窓のタイトルバーの**検索欄**を押してドラッグ | **窓が動く**（p030） | U | |
| B4 | 検索欄をクリック → もう一度ドラッグ | 1 回目でフォーカスとキャレット、2 回目のドラッグで文字が選ばれる | U | |
| B5 | タイトルバーの**メニューの項目**からドラッグ／クリック | ドラッグで窓が動く、クリック（離した時）で menu が開く | U | |
| B6 | 閉じるボタン | 押した時に閉じる | U | |
| B7 | B3〜B5 をタッチパッドの指（タップとドラッグ）で | 同じに動く | U | |

### C. WiFi（AX211、BUG-157・145・158、WS131 p011、ws005-p031）

| # | 手順 | 見ること | 誰 | 結果 |
| --- | --- | --- | --- | --- |
| C1 | system bar の network の menu で AP を選び、鍵を行の中の欄に入れて Join | 接続する（鍵の欄が AP の行の中、BUG-160） | U | |
| C2 | わざと**間違えた鍵**で Join | 「鍵が違う」系の文言（**Network is down ではない**、BUG-157） | U | |
| C3 | Settings の Wi-Fi の頁から Join | 接続する、エラーの文言が適切 | U | |
| C4 | 接続の後に A が `net show`・`ifconfig`・ping | DHCP の lease が取れる（BUG-145） | A | |
| C5 | WiFi を off → on | 自動で再接続 | U | |
| C6 | **WiFi を未接続のまま** 10 分ほど操作・放置 | フリーズしない（BUG-158 の疑い: WiFi が未接続の時） | U | |
| C7 | 有線（RTL8156）を抜く → 挿す | 外れて、挿すと取り直す | U・A | |

### D. 音（BUG-161・153、WS131 p004・p011）

| # | 手順 | 見ること | 誰 | 結果 |
| --- | --- | --- | --- | --- |
| D1 | 画面右上の音量を 50% にクリック | 50% のまま（100% に戻らない、BUG-153） | U | |
| D2 | Settings の Sound の slider を動かし離す | 確認の音、system bar と値が一致 | U | |
| D3 | ミュート → 解除 | 音が止まる・戻る | U | |
| D4 | 音量を変えて Log Out → login | 音量が戻る（session の終わりに保存、BUG-161）。操作中に desktop.conf が変わらないことは A が確かめる | U・A | |

### E. 設定（WS135、BUG-152）

| # | 手順 | 見ること | 誰 | 結果 |
| --- | --- | --- | --- | --- |
| E1 | Settings の Wallpaper の頁を開く | **止まらずに**開く（BUG-152: S1 で 10 秒止まった）、Lakeside・Birch-Lake がある | U | |
| E2 | 壁紙・窓の透明度・pointer の速さ・key の repeat を変える | すぐ効く（1 秒待たない） | U | |
| E3 | Log Out → login | E2 の設定が残る | U | |

### F. 標準 app（WS127 p007・WS128 p007）

| # | 手順 | 見ること | 誰 | 結果 |
| --- | --- | --- | --- | --- |
| F1 | Files: USB メモリを挿し、その中の file を Trash へ → Put Back | USB の中の Trash（`.Trash-1000`）に入り、戻る（ws127-p003） | U | |
| F2 | Files: file をドラッグし、別の tab の上で待つ | tab が切り替わり、落とせる（ws127-p006） | U | |
| F3 | Files: PDF の入った folder | 1 頁目の thumbnail（ws127-p004） | U | |
| F4 | Files の操作の速さ（多い folder の scroll、開閉） | 体感の速さ（S1 との比較） | U | |
| F5 | Image Viewer: Move to Trash・Open With・F5 で slideshow | 動く（ws128-p005） | U | |
| F6 | Terminal: Ctrl+Shift+F で検索、View > Theme > Light、font の大きさを変えて閉じて開く | 検索・theme・大きさの保存（ws128-p006） | U | |
| F7 | Terminal で `emacs /tmp/a.txt`（または App Home の Emacs の tile）→ 書いて保存 → 終了 | **/bin/emacs が起動**（ws129-p012）、日本語の IME で入力 | U | |
| F8 | Text Editor・Terminal で日本語の IME で連続して入力・確定 | 確定のたびに止まらない（BUG-143）、変換中の文字の大きさ（BUG-139） | U | |
| F9 | Text Editor・Terminal で IME を off にして速く打つ | 入力の遅れの体感（QEMU では約 300 ms。実機で 500 ms 級が残るか） | U | |
| F10 | `/bin/sh` で上下の矢印 | 履歴（BUG-103） | U | |

### G. System Monitor と GPU（WS134、BUG-120・159）

| # | 手順 | 見ること | 誰 | 結果 |
| --- | --- | --- | --- | --- |
| G1 | App Home の System Monitor の tile で開く（11:45 の image。前の image なら Terminal で `monitor &`） | CPU・memory・disk・network が本物の値で動く | U | |
| G2 | A が monitor の log の `ZMON FRAME fps=` を読む | **fps 15 以上**（ws134-p003 の判定は実機の値、2026-10-04 Q1） | A | |
| G3 | A が `sysctl hw.gputelemetry` | i915 の busy と周波数が出る（ws134-p007） | A | |
| G4 | 窓を 30 個以上開く（Terminal を多数） | GPU の object の枠で落ちない（BUG-120） | U | |
| G5 | バッテリー駆動にして数分 | 描画が 5 fps に落ちないか（BUG-159、観察） | U | |
| G6 | System Monitor の操作（タップ・長押し・スワイプ・ピンチ） | 動く（ws134-p004） | U | |

### H. 終わり

| # | 手順 | 見ること | 誰 | 結果 |
| --- | --- | --- | --- | --- |
| H1 | A が `dmesg`・各 log（zdesktop・sessiond・networkd）を集めて保存 | 証拠 | A | |
| H2 | Shut Down | 電源が切れる | U | |

## 4. 記録

- 結果は上の表の「結果」の欄と、`plan/ws133/` の S2 の記録（必要なら `s2-results.md` を作る）に。
- 実機で FAIL の項目は Bug の ticket に「実機」と書いて元の WS へ。QEMU だけの証拠と混ぜない。

## 5. 結果（2026-10-04 13 時〜、実機 Latitude 5330、image sha256 f03920c3…、ユーザーの報告）

| # | 結果 | ユーザーの記録 | Bug |
| --- | --- | --- | --- |
| A1 | **NG** | desktop は起動。1 分放置でフリーズ → 再起動。WiFi 未接続だとフリーズ、WiFi 接続で問題なし。WiFi 未接続の時の bug と確認 | BUG-158（再現: 実機、未接続で約 1 分で kernel のフリーズ） |
| A2 | OK | | |
| A3 | **NG** | Shutting Down から画面が黒くなり、電源が切れない。手動で電源を切った | BUG-119（再現） |
| A3 の後 | 観察 | 再起動で WiFi は自動で接続したが、それまでの約 10 秒「Network service is not …」（network の service が使えない）と表示 | [BUG-176](bugs/BUG-176.md) |
| B1 | 未実施 | USB マウスが手元に無い（後で） | |
| B2 | **NG** | タッチパッドの 2 本指のスクロールができない（Browser で確認） | BUG-156（再現） |
| B3 | **NG（デグレ）** | タッチパッドで窓の移動ができない。タッチしてクリックしてから別の指で動かすとドラッグできた。2 回タップして動かすのもできた。押下（トラックパッドの押し込み）で動かすかは仕様の検討漏れ | [BUG-166](bugs/BUG-166.md)（仕様の決めが先） |
| B4 | OK（条件付き） | B3 と一緒に仕様の調整が必須 | BUG-166 |
| B5 | OK（条件付き） | 同上 | BUG-166 |
| B6 | OK（条件付き） | 同上。**トラックパッドのクリック（押し込み）が認識されない** | [BUG-167](bugs/BUG-167.md)（押し込み）、BUG-166 |
| C6 | **NG** | 1 分でフリーズ。kernel のフリーズ | BUG-158 |
| C2 | **NG** | 間違えた鍵で `Could not join (Network is unreachable)` | BUG-157（再現、文言） |
| C1 | OK | | |
| C4 | OK | DHCP OK | BUG-145（実機で OK） |
| C3 | OK | | |
| C5 | OK | WiFi off → on で 8 秒で自動接続 | |
| C7 | **NG** | 起動の後に USB LAN を挿すと ue0 は down のまま。WiFi を off にしても down のまま。そのまま ue0 を抜くと、system bar の Ethernet のメニューに wlan0 が出る。ue0 を挿し直しても wlan0 が出たまま | [BUG-168](bugs/BUG-168.md)・[BUG-169](bugs/BUG-169.md)（仕様ではない） |
| D1 | **NG** | 100% には戻らず調整できた。しかし slider をドラッグすると、しばらくフリーズし、放置で回復。連続で確認の音を鳴らそうとしている疑い | [BUG-170](bugs/BUG-170.md) |
| D2 | **NG** | Settings も、クリックは OK、ドラッグでフリーズ | BUG-170 |
| D3 | OK | ミュート → 解除 | |
| D4 | OK | | BUG-161（実機で OK） |
| E1 | OK | | BUG-152（実機で OK） |
| E2 | OK（気付き） | opaque にしても窓が不透明にならない | [BUG-171](bugs/BUG-171.md) |
| E3 | OK | | |
| F1 | 未実施 | USB メモリが起動用の 1 本しかない | |
| F2 | 未実施 | 「タブ」が何か分からなかった（手順の説明の不足） | |
| F3 | OK | PDF の試験の data が image に無く、Notes で PDF を作って表示 | 気付き: UAT の image に見本の file（PDF・PNG）が無い |
| F4 | OK（一部） | scroll bar は OK。タッチのスクロールができないので慣性スクロールは未テスト | BUG-156 |
| F5 | 未実施（一部） | PNG が無くテストできず。PPM は Files の中で preview の窓が出た | 気付き: 見本の file が無い |
| F6 | OK | | |
| F7 | OK | emacs の保存・終了・SKK | |
| F8 | OK | | BUG-143・BUG-139（実機で OK） |
| F9 | OK（気付き） | IME を off にした入力は OK だが、key のリピートが安定しない（表示が安定しないだけかも） | [BUG-172](bugs/BUG-172.md) |
| F10 | OK（NG あり） | 履歴は出るが、長めの日本語の文字列が履歴にある時、表示に改行が入り、履歴が行頭から表示されて prompt が見えなくなる | [BUG-173](bugs/BUG-173.md) |
| G1 | OK | System Monitor の本物の値 | |
| G2・G3 | 未実施 | WiFi だと SSH できない（有線は C7 の不具合で使えない） | [BUG-174](bugs/BUG-174.md) |
| G4 | **NG** | Terminal を多数開くと errno=8 で起動しなくなる | [BUG-175](bugs/BUG-175.md)（BUG-120 の関連を調べる） |
| G5 | 未実施 | | BUG-159 |
| G6 | OK（一部） | タップは OK、スワイプはトラックパッドで動作しない | BUG-156 と同じ根の可能性 |
| G2 | **OK** | （Q1 が SSH で、2026-10-04 14:07、AC 電源）System Monitor を 20 秒動かして `ZMON FRAME fps=21.0〜21.4`（目標 15 以上）。submit_ms 約 11、callback_ms 約 31 | ws134-p003 の実機の判定: PASS |
| G3 | OK | `hw.gputelemetry`: `driver=i915 valid=0xd busy_ns=3186129731 cur_mhz=0 req_mhz=100 min_mhz=100 max_mhz=1200`（idle で cur 0） | ws134-p007 の実機 |
| H1 | 済み | dmesg・/var/log（messages・sessiond・greeter）・session.log・ps・sysctl を [plan/uat/2026-10-04/](uat/2026-10-04/) に（元は `build/uat-logs/1407/`） | |

### Q1 が log で気付いた点（2026-10-04 14:07）

- **[BUG-165](bugs/BUG-165.md): `ACPI: DSDT Dell Inc stopped at offset 0x1c89b (error 13)` → `acpi: the DSDT did not load (error 13)`**。DSDT が読めていない。Shut Down で電源が切れない（A3、BUG-119: `\_S5` は DSDT にある）と、タッチパッド（I2C HID は ACPI で見つける）の 2 本指・押し込みの不具合（B2・B6・G6、BUG-156）の共通の根の候補。最優先で調べる。
- `ZWL STARTUP step=wallpaper ms=7172`: 起動の時の壁紙に約 7.2 秒（WS138・WS139 の項目）。
- `usb1: port 10 enumeration failed (3)`。
- 有線（ue0）は起動の時から挿すと 10.0.30.3 で up（C7 は後から挿した時だけの不具合）。
| G5 | OK（再現せず） | （ユーザーが AC を抜いた直後に Q1 が SSH で計測、14 時過ぎ）System Monitor の `ZMON FRAME fps=21.5・14.4・22.4`（1 回だけ 14.4、submit_ms 17・callback_ms 56）。5 fps への低下は出ない。`sysctl` に battery・AC の項目が無い（DSDT が読めていないため電源の状態を知らない） | BUG-159（今回は再現せず） |
| H1 の追加 | 済み | Shut Down の間の log を SSH で連続に取った（[shutdown-watch.txt](uat/2026-10-04/shutdown-watch.txt)）: 05:18:15 compositor の正常な終了（`ZWL EXIT error=0`、音量 77 と設定を保存）、05:18:21 i915 の display の lease を返す、05:18:23 process は 10、その後 sshd の停止で接続が切れた。kernel の最後の電源を切る段は見えない | |
| H2 | **NG** | LCD はオフになるが、ファンが回り続け電源が切れない。電源ボタンの**長押しは要らず、1 回押すだけで切れた**（OS が止まった後に firmware が電源ボタンを扱っている形） | BUG-119（再現。DSDT が読めない（`\_S5` は DSDT）ことが原因の候補） |

### UAT のまとめ（2026-10-04、Q1）

- 実機で OK を確かめた: BUG-145（DHCP）・152（Wallpaper の頁）・161（音量の保存）・143・139（IME）、emacs・SKK、System Monitor の本物の値と fps 21（AC・バッテリー）、i915 の telemetry。
- 再現した: BUG-158（WiFi 未接続で約 1 分で kernel のフリーズ）、BUG-119（電源が切れない）、BUG-156（タッチパッドの 2 本指・スワイプ）、BUG-157（間違えた鍵の文言）。
- 新規（BUG-165〜176、[Bug Board](known-bugs.md)）: DSDT が読めない（error 13）、タッチパッドの窓のドラッグ（デグレ）と押し込みのクリック、後から挿した USB LAN が up しない、Ethernet のメニューの wlan0、音量の slider のドラッグでのフリーズ、opaque が不透明にならない、key のリピートの不安定、sh の履歴の全角の表示、WiFi の address への SSH、Terminal 多数で errno=8（ENOSPC）、起動の直後の「Network service is not …」の表示、起動の時の壁紙 7.2 秒。
- 未実施: B1（USB マウス）、F1（USB メモリ）、F2（タブ）、F5（PNG の見本が無い）。次の UAT では見本の file（PDF・PNG・JPEG）を image に入れる。

## uat-3（2026-10-04 夜、BUG-158 の ad-hoc UAT）

- image: `/home/awe/zedBSD-claude1/build/uat-3/hdd-image.img`（2,216,689,664 byte、**sha256 `ada4492e004dababea62e89388e377d4f47d94f0c3cc60f21ea2d55fa5469c08`**、20:55）。source は main `efe846a`（BUG-158 の修正 97543a4（bus master off の順）と acb4afa（AX211 の command の write pointer を 16 bit の連番に、firmware の SW_ERROR の原因）、kernel.log の永続化、WS049 の ACPI の修正（BUG-165 の DSDT、SCI・event thread）を含む）。
- 作り方: `plan/tools/guest/test-image.sh --no-harness plan/uat/config-uat.mk build/uat-3` に `--file /etc/keiland/apps.conf=plan/uat/uat-apps.conf`・壁紙（`/usr/share/keiland/wallpaper.ppm` と `wallpapers/*.ppm`）・`/root/.ssh/authorized_keys`（guest harness の鍵）、`I915_TEST_VBT=n ZEDBSD_TEST_IMAGE_TAG=uat-3`。compiler の warning は外部 Noct の 1 件（interpreter.c の -Wreturn-type、既知）と submake の jobserver の 1 件。
- QEMU の boot-test: PASS（Q1、複写 `build/uat-3-boot/uat.img`、`build/boot-test/login.png`、GPU なしなので greeter の後に getty の `login:`）。
- 実機で見る観点（BUG-158）: WiFi を未接続（保存の profile 無し、または AP の圏外）のまま desktop で 10 分以上放置して止まらないこと。前は約 1 分で全停止。止まった時は再起動の後に `/var/log/kernel.log.old` を見る（panic・fatal の行と AX211 の recovery の行）。あわせて DSDT の読み込み（BUG-165）、Shut Down で電源が切れる（BUG-119）、電源ボタン・蓋・AC（ws049-p007）、タッチパッド、音量の slider（BUG-170）。

### uat-3 の結果（2026-10-04 夜、ユーザー）

- WiFi（BUG-158）: 「WiFiは問題ないです。解決。」→ resolved。
- 新規: [BUG-177](bugs/BUG-177.md) Files の検索バーで日本語の IME が使えない、[BUG-178](bugs/BUG-178.md) title bar のタッチのドラッグで押した位置がずれると動かない（「直して」）、[BUG-179](bugs/BUG-179.md) title bar のダブルクリックの最大化が約 0.8 秒（目標 0.1、少なくとも 0.2 秒）、[BUG-180](bugs/BUG-180.md) 最大化した窓を上部のバーからドラッグで外すと一度最大に戻ってから小さくなる（「修正して」）。
- 他の観点（BUG-165・119・ws049-p007・BUG-170 など）は今回の報告に無い（未確認として扱う）。
- 追加（ユーザー）: [BUG-181](bugs/BUG-181.md) Browser の URL バーを hover すると長い URL が title bar をはみ出す、[BUG-182](bugs/BUG-182.md) Browser でタップがクリックにならず HTML のボタンを押せない（どちらも WS074、Codex の担当）。
- 追加（ユーザー）: タッチパッドの 2 本指のスクロールが少なくとも Settings で効かない → [BUG-156](bugs/BUG-156.md) を reopen（BUG-165 の DSDT の修正の後でも再現）。
- 追加（ユーザー）: [BUG-183](bugs/BUG-183.md) WiFi をオンにすると switch が青くなるまで約 1 秒（オフは一瞬）、[BUG-184](bugs/BUG-184.md) Settings で WiFi をオンにした後のオフのクリックが Scan のボタンに取られる。
- 追加（ユーザー）: [BUG-185](bugs/BUG-185.md) Settings で WiFi をオンにした時 Connecting… が出ず Connected まで状態があいまい（状態の取得の疑い）。
- 追加（ユーザー）: [BUG-186](bugs/BUG-186.md) 鍵の入力欄が Connecting… の間も残る（確定の時点で消す）、[BUG-187](bugs/BUG-187.md) 鍵の誤りで失敗すると他の AP に自動で接続し失敗の表示も出ない（Disconnected にして自動接続しない、失敗を表示）。
- 追加（ユーザー）: [BUG-188](bugs/BUG-188.md) Settings の AP の一覧でシングルタップでは接続されずダブルタップで接続される（シングルタップで接続すべき）。
- 要望（ユーザー）: [ws089-p021](ws089/phase021/phase.md) Wi-Fi の画面の自動の scan と Disconnect の icon、[ws089-p022](ws089/phase022/phase.md) Ethernet の頁の設定（compositor 経由、backend が net の command）。
- 追加（ユーザー）: [BUG-189](bugs/BUG-189.md) WiFi と USB LAN の両方の接続で Active Network が WiFi（USB LAN になるべき、default route が USB LAN かも試験する）。
- 追加（ユーザー）: [BUG-190](bugs/BUG-190.md) タップダウンが全ての UI の要素で押下として効かない（押下と同じ扱いにすべき）。
- 要望（ユーザー）: [ws089-p023](ws089/phase023/phase.md) Storage の頁の使用量の解析（multi-thread・逐次の更新・Stop）と Trash を空にする。Settings の Display の頁は [WS113](ws113/ws.md)（q702）。
- 追加（ユーザー）: [BUG-191](bugs/BUG-191.md) Settings の key のリピートの設定が 5330 のキーボードの挙動に効かない（BUG-172 と同じ根の見込み）。内蔵 LCD の明るさの調節の要望は [WS113](ws113/ws.md) の p006 へ。
- 要望（ユーザー）: [ws089-p024](ws089/phase024/phase.md) Mouse の頁に加速の設定、既定 base 150%・加速 強め。

## UAT 2026-10-05 午後（実機 Latitude 5330、image は 12 時の uat-0505c の見込み、ユーザーのコメント）

| # | 所見 | 記録 |
| --- | --- | --- |
| 1 | 起動の logo の animation が最初は速く、その後に通常に戻る。実時間で進める必要 | [BUG-193](bugs/BUG-193.md) |
| 2 | Terminal を全画面にすると戻れない、F11 で全画面にも戻すこともできない。全画面はゲーム用で scanout を占有する物、戻る key は compositor が持つ | [BUG-194](bugs/BUG-194.md) |
| 3 | タッチパッドでスクロールなどができない。Q1 が dmesg を読み、DSDT が読めず（AML の stack の予算）native の touchpad が付いていないと分かった（image は ee2f8c7 = uat-0505c を確認） | [BUG-195](bugs/BUG-195.md) |
| 4 | 最大化している状態で新しく起動した app は最大化で開くのがよい（タブレットを画面全体で使っている認識） | 要望、[ws099-p033](ws099/ws.md) |
| 5 | Browser: 長い URL で文字の範囲の選択が title bar をはみ出して描かれる（前回の指摘が直り切っていない） | [BUG-181](bugs/BUG-181.md) |
| 6 | Files の Devices: mount の前に確認の popup が無いのは危ない。起動 disk の partition は出さなくてよい | 要望、[ws132-p009](ws132/ws.md) |
| 7 | 電源ボタンを押すと、ただちに電源が切れた | [BUG-196](bugs/BUG-196.md) |
| 8 | タッチパッドの 2 本指・3 本指が使えない（BUG-195 で native の touchpad が付かず PS/2 の互換の mouse のため）。そのため 2 本指のスクロール・右 click、3 本指の gesture（Wiseview・仮想デスクトップ・切り替え）、Alt+Tab の 3 本指は**未実施** | [BUG-195](bugs/BUG-195.md)（次の UAT で再確認） |

## 次の UAT で採る記録（2026-10-05 Q1）

- ~~[BUG-190](bugs/BUG-190.md) の evdev の記録~~（2026-10-05 ユーザーの決定で不要。WS159 の後に再評価）。
