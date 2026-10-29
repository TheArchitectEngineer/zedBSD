<!-- awesome-plan project=zedbsd record=ws314-p001 -->

# ws314-p001 `keiland.h` を component ごとの `keiland_*.h` に分け、comment から開発の管理情報を除く

- 親: [WS314](../ws.md)。Queue: q1690 / q1690-i01（担当 P2、2026-10-30）。
- Status: cleared（2026-10-30。確かめは下、QEMU・実機は不要（宣言の移動と comment だけ））
- Disposition: normal

## 目的・受け入れ

- `userland/desktop/include/keiland/keiland.h` の定義を component ごとの `keiland_*.h` に移す。`keiland.h` は入口で全部を include する。
  宣言・定義・comment の移動だけ。API・ABI・KL_VERSION は変えない。各 header は単独で include できる。
- ユーザーの指示（2026-10-30、Q1 経由、原文）: 「ws314のp001で、コメントから開発情報を削除してほしいです。」
  Q1 の範囲の指定: 分けた全ての `keiland_*.h` と `keiland.h` の comment から、WS・Phase・Queue・Bug の番号、plan/ への参照、日付の経緯、
  試験の進め方の注を除く。旧 library の版（「libkeiui's version N」）と設計の節の番号も除く（版が要る所は KL_VERSION N）。
  KL_VERSION の履歴は API の変更の記録として残し、WS の参照だけを除く。意味の説明と出典は残す。comment だけの変更で API・ABI・symbol は同じ。
- 確かめ: libkeiland と keiland.h を使う全ての program を amd64・arm64・riscv64 と Linux（keiland-linux.mk）・FreeBSD（host で確かめられる範囲）で
  build して warning 0、exports.map の名前は不変、前後で生成物の symbol の一覧が同じ。

## 分け方

`keiland.h` の節をそのままの順で移した（節の中の順も保つ）。component は関数・型の接頭辞（`kl_xxx`）で決め、
一つの component が元の header で離れていた所（`kl_window` の宣言型の部分、`kl_system_media_*`）は同じ header にまとめた。

| header | 中身 | include する物 |
| --- | --- | --- |
| keiland_version.h | `KL_VERSION`、`kl_version`、版の履歴（版ごとに 1 行の一覧）、file chooser と preferences の移動の注 | — |
| keiland_menu.h | System Menu（`kl_menu_*`・`kl_window_menu_*`）、context menu（`kl_menu_popup`・`kl_context_menu_*`） | — |
| keiland_titlebar.h | Titlebar Presentation（`kl_titlebar_*`、`KL_CONTROL_*`・`KL_TAB_*`・`KL_FOCUS_*` ほか） | — |
| keiland_recent.h | 最近の file（`kl_recent_*`） | — |
| keiland_glass.h | glass の panel（`kl_glass_*`） | — |
| keiland_motion.h | touch motion（`kl_motion_*`） | — |
| keiland_scroller.h | scroller（`kl_scroller_*`） | — |
| keiland_gesture.h | gesture（`kl_gesture_*`） | — |
| keiland_desktop.h | desktop surface（`kl_desktop_*`） | — |
| keiland_keyboard_inset.h | keyboard inset（`kl_keyboard_inset_*`） | — |
| keiland_edit.h | 編集の操作（`kl_edit_*`） | — |
| keiland_settings.h | desktop の settings（`kl_settings_*`） | — |
| keiland_system.h | desktop の system（`kl_system_*` 全部、`kl_system_media_*` を含む） | — |
| keiland_instance.h | program の一つの copy と activation（`kl_instance_*`・`kl_activation_token`・`kl_activate`） | — |
| keiland_appearance.h | 明暗（`kl_appearance_*`） | — |
| keiland_tr.h | 翻訳（`kl_tr_*`） | — |
| keiland_canvas.h | `kl_color`・`kl_rect`・`kl_image`・`kl_canvas`、`kl_canvas_*`・`kl_image_*`・`kl_color_mix` | — |
| keiland_text.h | text（`kl_text*`・`kl_glyph`・`kl_utf8_next`） | canvas |
| keiland_theme.h | theme と accent（`kl_theme*`・`kl_accent*`） | canvas |
| keiland_icon.h | icon（`enum kl_icon`・`kl_icon_*`） | canvas |
| keiland_scroll.h | scroll・touch pad の track・overlay の scroll bar（`kl_scroll*`・`kl_axis_track*`） | — |
| keiland_key.h | key（`KL_KEY_*`・`KL_MOD_*`・`kl_key_*`） | — |
| keiland_text_touch.h | text の touch と編集の bar（`kl_text_touch*`・`kl_text_bar*`） | canvas |
| keiland_ui.h | window の入力の振り分け（`kl_ui_*` の hit・frame・damage） | — |
| keiland_window.h | window（`kl_window_*`、宣言型の menu・control・tab・glass・drag and drop・tablet・Vulkan surface、`kl_drop_frame`・`kl_clock_us`） | — |
| keiland_app.h | application（`kl_app_*`） | window |
| keiland_widgets.h | widget（`kl_style`・`kl_button` ほか、`kl_field`・`kl_list`・`kl_grid_*`・focus と `kl_ui_window_*`） | canvas・icon・scroll |
| keiland_file_chooser.h | file chooser（`kl_file_chooser_*`） | — |
| keiland_media_picker.h | media picker（`kl_media_picker_*`） | — |
| keiland_audio.h | 音の再生の stream（`kl_audio_stream_*`） | — |

