# HDMI を主出力にする（全画面 1 screen、デモ用）— 調査と計画

2026-09-28。読むだけの調査（実機・QEMU は未使用）に基づく計画の案であり、実行の許可ではない。

ユーザー（WS079 の文脈）:「HDMIをメイン出力にする方法を確立して、全画面1スクリーンのみでデモに使いたいです。」
デモ機は Latitude 5330（ADL-P、display ver 13、PCH ADP）。10 インチのタッチ LCD（AES ペン）を HDMI + USB、または USB-C（DP alt mode）で繋ぐ。
デモ日は 2026-10-17。

## 1. 今の出力の決まり方

| 問い | 答え（根拠） |
| --- | --- |
| kernel が駆動する connector | **eDP だけ**。resident node の設定は port A・pipe A・transcoder A・DPLL0・AUX A に固定（`display/modeset.c` の cfg の組み立て、`c->port = 0` 等）。preflight は `d->edp` が live であることを要求する |
| HDMI の modeset | **できる（試験の場面だけ）**。E-123（2026-09-20、[report-e123-hdmi.md](../ws031/handover/expert-reports/report-e123-hdmi.md)）で DDI B（combo PHY）・pipe B・transcoder B・DPLL0/1、1280×720@60（CEA 4）を実機で点灯・停止、eDP との 2 画面も PASS（[report-e123b-dual.md](../ws031/handover/expert-reports/report-e123b-dual.md)）。今の tree では `tests/display/hdmi-output.c`（HDMI-B・DUAL・DUAL-SHARED）と `lcd-run.c` の `output_hdmi` 経路。modeset は `cfg->output_hdmi` で DP 固有の段（M/N、link training、backlight）を飛ばし、WRPLL（`state.c` の `drv_i915_lcd_compute_hdmi`）、DDI の HDMI hook（`ddi.c`）、`TRANS_DDI_FUNC_CTL` の HDMI/DVI 選択を持つ。**DVI mode（`has_hdmi_sink=0`、infoframe なし、音声なし）で点灯済み** |
| EDID | 読む経路（GMBUS、`edid-read.c`、`hdmi.c` の `intel_hdmi_set_edid`、`edid.c` の `drv_i915_edid_preferred_mode`）はあるが、**E-123 の環境では DDC が 0x50 で NAK**（port を点けた後も同じ、同じ環境の Linux 6.8 も同じ）。原因は未確定。このため試験の場面は mode を固定で与えている |
| hotplug | **kernel は受ける**（`hotplug.c`: SDE 割込み → `intel_hdmi_detect`、実機で抜き差しを確認）。ただし display の ops は「topology の変化を公開しない」（`display.c` の `i915_display_ops`、`drv_i915_hpd_events` は event の番号だけ）。出力の切替には繋がっていない |
| USB-C（DP alt mode） | **不可**。TC port の `intel_tc_port_connected`・`intel_dp_detect`・`hpd_pulse` は移植されず step の記録だけ。modeset は combo PHY の port 0〜1 しか受けない（`drv_i915_lcd_modeset_prepare` の `cfg->port > 1` で EINVAL） |
| compositor が受け取るもの | `GPU_DISPLAY_QUERY` の index 0 の 1 枚だけ（`userland/desktop/wayland/display.c`）。i915 は常に `display_id=1`・名前「eDP panel」・panel の native mode を current = preferred = max として返す（`i915_display_query`）。`GPU_DISPLAY_MODE` は panel の mode だけで、小さい frame は panel の timing に拡大（再 timing しない）。compositor は preferred の大きさで全画面を作る |
| UEFI GOP | loader は `LocateProtocol` で最初の GOP を取り、`video=WxH` があれば `SetMode`（`bootloader/uefi/video.c`）。どの connector に出すかは firmware の判断。boot の framebuffer は text console と retro の `/dev/graphics` 用で、**Keiland は GOP を使えない**（compositor は `GPU_CAP_SHARE | GPU_CAP_DISPLAY` を要求、GOP の GPU node は無い）。i915 の N0/N1 は firmware が pipe A に点けた表示を読んで止める |

## 2. HDMI を唯一の全画面出力にするのに足りないもの

