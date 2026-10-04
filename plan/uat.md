<!-- awesome-plan project=zedbsd record=uat-2026-10-04 -->
# 実機の UAT（2026-10-04 13:00、Latitude 5330）

2026-10-04 ユーザー「実機は本日13時にUATとしてテスト予定です。テスト観点とテスト手順をまとめて、plan/uat.mdにしておいてください。」

S1（2026-10-03、[WS133](ws133/ws.md) の s1-procedure.md・s1-results.md）の後の直しと、10/03〜04 に QEMU だけで確かめた変更を、素の 5330（bare metal）で確かめる。結果は各項目の「結果」の欄に、QEMU の証拠と分けて書く（AGENTS.md「検証」）。FAIL は Bug にして元の WS へ。

## 1. image

| 項目 | 値 |
| --- | --- |
| path | `/home/awe/zedBSD-claude1/build/uat-2/hdd-image.img`（2,216,689,664 byte、**sha256 `f03920c31d83aa093c17de10d28c877e24c3b52808eed7446880691b3612aa64`**、2026-10-04 12:08 に作り直し（emacs の SKK の辞書の path の直し a1d84b8 を含む）: App Home に System Monitor と Emacs の tile（`plan/ws133/uat-apps.conf`）、config-uat.mk が emacs・monitor・noto-color-emoji を足す。前の版 `4b643284…cb71f` は tile が無い） |
| 作り方 | `plan/tools/guest/test-image.sh --no-harness plan/ws133/config-uat.mk build/uat-2` に build-demo-image.sh と同じ `--file`（apps.conf・壁紙・authorized_keys）と `I915_TEST_VBT=n`。config-uat.mk は demo の構成（その時の CI の amd64 の構成: emacs・clang 入り）に monitor と noto-color-emoji を足したもの。build/uat-1 はユーザーの新しい CI の構成で emacs が無いので使わない |
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
| A3 の後 | 観察 | 再起動で WiFi は自動で接続したが、それまでの約 10 秒「Network service is not …」（network の service が使えない）と表示 | 新規（起動の直後の networkd の待ち） |
| B1 | 未実施 | USB マウスが手元に無い（後で） | |
| B2 | **NG** | タッチパッドの 2 本指のスクロールができない（Browser で確認） | BUG-156（再現） |
| B3 | **NG（デグレ）** | タッチパッドで窓の移動ができない。タッチしてクリックしてから別の指で動かすとドラッグできた。2 回タップして動かすのもできた。押下（トラックパッドの押し込み）で動かすかは仕様の検討漏れ | 新規（ws099-p030 の後の touch の drag。仕様の調整が要る） |
| B4 | OK（条件付き） | B3 と一緒に仕様の調整が必須 | 同上 |
| B5 | OK（条件付き） | 同上 | 同上 |
| B6 | OK（条件付き） | 同上。**トラックパッドのクリック（押し込み）が認識されない** | 新規 |
| C6 | **NG** | 1 分でフリーズ。kernel のフリーズ | BUG-158 |
| C2 | **NG** | 間違えた鍵で `Could not join (Network is unreachable)` | BUG-157（再現、文言） |
| C1 | OK | | |
| C4 | OK | DHCP OK | BUG-145（実機で OK） |
| C3 | OK | | |
| C5 | OK | WiFi off → on で 8 秒で自動接続 | |
| C7 | **NG** | 起動の後に USB LAN を挿すと ue0 は down のまま。WiFi を off にしても down のまま。そのまま ue0 を抜くと、system bar の Ethernet のメニューに wlan0 が出る。ue0 を挿し直しても wlan0 が出たまま | 新規 2 件（後から挿した USB LAN が up しない／Ethernet のメニューに wlan0）。仕様ではない（Q1） |
| D1 | **NG** | 100% には戻らず調整できた。しかし slider をドラッグすると、しばらくフリーズし、放置で回復。連続で確認の音を鳴らそうとしている疑い | 新規（slider のドラッグでフリーズ） |
| D2 | **NG** | Settings も、クリックは OK、ドラッグでフリーズ | 同上 |
| D3 | OK | ミュート → 解除 | |
| D4 | OK | | BUG-161（実機で OK） |
| E1 | OK | | BUG-152（実機で OK） |
| E2 | OK（気付き） | opaque にしても窓が不透明にならない | 新規（窓の透明度の opaque） |
| E3 | OK | | |
| F1 | 未実施 | USB メモリが起動用の 1 本しかない | |
| F2 | 未実施 | 「タブ」が何か分からなかった（手順の説明の不足） | |
| F3 | OK | PDF の試験の data が image に無く、Notes で PDF を作って表示 | 気付き: UAT の image に見本の file（PDF・PNG）が無い |
| F4 | OK（一部） | scroll bar は OK。タッチのスクロールができないので慣性スクロールは未テスト | BUG-156 |
| F5 | 未実施（一部） | PNG が無くテストできず。PPM は Files の中で preview の窓が出た | 気付き: 見本の file が無い |
| F6 | OK | | |
| F7 | OK | emacs の保存・終了・SKK | |
| F8 | OK | | BUG-143・BUG-139（実機で OK） |