- 値で使う型・macro・typedef・enum は定義の header を include し、pointer だけで使う struct は前方の宣言にした（循環なし）。
  `<stddef.h>`・`<stdint.h>` は使う header が自分で include する。各 header は guard（`KEILAND_<NAME>_H`）と自分の `extern "C"` を持つ。
- `keiland.h` は library の説明、include の一覧、widget の層の説明（drawing 以後の include の前）だけ。
- KL_VERSION の履歴の置き場所: keiland_version.h。1 行に詰めていた長い comment を、版ごとに 1 行（折り返しあり）の一覧にした。
  版を足す時は一覧に 1 行を足す（他の担当の branch との衝突が 1 行で済む）。
- 元の `kl_system_media_*` は `extern "C"` の外にあった（C++ から使うと名前が mangle される不具合）。keiland_system.h の中で `extern "C"` の内に入った。C の ABI は同じ。
- 他の header を指していた「above」「below」の comment は header の名前に直した（titlebar の「System Menu above」、version の注）。

## comment から除いた開発の管理情報

- 自動の規則（scratchpad の strip.py、commit しない）で、comment の括弧の中の WS・Phase・Bug の番号、plan/ の path、「design section N」、
  「libkeiui's version N」を除き、変えた段落だけを 78 桁で折り返し直した。残りは個別に直した:
  「(the user's choice of 2026-10-02)」、「moved here by ws090-p006」、「since ws172-p002」、「(one definition since WS131 p014)」（→ 定義の header の名前）、
  「as Terminal had them of its own」「Settings' own window moved here」「Files' drag and drop and its desktop moved here」、
  drawing の層の由来の文（「It began as the file manager's drawing surface ...」）、text の「version 9」（旧 library の版）。
- 残した物: KL_VERSION N（API の版）、protocol の版（`kl_titlebar_v1 before version 5`・`kl_system_manager_v1 version 24` など）、
  実装の file の名前（`canvas.c` など）、docs/ への参照（`docs/architecture/security.md`）。
- 語の単位の diff で、除いた語が開発の管理情報だけであることを確かめた（他の語は折り返しの移動だけ）。

## 付随の修正

- `userland/desktop/libkeiland/exports.py`: `keiland.h` の include の順に `keiland_*.h` を読む。docstring の開発の管理情報を除いた。
- `userland/desktop/libkeiland/exports.map`: 再生成。名前は同じ 508、順だけが変わった（22 行。`kl_system_media_*` が `kl_audio_*` の後から
  keiland_system.h の位置へ、window の宣言型の部分が `kl_app_*` の後から keiland_window.h の位置へ）。Q1 が順の変化を了承（2026-10-30）。
- `userland/desktop/keiland-freebsd.mk`: install する header を `keiland/keiland*.h` 全部にした。
- keiland.h だけを複写・link していた host の試験の script を `keiland*.h` にした（Q1 が所有に加えた、2026-10-30）:
  plan/tools の phone/run-host-phone.sh・input/host-input.sh・files/host-build.sh・touch/run-notestouch.sh・keiui/run-host-touch-select.sh・
  keiui/host-chooser.sh・media/run-host-mediastorage.sh・mail/run-host-browser-mail.sh・mail/run-host-mailer.sh・textedit/host-core.sh・
  music/host-music-play.sh・calendar/run-host-calendar.sh・keiland-machine/host-machine.sh、
  plan/ws128/tests/host-share.sh・plan/ws079/tests/run-notes-zoom-limit.sh・plan/ws191/tests/host-audio-stream.sh・
  plan/ws217/tests/host-rotation-settings.sh・plan/ws089/tests/host-build.sh・plan/ws100/tests/host-audio.sh・plan/ws131/tests/host-system.sh。
- 直さなかった物: plan/tools/keiland-freebsd/native-build-audit.py（keiland.h があることだけを見る、今も正しい）、
  plan/ws307/tests/host/installer-manifest-test.sh（keiland.h は架空の path の例）、plan/ws131/tools/rename-map.py（WS131 の一度きりの道具）。
  Makefile.linux・Makefile.freebsd に keiland.h の名前は無い（`-Iuserland/desktop/include` だけ）。zedBSD の sysroot は
  `userland/desktop/include` の全 file を入れる（toolchain/llvm/sysroot.mk、変えていない）。

## 確かめ

- host（clang 19・gcc、`-Wall -Wextra -Werror`）: 31 個の header のそれぞれを単独で include して C（gnu17、clang・gcc）と C++（clang++）で compile、
  2 回の include も通る。
- `keiland.h` を前処理した宣言の多重集合が前後で同じ（696、`VK_VERSION_1_0` を定義すると 697）。macro は新しい guard の他は同じ。
  comment の掃除の後も同じ。
- `python3 userland/desktop/libkeiland/exports.py --check` が通る。名前の集合は前と同じ 508。
- zedBSD（`sh plan/ws314/tests/p001-build.sh`、前は c910bba8c の keiland.h・exports.py・exports.map、後は分割と comment の掃除の tree で build）:
  amd64（config-amd64.mk、desktop 20 と試験の program 9 と installer = 30）・arm64（config-odroid-c5-keiland.mk、20）・
  riscv64（config-visionfive2-keiland.mk、20）の全部が前後とも make 0・warning 0・全部 build、`llvm-nm -D` の symbol の一覧が 3 arch とも前後で同じ
  （build/ws314-p001/summary.txt）。最後の comment の掃除（widgets の 1 段落）は build の後だが、前処理の結果が同じことを確かめた。
- Linux（`sh plan/ws314/tests/p001-hosts.sh`、keiland-linux.mk の all）: 前後とも make 0・warning 0・ELF 29、`nm -D` の一覧が前後で同じ。
- FreeBSD（host で確かめられる範囲）: 31 個の header を `--target=x86_64-unknown-freebsd14 -ffreestanding` で単独に compile して失敗 0。
  `KEILAND_FREEBSD_PUBLIC_HEADERS` に keiland*.h が 31 個入る（`make -pn`）。`install-headers` は libseat の取得（`fetch` が host に無い）で止まり未実施。
- 直した host の試験の script のうち 3 本を流した: `plan/tools/input/host-input.sh`（96/96 passed）、`plan/tools/mail/run-host-mailer.sh`
  （PASS 18・FAIL 0）、`plan/tools/keiui/host-chooser.sh`（85/85 passed）。出力は build/ws314-p001-tests/。他の 17 本は未実施。

## 残り

- comment に残る旧い歴史の短い語（例「(KL_VERSION 28, a volume volumed lists)」の言い回し）は p010 の規約の見直しで読み直す。