1. **出力の選択**: resident node が eDP の代わりに HDMI（port B・pipe B か A・DPLL）で modeset する経路。`i915_display_query`・`_mode` が HDMI の mode・名前・物理寸法（EDID の byte 21〜22）を返す。eDP は点けない（panel power と backlight を off のまま、N1 が firmware の pipe A を止めた後に触らない）。今の `d->edp` 必須の preflight を「選んだ出力が live」に一般化する。
2. **connector の検出**: boot 時に 1 回、`SDEISR` の live status と EDID の有無で HDMI の接続を決める（hotplug の経路は既にあり、`intel_hdmi_detect` を 1 回呼べばよい）。デモでは boot 後の抜き差しで出力を切り替えない（display_id と generation を boot 中は固定）。
3. **EDID と mode**: EDID が読めれば DTD 1（`drv_i915_edid_preferred_mode`、多分 1280×800 か 1920×1200）を使う。1920×1200@60 CVT-RB は約 154 MHz、1280×800@60 は約 71〜83 MHz で、どちらも scrambling の要らない 340 MHz 以下。**EDID が読めない（E-123 と同じ）ときの代わりが要る**: kernel の parameter で mode を与える（下の `display.mode=`）か、CEA 4（1280×720）。
4. **HDMI の encoder と DDI**: 移植済みで実機で点灯済み（combo PHY の voltage swing、VBT の HDMI level shift、WRPLL）。足すのは resident の present（flip、2 枚の buffer、`scanout.c`）を pipe B の plane に向けることと、stop の経路の pipe B 版（E-123 の LAST-RESORT と参照の停止経路はある）。pipe A の DBUF/watermark を pipe B の単独に計算し直す（E-123b の device 全体の DBUF で済むはず）。
5. **音声なしの HDMI/DVI**: DVI mode（`has_hdmi_sink=0`）で足りる。安い LCD の controller は DVI を受けるのが普通。受けない場合だけ HDMI mode + AVI infoframe（`hsw_set_infoframes` の packing は未移植）を足す。
6. **入力の対応**: 1 画面なので touch とペンの座標は画面の全体へ 1:1（WS079 の範囲、ここでは扱わない）。

## 3. 10-17 のデモに向けた最も単純で確かな道と代わり

**本命（A）: kernel の parameter `display=hdmi`（既定は `auto` = 今の eDP）。**
- `display=hdmi`: HDMI が boot 時に connected なら resident を HDMI で点け、eDP は点けない。connected でなければ eDP に戻り、log に理由を残す。
- `display.mode=WxH[@R]`: EDID が読めないときの mode（例 `1280x800@60`）。EDID があれば EDID が勝つ。
- 既存の `kmsg=`・`login=` と同じ `src/kern/boot.c` の解析に足す（HAL に触れない）。
- 1 画面・hotplug なし・DVI mode・pipe B（E-123 で実証済みの組）を使う。これが実機で確かめた部品の組み合わせに最も近い。

**代わり（B）: mirror**。eDP を今まで通り点け、同じ buffer を pipe B で HDMI にも出す（E-123b の DUAL-SHARED、実機 PASS）。ただし scaler 無しでは左上の切り出しになるので、compositor の大きさを HDMI の mode に合わせ（`--width/--height`）、eDP 側にその一部が出る形。A の出力選択が間に合わないときの保険。

**代わり（C）: 画面の複製器**。HDMI の splitter や外部の converter は使わない（使える保証がない）。**GOP（蓋を閉じて firmware に HDMI へ出させる）は Keiland を動かさないので使えない**。蓋を閉じた場合 firmware が HDMI に pipe を点けて来る可能性があり、N0/N1 がそれを止める・読む挙動を確かめる必要はある（A の試験 H4）。

**USB-C（DP alt mode）はデモでは使わない**。TC の PHY・DP の link training（DP SST は eDP 用のものがある）・Type-C の所有の手順が必要で、10-17 までに実機で固める見込みが低い。

## 4. Phase の分け方（案）と試験

QEMU は i915 の HDMI を試せない。各 Phase の最後は実機（5330 + 10 インチ LCD、`plan/ws075/tests/test-hw.sh` と `flock /tmp/i915-hw.lock`）。host 試験は `tests/display/host-*.c`。

| Phase（案） | 内容 | 試験 |
| --- | --- | --- |
| H1 実機の事前調査 | 今の build の HDMI-B と hdmi-edid の場面を **10 インチ LCD** で走らせる: EDID が読めるか（native boot で）、DVI mode で映るか、1280×720 と LCD の native mode（固定で与える）で映るか。VBT の port B の child（ddc_pin、level shift）を記録 | 実機: HDMI-B・HDMI-EDID の場面、LCD を目視・写真。コード変更なし |
| H2 出力の選択 | `display=` と `display.mode=` の parameter、resident の cfg を HDMI（port B・pipe B・DPLL）で作る、query/mode が HDMI の mode と名前を返す、eDP を点けない、HDMI が無ければ eDP に戻る | host: parameter の解析と cfg の組み立ての試験、`host-lcd-modeset-test` を HDMI の cfg で。実機: `display=hdmi` で boot → graphical login と Keiland が LCD の全画面、eDP は暗い。`display=auto` で従来どおり eDP（回帰） |
| H3 EDID の mode | EDID の DTD 1 を mode と物理寸法に使う。EDID が無ければ `display.mode=`、それも無ければ CEA 4 | host: 既知の EDID（LCD から採った実物）から mode・WRPLL を計算。実機: parameter 無しで native mode |
| H4 デモの形での確認 | 蓋を閉じた boot・開いた boot、LCD を先に繋いだ boot、30 分の連続表示（flip と present）、shutdown で pipe B が止まること | 実機のみ。写真と `boot-test.sh` 相当の画面の取得 |
| H5（任意）| hotplug で出力を切り替える、HDMI mode + infoframe、USB-C の DP alt mode | デモ後に判断 |

H1 は H2 と独立に先に走らせ、結果で H3 の要否（EDID が読めるか）と mode を決める。WS075 の既存の p007〜p010 との順は root が決める（p010 の規約確認はこれらの後）。

## 5. ユーザーへの問い

1. デモでノートの**蓋は閉じる**か。閉じるなら firmware の表示と N0/N1 の挙動を H4 で確かめる。
2. 接続は **HDMI + USB** でよいか（USB-C の DP alt mode はデモでは使わない案）。
3. LCD の**解像度**（1280×800 か 1920×1200 か）と、手元に LCD があるか（H1 に要る）。EDID が読めない場合に固定の mode で良いか。
4. HDMI が繋がっていないとき eDP に戻る動作でよいか（デモでは常に繋ぐ前提）。
5. 音声は HDMI から出さなくてよいか（DVI mode の案）。
6. 実機の i915 の予定（今は別の agent が使用中）: H1 の実機の時間をいつ取るか。

## ユーザーの回答（2026-09-28）

1. 蓋: ACPI が未実装なので、sleep しないように**開けておく**。内蔵画面（eDP）は点けない（HDMI の 1 画面だけ）。
2. 接続: デモは **HDMI + USB**、USB-C（DP Alt Mode）は使わない。時間が余れば USB-C も予定どおり実装する（H5）。
3. LCD の解像度: **1920x1080** の見込み。EDID が読めないときは boot parameter の固定のモード。
4. HDMI が起動時に無いとき: **内蔵画面で起動**（fallback）でよい。
5. 音声: HDMI から**音は出さない**（DVI のモード）。
6. H1 の調査で i915 の実機（5330）を**占有してよい**。10 インチの touch LCD を HDMI と USB でつないだ（2026-09-28）。
   touch の HID は HDMI の出力が有効になるまで見えないかもしれない（ユーザー）。H1 で HDMI の前後の USB の device の一覧と、見えれば report descriptor を記録する。
7. 5330 の IP は電源の入れ直しで 10.0.30.3 に変わった。main が `~/.ssh/config` の `solaris10-man` を 10.0.30.3 に書き直した（ssh で `chaos` を確認）。

## 結果（2026-09-28）

- H1 = [ws075-p011](phase011/phase.md): EDID は読める（JTG S123、native は **1920x1280**@60、164.36 MHz）。pipe B・DVI で
  1280x720・1920x1080・1920x1280 を出力。touch の USB は 5330 に列挙されない（cable と口の確認が要る）。
- H2 = [ws075-p012](phase012/phase.md): `display=hdmi`（EDID の mode、`display.mode=` で上書き）で Keiland が HDMI の全画面、
  `display=auto` は eDP。H3（EDID の mode）の内容は H2 に含めた。残りは LCD の目視、HDMI の無い boot の実機確認、H4。
- H4 = [ws075-p013](phase013/phase.md)（2026-09-28、実機の passthrough）: デモの image（`plan/ws075/demo/build-demo-image.sh`、
  `config-demo-hdmi.mk`: graphical boot + `display=hdmi` + App Home の application）で、splash → greeter（HDMI 1920x1280）→ login →
  session（HDMI）→ Terminal の窓を 4 秒ごとに drag して 30 分（perf の全区間で present あり、lease の終わりの判定は underrun なしの
  PASS、3214 flip）→ Log Out → greeter の Shut Down（CPU は全て halt、pipe B は停止）。途中で 2 つの不具合: (1) H2 の resident の vblank の
  待ちが pipe A の frame counter を読み、HDMI では flip の event が全て時間切れ → 最初の lease の終わりで display が FAIL し login の後が
  出なかった（`vblank.c` を修正）、(2) i915 の `/dev/gpu0` より先に sessiond が起動して console に戻る（デモの image の `greeter_gpu` で
  回避、2026-09-28 に ws035-p113 で sessiond を直し回避を削除、BUG-092）。eDP への fallback は試験の switch（`-DI915_TEST_HDMI_ABSENT=1`）で確認。
  **amd64 の Shut Down は halt であり電源は切れない**（ACPI の S5 は未実装）。session には Shut Down が無い（Log Out → greeter）。
  最終の image（main の merge の後、Notes・PDF Viewer を加えた）では **Notes の起動の約 2 秒後に kernel が止まった**（`i915_timer_thread` →
  `waitq_sleep` → `spin_unlock` の持ち主の違いの trap、gdbstub で解析、未修正。p013 の不具合 3、要 Bug ticket）。
- 未実施（ユーザー）: LCD と eDP の目視、bare metal での起動（firmware の splash の出し先、takeover の時の eDP）、実物の cable を抜いた
  boot。splash を HDMI に出す案（kernel の spinner を i915 の pipe B で続ける、loader が外の monitor の GOP を選ぶ、BIOS の設定）と、
  lease の替わり目で pipe を止めない案は [p013](phase013/phase.md) の「提案」。画面は worktree の `build/ws075-shots/hdmi-h4-*.png`。
