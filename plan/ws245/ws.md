<!-- awesome-plan project=zedbsd record=ws245 -->

# WS245: i915 の Gen12 系（display 12・13）の display の書き直し

<!-- awesome-plan-current:start -->
Status: incomplete（2026-10-15 作成。p001 の [design.md](design.md) は改訂 6 で cleared（2026-10-16 Q1）。q1218（P1、2026-10-16）で段の実装を開始: p047（証拠の道具）cleared、p016（OpRegion）・p017（VBT の解析）は 5330 の素の起動の回帰に合格し main に merge（p016）/ merge の依頼済み（p017）。p019〜p050a は branch agent/p1-ws245 に実装済み（5330 は全部の段の後に 1 回））
Primary Milestone: MG006
Related Milestones: MG002
Parent: [Master](../master.md)
Related: [WS244](../ws244/ws.md)（i915 の Linux 由来の code の整理、棚卸し plan/ws244/inventory.md・tests/inventory.py）、[Guardrail](../guardrail.md)「MIT・GPL の出典のデータの引用」
Resume point: 2026-10-16 の方針（ユーザー「i915は、実装だけ先に終わらせ、全部マージしたものをテストして、動かなければ二分探索で半分マージしてテスト、だめならさらに半分、で進めます。まずはぜんぶ実装してください。」、Q1 経由）: p020 以降の段を design の順に p046 まで branch（agent/p1-ws245）に積む。各段は host 試験・build（既定と `I915_TESTS=y` の 5 組）warning 0・WIP commit・phase.md の記録。5330 の回帰は全部の後に 1 回（基準は main の kernel、段は branch の tip）、動かなければ下の「段の境の SHA」で二分探索。p019 は T1-704（機能の基準は合格、書き込みの順のずれは別の thread の相対の順: [phase019](phase019/phase.md)）。今の段（2026-10-17 q1229-i11、P1）: p050d4 を Q1 の判断で 4 段（d4a 画面の口、d4b 走らせ方の口、d4c 始まりと終わり、d4d 旧 file の削除）に分けた。p050d4a 済み（86041583b）: 新しい `xd-picture.c/.h`（screen 0・1 の object、prepare・light・dark・flip・status・retained・backlight、device の PLL の pool と DBUF の layout）と `xd-backend.h`（旧 `i915_lcd_emit` を own に、hook `power_domain` を足した）、head.c・present.c・backlight.c・modeset.c の run の code を付け替え、旧 modeset object の口・output-shell.c・panel-backlight.c・color.c と display の ktest の組を消した。比べは `sh build/tmp/stagecmp.sh <前の段> <名前> out50c-picture.c`（新しい口の harness）と `host-xd-picture-test`。p050d4b 済み（9915b53dc、q1229-i12）: 新しい own の `resident.c/.h` に resident run・show・binding・preflight・config（picture の config を直に）・link fallback、`display-internal.h` に `struct i915_display_link`・`i915_lcd_kernel_deps`・`i915_lcd_run_params`、出力は `mode`+`link`、output.c が plan_link を直に（`drv_i915_output_link_hdmi/_dp`）、modeset.c は world だけ。比べは `sh build/tmp/stagecmp.sh <前の段> <名前> out50c-d4b.c out50c-d4b.c`（4 つ目は基準の側の harness）。p050d4c 済み（f491cfcad）: xd の port の表、own の edp-device.c（eDP の device と kernel の backend、panel の link は plan_link で）、始まりの前半を xd の部品（memory の読み・WA・GMBUS reset）で、own の vga.c、takeover.c を空に。modeset world の削除は d4d へ。p050d4d 済み（f25557317、q1229-i13: modeset・state・ddi・dp・pipe・plane・panel・edid を空に、旧 host 試験と fake を空にし oracle は xd の試験、internal.h の Linux 型を削減。以後の比べは `run-xd-host-tests.sh` と build だけ。aux.c と dp-sink.c の DP world は dp-ext のため p052 まで）。p039 済み（02dc4b5c8、q1229-i13: xd-hpd と own の hpd.c、HDMI の検出も xd-hpd に。TC の flow と DP-ext の probe は p052 まで hook）。p051 済み（1388dbae8: GOP の分類と HDMI の port を platform の記述で、output の pipe、head-rules を head.c に）。p052 の 1 つ目済み（200128cc0: 外部 DP の AUX を xd-aux に、Linux の DP の環境を空に）。p052 の残り済み（q1229-i14、e92b2bee0・e466cb254: tc.c の状態機械を新しい xd-tc-port.c に（全 platform、display 12・11 の TC も driven）、tc-kern.c を own の tc-port.c に、host-tc の検査を host-xd-tc-port-test に、dp-ext.c の probe を xd-dp.c の `drv_i915_xd_dp_probe_*` に。intel/ の header は p043）。p041 済み（4045c6b0b: suspend で hotplug を止め TC を返す、resume で VGA・WA・GMBUS を直し TC を読み直し HPD を arm し直して全 connector を見る）。p042 済み（93ec4f94a、q1229-i15: 段の縮約は Q1 の判断 (a)、`drv_i915_display_start/_outputs/_serve/_stop_serving/_stop`、DRM・PM demand・modeset/flip の workqueue を消した、diagnostics.c を xd-diag.c と own の run-log.c に、log の一覧は design §6.4.1）。p043 は Q1 の判断で p043a・p043b に分けた（[phase043](phase043/phase.md)）。p043a 済み（35b24c912・7de79d444: clock.c を own に、PCH の判定を own と xd の表に、intel/ の旧 header と host-xd-regs-test を空に、check-sources.py の監査）。p043b の帯域済み（d16688474: 新しい xd-bandwidth.c、watermark.c/.h を空に、host-xd-bandwidth-test）。p043b の power 済み（57967dfb0、q1229-i16: 新しい own の display-power.c/.h（`struct i915_display_power`、呼び手は `I915_XD_POWERS_*` の集合）、power.c/.h・dc9.c/.h を空に、internal.h の power の型と Linux の base と Derived の表示を消した、host-display-power-test、check-sources.py を runner に。[phase043](phase043/phase.md) の「p043b の 2 つ目: 実装」）。p046 済み（q1229-i16、0e85d1ff1・a5e461698・a3849a76f・16de8c315: style-check の規則を WS245 の行の全部に、試験も、inventory の S を 0 に、試験の plan/ の path を外した、host-capture-test を runner に。全文の目視の通読は未実施: [phase046](phase046/phase.md) の「制限」）。**実装の段は全部済み。T1 の 5330 の 1 回（q1229-i18、2026-10-17）は起動・survey は PASS、`i915.debug=display` の書き込みの比べが FAIL。q1229-i19（P1）で差を分類し（[phase046](phase046/phase.md) の「q1229-i19: 5330 の差の分類」）、不具合 1 件（takeover の stop で AUX・DDI IO の well を持たず D3 の AUX が 160 回 timeout、VDD も無い。p050a の「INIT の参照が全 well を持つ」が誤り）を 26b307bd9 で直した。次は T1 の再試験（同じ節の「q1229-i19 の後に T1 に流す項目」）**。動かなければ下の「段の境の SHA」で二分探索。p050d2 の形（[phase050d2](phase050d2/phase.md)）: plan の check が WM・DBUF・CD clock・QGV の本物の判断（拒み `I915_XD_PLAN_WM` を足した）、殻は plan を `ms->wm`・`ms->cdclk` に写すだけ、影の比べの log は消した。p050d3 の形（[phase050d3](phase050d3/phase.md)）: 1 frame に入る更新（evasion）は `drv_i915_xd_screen_update`、scanline と window は xd-pipe、plane.c は xd の plane と plan の WM を直に、`ms->plan`・`ms->flip` が状態（旧の写しを消した）。比べの道具は P1 の `build/tmp/stagecmp.sh <前の段> <名前>`（旧 dp/lcd と harness out50c の trnorm）。その後 p039・p051・p052・p041・p042・p043・p046（p045 は P6）。p050c の形（[phase050c](phase050c/phase.md)）: 3 種とも step は xd-output、Type-C の PHY・PLL・link は `struct i915_xd_output_tc` の hook（殻 output-shell.c が旧の code に、p040・p052 で xd に）。HDMI・DP-TC の比べは harness `temp/harness/run-out50c.sh`。p050b の形（[phase050b](phase050b/phase.md)）: 1 つの出力の step は新しい `xd-output.c`（環境 `struct i915_xd_output_env` は backend の hook）、modeset.c の step は panel の時 xd-output を呼び、殻 `output-shell.c` が記述・環境・旧の写しの更新（p050d で消す）。旧の lcd 試験の比べは `build/tmp/trnorm.py`（read・decide を除き rmw を値に）で、差は error の文言 2 行。p050a の形（[phase050a](phase050a/phase.md)）: takeover は `drv_i915_xd_takeover`（xd-display、hook は xd-panel・xd-aux を直に）、init の sanitize は `drv_i915_xd_firmware_sanitize`、registry と旧の readout は消した。DPCD の native の read/write は `drv_i915_xd_dpcd_read/_write`（xd-aux）。**Q1 の判断（2026-10-16 23:0x）で p050 を 4 段に分けた**（step の中身が殻のままで design の 4 LW を大きく超えるため）: p050a takeover の readout/sanitize を xd-takeover の record と stop に（takeover.c の registry を消す）、p050b eDP の step の中身を xd の直の呼びに、p050c HDMI・DP-TC の step、p050d own の file を xd.h の口に・旧 file と host-lcd/dp 試験（oracle は xd の試験へ）を消す。その後 p039・p051・p040・p052・p041・p042・p043・p044・p045・p046 を design の順に。p049 の形（[phase049](phase049/phase.md)）: flip の判断は xd-screen の `struct i915_xd_flip`（ms の flip の field は写し）、resident run の戻りは `drv_i915_xd_screen_run_verdict`。p048 の形（[phase048](phase048/phase.md)）: commit_enable/_disable は xd-screen の light/dark（step は modeset.c の hook、crtc の part は pipe.c の `drv_i915_lcd_ms_crtc_enable_part/_disable_part`）。p038 の形（[phase038](phase038/phase.md)）: `xd-screen.c` の plan（link と check）、state.c の compute は plan の写し、modeset.c の check は plan と旧の比べの 1 行（判断は旧）。design §6.2 の p038 の行）。p037 の形（[phase037](phase037/phase.md)）: takeover の hardware は `drv_i915_xd_takeover_stop`（hook は registry の encoder の旧の状態）、N0 は xd の判断だけ。p036 の形（[phase036](phase036/phase.md)）: `xd-takeover.c` の readout・判断・分類、`xd-display.c` の `drv_i915_xd_check_firmware`（環境）、display.c の影の比較 1 行、output.c は xd の record を読む。旧の host 試験の前後の比べは `sh build/tmp/refcmp.sh <前の段の SHA> <段>`。p035 の残り（takeover.c の `drv_i915_display_wa_apply`・`drv_i915_display_nogem_front` の max cdclk・HTI・GMBUS reset、raw clock、DDI A の lane、WM の latency の mailbox）は p036・p037 の takeover の段で xd に。p035 の形（[phase035](phase035/phase.md)）: power.c の core の init/uninit は `drv_i915_xd_core_start/stop`（xd-core.c）に hook（PW1・CDCLK・DBUF・fault）を渡す。p034 の形（[phase034](phase034/phase.md)）: watermark.c は crtc/plane state を `struct i915_xd_wm_pipe`・`i915_xd_wm_surface` にし xd-dbuf の level を Linux の `skl_plane_wm` に写して返す（raw/optimal は旧の state に残る）。slice の電源は power.c が domains の lock の下で xd。p033 の形（[phase033](phase033/phase.md)）: plane.c の書き手は plane state から `struct i915_xd_plane` を作り xd-pipe の noarm → 旧の `skl_write_plane_wm` → arm の順。SURFLIVE の読みと flip の状態機械は p049 まで旧（共有の xd window を worker 以外の thread から触らない）。p032 の形（[phase032](phase032/phase.md)）: pipe.c・ddi.c・color.c の書き手は crtc state から `struct i915_xd_trans_setup`・`i915_xd_trans_func`・`i915_xd_msa`・`i915_xd_pipe_misc` を作り xd-pipe を呼ぶ。lcd 試験の E は消した（pipe-off の待ちは xd の poll）。記録器（emit_transcoder）は xd を通して残る（消すのは p050）。p031 の形（[phase031](phase031/phase.md)）: 殻が port と crtc state から `struct i915_xd_dp_link` を作り、sink の hook は環境の DPCD accessor、source の hook は port の hook。帯域・M/N・rate の一覧・fallback・link の選びも xd-dp（state.c・pipe.c・dp-ext.c が呼ぶ）。DPCD の値は `intel/display-dpcd-xd.h`（map の 62 対）。p030 の形（[phase030](phase030/phase.md)）: 殻が crtc state から `struct i915_xd_ddi_link` を作り xd-ddi の列を呼ぶ、旧の Dekel の窓の WA は xd の hook。lcd 試験の E（time base の fault）は stop の pipe-off の待ち（0x70008、p032 で xd に移ると壊れる: その段で書き直すか消す）。p029 の形（[phase029](phase029/phase.md)）: 旧の get_buf_trans の hook は xd の表の view（長さと HDMI の既定）を返し、語の書き込みは xd。harness `wells29.c` は GRP の書き込みを 4 lane に写す。p028 の形（[phase028](phase028/phase.md)）: modeset の lcd world の hardware の書き込みは、backend の read32/write32/usleep を hook にした xd の window（clock.c の `i915_dpll_window`、backlight の形）を通す。旧の host-lcd-modeset-test の比べは rmw を書いた値に直した書き込みの列（scratch の `wr.py` と同じ: 旧の `rmw b=set c=clear d=前の値`）。xd の platform は `drv_i915_lcd_xd_platform()`（lcd world に display が設定、無ければ display version の最初の行）。p026 の形: 消す旧 file は中身を消して vmunix.mk から外す、file の削除は merge の時に Q1（下の「merge の時に Q1 が消す物」）。harness: `plan/ws245/temp/harness/`（`oldtests.sh <tree> <out>`、`wells2N.c`・`run-wells2N.sh`）。build は `sh build/tmp/kbuild.sh p021`（約 3.5 分。出力を grep する時は `-Werror` に当たる `error` を使わない）。
<!-- awesome-plan-current:end -->

## ユーザーの指示（原文、2026-10-15）

「Gen12（5330）のディスプレイは書き直します。WSを立ててください。」
（前提の決定: 「code・コメント・構造:については、MITであっても写してはいけません。」「範囲を全部の WS に広げます。」「ガードレイルに、ファイル上部には参考にしたことや引用したことを一切書かず、該当箇所にブロック単位で記載することを明記してください。」）

## 対象（WS244 の棚卸しの「Gen12 の display の Linux 環境」の群、約 6 万行）

`src/drivers/gpu/i915/display/` の ddi・dp・dp-sink・dp-internal.h・pipe・plane・clock・watermark・vbt・vbt.h・panel・panel-backlight・power・dmc・takeover・takeover-internal.h・hotplug・hotplug-internal.h・modeset・modeset-internal.h・internal.h・opregion・opregion-internal.h・aux・gmbus・hdmi・hdmi-mode・edid・edid-read・color・vblank・phy・state・diagnostics・dp-ext・dp-ext-kern・tc・dkl-phy。どれも「Derived from the Linux kernel v6.8.12」と Linux の型・関数の分け方・comment を持つ（ddi.c 107/111、dp.c 92/92、pipe.c 74/74、plane.c 43/44 の関数が写し）。intel/ の header のうち Gen12 だけが使う struct の配置（vbt-defs.h・ref.h・connector.h・wm.h）も含める。

## ユーザーの決定（2026-10-15、design.md のレビューの後）

「5は、(1) a、(2) 5330のみ、(3) Linuxのコードを参考にする、(4) Linuxを優先、でおねがいします。」
1. 逃げ道の形は (a): 口（xd.h）を最初の方の Phase で決め（design.md 改訂 2 では p003）、own の file は最初からその口だけを呼ぶ。口の実装を旧（薄い adapter、旧 code を消す Phase で削除。design.md 改訂 2 では p012）と xd で丸ごと切り替える（状態を混ぜない）。
2. 「5330のみ」（意味は未確認。下の対象の方針が優先）。Q1 は当初「Tiger Lake（display 12）は支えない」と記録したが誤り（2026-10-15 ユーザーの補足「Tiger Lakeは実機で動作確認済みです。display 10,11以外は動作確認済みです。」「記録が食い違っているだけです。」）。display 9・12・13 は実機で動いている機能で落とさない。xd は display 12（Tiger Lake、5320）と display 13（ADL-P、5330）の両方を持つ。display 10・11 は今も支えていない（範囲外）。
3. 手順は Linux の code を参考にする（Tiger Lake・Alchemist の PRM は補助）。guardrail のまま、code・comment・構造は写さず、値は block ごとに Value(s) referenced:、参考にした手順は Procedure referenced: <版> <path> (<関数>) を付ける。
4. PRM と Linux（5330 で動いている実績）の値・手順が食い違う時は Linux を優先する。

## 対象の機種（2026-10-15 ユーザー「すでに多数の機種で動作しており、5330を特別扱いはしません。Skylake～Raptor Lake Refreshまですべてサポートします。」）

- WS245 は 5330 を特別扱いしない。対象は今の Gen12 の code が動かしている全機種: display 12（Tiger Lake・Rocket Lake・Alder Lake-S・Raptor Lake-S/HX と Raptor Lake Refresh のデスクトップ）と display 13（Alder Lake-P/U・Raptor Lake-P/U）。display 9（Skylake〜Comet Lake）は gen9-*.c（WS244）。
- display 9・12・13 は実機で動作確認済み（ユーザー「display 10,11以外は動作確認済みです。」）。display 11（Ice Lake）と display 10 は今の code で支えていない。Ice Lake（display 11）は、この WS の最後の方の Phase で新しく支える（2026-10-15 ユーザー「display 11は、i915のリファクタリングの既存WSに最後の方のphaseとして入れてください。」）。display 10 は範囲外。
- 下の「目的と完了の条件」の「5330」の記述（2026-10-15 の作成時）は、この方針で読み替える。実機の確かめは 5330 に限らない。

## 目的と完了の条件

- Gen12 の display を、Intel の公開の PRM（Gen12、ADL-P）と hardware の振る舞いから設計し直して書く。Linux の code・comment・構造（関数の分け方、型、制御の流れ、名前）を写さず、行ごと・関数ごとの言い換えもしない。
- register の offset・bit・表（DDI の buf trans、PLL・clock の表など）はデータとして独自の名前で残し、block ごとに「Value(s) referenced: <版> <path> (<名前>)」を付ける。file の上部には出典を書かない。
- 外向きの口（display の API、Gen9 と共有の部分）は保つ。
- 書き直しの後、`Derived from the Linux kernel` と Linux の MIT の表示は Gen12 の file から消え、`userland/base/licenses/i915-driver/` は WS244 の最後で整理する。
- 完了（対象の機種の方針で読み替え: 5330 だけでなく display 12・13 の対象機種で）: 5330 の実機で、起動・GOP の引き継ぎ・内蔵 panel（eDP）・backlight・外部の DP/HDMI（USB-C を含む）・hotplug・mode set・suspend の戻りが今と同じに動く（ユーザーの確かめ、試験の一覧に載せる）。

## 実装の開始とロールバックの体制（2026-10-15 夕）

ユーザー:「WS245は、ロールバックできる体制にしておいて、今から実装します。」
- ロールバックの基点: main 0c25c87a7（WS245 の実装の commit が入る前）。Phase ごとに別の merge commit にするので、`git revert -m 1 <merge>` で Phase 単位に戻せる。
- kernel の振る舞いは p009 まで変えない: p002・p004〜p008 は新しい file と host 試験だけ。p003 の adapter 以降も `i915.display=old` が既定で、xd は opt-in（design.md §7 #0、既定の切り替えは p011）。`old` は p013 まで逃げ道。
- §7 の判断は既定の案で進める。#18（2026-10-15 ユーザー、クリック「今のまま（efifb）」）: RKL・ADL-S/RPL-S は device.c に足さず今の振る舞い（efifb）を保ち、xd の platform の表にだけ持つ。#22（2026-10-15 ユーザー、クリック）: Gen9 の実機は SV7（10.0.10.27）。
- merge の時期（§7 #13、2026-10-15 夕 ユーザー「WS245はいつでもロールバックできるので、今マージします」）: デモの後まで待たず、p003 以降も Phase の成果ができ次第 main に merge する（既定の案の「p003 以降はデモの後」を置き換え）。戻す時は Phase の merge commit を revert する。

## 進め方の改め（2026-10-15 夜 ユーザー、作業の即時中断の後）

ユーザー:「待ってください、書き換えが一度に大幅すぎます。すべての作業を即時中断してください。」に続けて「まず、Intel GPUのディスプレイコントローラについては、ゴールとしてはSkylakeからRaptor Lake Refreshまですべてで動くようにします。ただし、各ステップで5330で動くかどうかをきちんとリグレッションテストしながら、ステップで書き換えていきます。そうでないと破綻します。」
- design.md §6.2 の Phase の分け方（p002・p004〜p008 を host 試験だけで新しい file として作り、p009 で xd に切り替える）は、この方針に合わない。小さな段ごとに動いている道（5330）を書き換え、各段で 5330 の回帰の試験を通してから次へ進む形に、Phase の表を作り直す。
- 既に main に入った p002（土台）・p004（VBT・EDID）の新しい file（呼ばれない）と、止めた p006 の途中の差分（p1 の worktree、未 commit）の扱いは、作り直しの中で決める（ユーザーに確かめる）。
- 作り直すまで WS245 の実装は止める。

## 5330 の素の起動の確かめ（2026-10-15 夜）

- U10（`build/uat/ws245-amd64/hdd-image.img`、main 1716c14bc、p002・p004 の merge を含む）: 普通の起動で graphical の login・session・compositor・内蔵 LCD・backlight（brightness=100）、GPU の投入 2〜4.6 ms、device lost 0。ユーザー「問題なく使えてます。」→ 今の main は 5330 で動く（passthrough は VBT が無く panel が点かないので使わない、ユーザー「ベアメタルにしましょう」）。
- kernel の binary の比べ（Q1、sha256）: p002 の 2 つ目以降と p004 の xd の file は呼ばれず binary を変えない。binary を変えたのは p002 の 1 つ目（9e78e62f8、own の型の分離と Gen9 の移設）と AX211 の整理。
- 観測（未解析）: Ctrl+Shift の console の起動で `/bin/wayland` を ssh から root で手で起動すると、GPU の仕事が 1 回約 2 秒（GPU 自身は 16 ms）で時間切れ → device lost。普通の起動では出ない。最初の普通の起動（boot-000001、log が 0 byte）で backlight が消えて何も写らなかった原因は不明。

## merge の時に Q1 が消す物（全部の段の試験の後、branch を main に入れる時）

- p025: src/drivers/gpu/i915/display/dc9.c・dc9.h（本番の呼び手 0、vmunix.mk から外し済み）、plan/ws052/tests/host-i915-dc9.c と plan/ws052/tests/Makefile の i915-dc9 の規則（期待値は host-xd-power-test の DC9 の check へ移した）。
- p026: src/drivers/gpu/i915/display/dmc.c（中身は消して vmunix.mk から外し済み）。
- p029: src/drivers/gpu/i915/display/phy.c・phy.h（中身は消し、vmunix.mk から外し済み）。
- p037: src/drivers/gpu/i915/tests/display/host-native-decide-test.c（中身は消した）、plan/ws084/tests/run-native-decide-host-test.sh（参照されない）。
- p050d4a（Q1 の判断 2026-10-17）: P1 が中身を空にした display の file と ktest の file（P1 の branch の ws.md の一覧）を消す。plan/ws051/tests/host-dkl.sh・host-dkl-test.c・host-dkl-linux.c は master の Tools にも未完の Phase にも無く、消した関数を呼ぶので削除（試験の整理の基準）。plan/ws031/tests/vkloop-hw.sh の display・display_ktest・display_ktest2 の場面と plan/tools/hw5330/README.md の 302〜303 行の該当の記述は、その組が無くなるので外す（main の側で Q1 が直す）。
- p050d4b: plan/ws051/tests/host-dpext-link.sh・host-dpext-link.c（消した compute_dp_ext を呼ぶ、oracle は host-output-link-test に移した）を削除。plan/ws084/tests/reboot-loop.sh の 10・71・82 行の log の文字列 'LCD-B preflight' を 'resident display: preflight' に直す（merge の時に main の側で Q1）。
- p050d4c・d4d: src/drivers/gpu/i915/display/takeover.c・takeover.h・aux.h・panel.h・vblank.c・vblank.h（中身を空にした）。
- p050d4d（Q1 の判断）: plan/ws051/tests/host-n1-tc.c・host-n1-tc.sh（この段で壊れる）と host-power-map.c・host-power-map.sh（前から壊れていた）は master の Tools・未完の Phase に無いので削除。
- p051（Q1 の判断）: plan/ws113/tests/host-head-rules.c・.sh（この段で壊れる）と host-gop.c・.sh（前から壊れていた）は master の Tools・未完の Phase に無ければ削除（merge の時に確かめる）。
- p052（Q1 の判断）: WS051 の host-tc の 112 checks を xd の試験に移したら、plan/ws051/tests/host-tc.sh と関係の file を消し、master の Tools の host-tc の行を run-xd-host-tests の行に替える（merge の時に main の側で Q1）。
- q1229-i14 の返却（2026-10-17、Q1 の判断）: plan/ws051/tests/host-dpext.sh・host-dpext.c は base で既に link error で参照も無いので、試験の整理の基準で merge の時に消す。p042 の段の縮約は (a)（GT と display の挟まりは変えず、隣り合う段だけまとめて own の名前にする。device.c の並べ替えは 5330 の起動を変える危険があるため取らない）。
- p042（2026-10-17、Q1 の判断、merge の時に main で）: `src/drivers/gpu/i915/display/diagnostics.c/.h` を消す。plan/ws075/tests/hdmi-h1-hw.sh（master の Tools にある）の grep の `LCD-B` を `display run` に直す。plan/ws075/tests/hdmi/usb-poll.sh は master・tests/・未完了の Phase から参照が無いので直さずに消す。plan/bugs/BUG-266/tests/host-trace-tc.c・run-host-trace-tc.sh（BUG-266 は resolved）は消し、BUG-266.md の参照の行を外す。
- p043（2026-10-17、Q1 の判断）: power.c・watermark.c・clock.c に Linux 由来の部分が残るため、p043a（試験の整理・intel/ の header・PCH の判定・clock.c を畳む・出典の監査）と p043b（watermark.c の帯域を xd の部品に、power.c の domain 層を xd-power に）の 2 段に分ける。ユーザーの方針「まずはぜんぶ実装」に従い、p043b も 5330 の試験の前に実装する。帯域と power は二分探索のため段の境を分ける。
- q1229-i15 の返却（2026-10-17、Q1 の判断）: (1) host-capture-test.c は design §6.4 どおり残し、p046 で run-xd-host-tests に登録する（build できなければ消す）。(2) 5330 の dump の名前と比べる試験は作らない（重なりが 5 組だけ、値の照合は check-xd-regs-linux.sh）。(3) system agent の数は書き換えの間は前と同じ ADL-P の値のまま（振る舞いを変えない）。platform ごとの値は p044 の platform の行で行う。plan/ws075/tests/hdmi/usb-poll.sh は前の判断どおり直さずに消す。
- q1229-i16 の返却（2026-10-17、Q1 の判断）: 実装の段は全部済み（tip db95b5a6c）。T1 に 5330 の素の起動の回帰を 1 回頼む（phase046 の「T1 に渡す実機の確かめ」の 1〜5）。(1) p046 の全文の目視の読み通しは試験を止めずに後で conformance-reviewer に file を分けて頼む。それまで p046 は cleared にしない。(2) style-check.py が lock の境を認めない 44 件は、道具の直し（plan/tools）として残件に置く。(3) gen9-*.c・bootfb.c の R11 は WS244・WS251 の範囲のままにする。(4) plan/ws031/ws.md:175 の `run-capture-host-test.sh` は merge の時に Q1 が run-xd-host-tests の host-capture-test に直す。merge の時に消す物の追加: display/power.c・power.h・dc9.c・dc9.h。
- q1229-i19 の返却（2026-10-17、Q1 の判断）: 5330 の差の不具合 1 件（takeover で AUX A・DDI IO の well を持たず D3 が 160 回 timeout、p050a で入った物）を 26b307bd9 で直した。T1 に再試験（debug の比べ・log・reboot・素の起動）を頼む。(1) DC6 の判断が serve の時の写しのままで、DMC が後に載ると DC6 に入らない件（main と共通、今回の差の原因ではない）は、書き換えの merge の後に別の段で直す（実機の振る舞いが変わるので 5330 の確かめと一緒に）。(2) display 11 の takeover が firmware の DDI の select を display 12 の field（0x78000000、display 11 は 0x70000000）で読む件は p045 の残りに入れる。
- p040（P6、agent/p6-ws245 の 806e0b063、p044 は aa14ff623）: src/drivers/gpu/i915/display/dkl-phy.c・dkl-phy.h・src/drivers/gpu/i915/intel/dkl.h（中身は空）。master の Tools の plan/ws051/tests/host-tc.sh が dkl-phy.c を grep/sed する 2 行と host-tc-tables.c の DKL の 3 check は、窓の check が host-xd-tc-test に移ったので merge の時に消す。
- p028〜: master の Tools の plan/ws051/tests/host-vbt-pll.sh が takeover.c の PLL の表を読めず FAIL になる（表は xd の platform の表へ、PLL の check は host-xd-dpll-test へ移った）。merge の時に道具から adlp_plls の部分を外すか DVO だけの道具にし、Tools の行を直す。

## 全部を実装してから試験（2026-10-16 夕 ユーザー）

ユーザー:「i915は、実装だけ先に終わらせ、全部マージしたものをテストして、動かなければ二分探索で半分マージしてテスト、だめならさらに半分、で進めます。まずはぜんぶ実装してください。」→ p020 以降の残りの段を全部実装して branch に積み、tip で 5330 の回帰（T1）。FAIL なら二分探索。下の「まとめて試験」は置き換えた。

## まとめて試験（2026-10-16 夕 ユーザー、上で置き換え）

ユーザー:「i915は、実装だけ先に終わらせて、10 phase分くらいまとめてマージしてテスト、という流れで加速しましょう。」→ 約 10 段を branch に積み、まとめて T1 が 5330 の回帰を 1 回流し、合格で main へ（guardrail）。最初のまとめは p019〜（p019 は T1-704 で単独に流し中、その結果はそのまま使う）。

## 5330 の素の起動での段ごとの回帰の手順（2026-10-15 夜 Q1 が実施して確かめた形）

1. 段の kernel を build（`make -j16 ZEDBSD_CONFIG=config/ci/config-amd64.mk BUILD=… vmunix`）。userland を変えない段は kernel だけを差し替える。
2. 5330 が Linux（chaos、`ssh solaris10-man`）の時: `sudo mount /dev/sda2 /mnt/zb`（USB の ZEDBSD 区画）→ 前の `vmunix` を控えて段の kernel を `vmunix` に写す → umount → `sudo efibootmgr -n 0005`（USB の UEFI の entry、1 回だけ）→ `sudo systemctl reboot`。zedBSD の時は `ssh kei@10.0.30.3`（password kei、sudo も kei）で `/boot/boot0/` に写して `reboot`（再起動は既定の Linux に戻るので、続けて上の BootNext を使う）。
3. zedBSD に ssh で入り、`sysctl kern.boot.login`（graphical）、`/var/log/sessiond.log`（SESSION start・HANDOFF）、`dmesg` の i915 の行（`lease 1 claimed`・`picture up`（大文字小文字を区別しない grep）・`perf: … per submit`・`device lost` が 0）、backlight（`backlight-probe -f /dev/backlight/backlight0`、Q1 の `build/q1/blp/bin/backlight-probe`）を見る。LCD の目視はユーザー（頼まず、一覧に）。
4. 5330 の DHCP が取れない事がある（2026-10-15、ユーザーが再取得して回復）。ssh が届かない時は止まったと決めつけず、ユーザーに知らせる。
5. **書き込みの全部の列が要る時だけ**（design.md §6.3。survey 1 の digest が基準と違った段）: Linux から USB の EFI の区画を mount し、zedBSD の loader の設定（`zedbsd-native-uefi.cfg` から作られる 1 行 1 語の file。build の `ZEDBSD_BOOT_EXTRA_LINES` と同じ経路）を控えてから `i915.debug=display` の 1 行を足す。同じ語の行が既にあれば足さない。見終わったら控えに戻す。
6. **survey**（p047 から）: `plan/ws245/tests/5330-survey.sh <host> <記録の名前>` が ssh で dmesg・sysctl・sessiond.log・backlight-probe を取り `plan/ws245/tests/5330-<段>-<日付>.md` に写し、`5330-survey-diff.py <基準> <段>` が survey と digest の差と判定（PP_STATUS on、PWM、frame counter が 3 以上、SURFLIVE、CRC が 0 でない、underrun 0、TRANS_CONF）を出す。

## レビュー 4 への判断（2026-10-15 夜 ユーザー、クリック）

- 内蔵 LCD に映っているかの確かめ（レビュー 4 の B3）:「register などの証拠で代える」→ 各段の回帰で panel の電源（PP_STATUS）・backlight の PWM・frame counter・表示中の buffer（SURFLIVE）・pipe の CRC を ssh で読み、段の前後で比べる。目視は節目だけユーザーの一覧に。
- HDMI・USB-C DP の外部 monitor の常設（M7）:「常設できない」→ 外部出力は節目だけユーザーの一覧で確かめる。
- 段の kernel で止まった時（M8）:「その都度自分で入れ直す」→ 止まったら Q1 がユーザーに知らせる。

## §7 の回答（2026-10-15 夕 ユーザー、1 件ずつクリック）

| # | 回答 | 既定の案との差 |
| --- | --- | --- |
| 0 | 「もっと早く（p009 から）」: xd を既定にするのは p009 から | 変更（既定は p011 だった） |
| 1 | 「案のとおり（DVI mode）」 | — |
| 2 | 「案のとおり（変えない）」: mode set で CDCLK を変えない | — |
| 3 | 「案のとおり」: PSR off、DC6 は DMC の時、DC3CO 無し | — |
| 4 | 「案のとおり」: storm は rate の上限だけ | — |
| 5 | 「案のとおり」: OpRegion の runtime 無し | — |
| 6 | 「案のとおり（引用で可）」 | — |
| 7 | 「案のとおり（合成）」 | — |
| 8 | 「早く消す」: 旧 code と adapter は xd を既定にした時点（p009 以降）で早く消す | 変更（p013 まで残す案だった）。逃げ道は git の revert（「WS245はいつでもロールバックできる」） |
| 9 | 「案のとおり（支える）」: eDP 1.4 の rate の表 | — |
| 10 | 「案のとおり（置き換える）」: ktest-display を小さな ktest に | — |
| 11 | 「PRM を出典にも使う」: Intel の PRM の値を直に引用してよい（Notices の条項を受け入れる） | 変更（照合だけの案だった） |
| 12 | 「この WS で縮める」: display.h の段を own の段（start・serve・suspend・resume・stop）に WS245 の中で縮める | 変更（WS244 で、の案だった） |
| 13 | 「今マージします」（別に回答済み） | 変更 |
| 14 | 「案のとおり（支えない）」: ADL-P A0 | — |
| 15 | 「案のとおり（付け替えて残す）」: 5330 の passthrough の scenario 7 つ | — |
| 16 | 「案のとおり（立て直す）」: WS051・WS113・WS084 の display の Phase | — |
| 17 | 「案のとおり（入れる）」: TGL の TC port を p010 に | — |
| 18 | 「今のまま（efifb）」（別に回答済み） | — |
| 19 | 「案のとおり（支えない）」: RKL＋CMP | — |
| 20 | 「別の WS を今立てる」: RKL・ADL-S・ICL の GT は WS245 では起こさず、[WS252](../ws252/ws.md) を作った | 変更（別の WS を今作る） |
| 21 | 「全部 xd を既定に」: 実機で確かめていない platform も xd を既定にする（device.c が attach する物。RKL・ADL-S/RPL-S は #18 で attach しない） | 変更（opt-in の案だった） |
| 22 | 「SV7（10.0.10.27）」（別に回答済み） | — |

→ design.md の改訂 4 が要る（#0・#8・#11・#12・#21、Phase の表と p009〜p013 の受け入れ）。p001 はこの回答で判断が揃ったので、改訂 4 の後に cleared。

## Q1 の判断（q1229-i15 の返却、2026-10-17。main の ws.md から写し）

(1) host-capture-test.c は design §6.4 どおり残し、p046 で run-xd-host-tests に登録する（build できなければ消す）。(2) 5330 の dump の名前と比べる試験は作らない（重なりが 5 組だけ、値の照合は check-xd-regs-linux.sh）。(3) system agent の数は書き換えの間は前と同じ ADL-P の値のまま（振る舞いを変えない）。platform ごとの値は p044 の platform の行で行う。plan/ws075/tests/hdmi/usb-poll.sh は前の判断どおり直さずに消す。

## Phase（design.md §6.2、改訂 4。p001 の clearance で確定）

改訂 3a の p003・p005〜p015 は、2026-10-15 夜のユーザーの方針（「進め方の改め」）で**段ごとの置き換え**の表（p016〜p046。改訂 5 で p047〜p052 を足し、表は実行の順）に置き換えて cancel した（disposition canceled。ID と履歴は残す。p006 は q1207 の attempt が途中で止まり uncleared: 差分は temp に保存、design.md §6.7）。各段の受け入れは「host 試験 + build warning 0 + 5330 の素の起動の回帰（上の手順 1〜3 と p047 の survey の基準との差）に合格 → main に merge」。内蔵 LCD は survey の register の証拠（レビュー 4 への判断）、目視・外部の出力・suspend は節目だけユーザーの一覧（design.md §6.1 R9）。

| ID | 内容 | 状態 |
| --- | --- | --- |
| [p001](phase001/phase.md) | 設計: display 11〜13 の model、外向きの口と契約、データの表の引用の方法、段の分割と順序、試験の道、LW。design-reviewer を通す（5 回） | cleared（2026-10-16 Q1、design.md 改訂 6） |
| [p002](phase002/phase.md) | 土台: register と表の header、xd-io、xd-platform、host の試験の枠と own の register の model、own の型の分離、Gen9 の own への移設 | cleared（2026-10-15、T1-699 PASS、U10 で 5330 の素の起動に合格） |
| p003 | （改訂 3a）口 `xd.h` と adapter、`i915.display=` | **canceled**（2026-10-15 夜、段ごとの置き換えに置き換え。adapter と boot の語は作らない: design.md §6.1 R5） |
| [p004](phase004/phase.md) | VBT（合成の fixture）・EDID の解析（呼ばれない部品） | cleared（2026-10-15、host 試験、main 2d5b167aa、U10 で 5330 に合格） |
| p005 | （改訂 3a）AUX・GMBUS | **canceled**（→ p019・p020） |
| p006 | （改訂 3a）power・DMC・clock・TC の register | **canceled**（→ p023〜p028・p040。q1207 の attempt は uncleared: 途中の差分は `temp/p006-partial-20261015.patch` と `temp/p006-partial-20261015-untracked/`、design.md §6.7） |
| p007 | （改訂 3a）DDI・DP・panel | **canceled**（→ p021・p022・p029〜p031） |
| p008 | （改訂 3a）pipe・DBUF | **canceled**（→ p032〜p035） |
| p009 | （改訂 3a）screen・takeover・xd の opt-in | **canceled**（→ p036〜p038） |
| p010 | （改訂 3a）hotplug・Type-C・外部 DP・HDMI・head | **canceled**（→ p039・p040） |
| p011 | （改訂 3a）suspend・既定の切り替え | **canceled**（→ p041。既定の切り替えは無い） |
| p012 | （改訂 3a）platform の網羅 | **canceled**（→ p044） |
| p013 | （改訂 3a）旧 code の削除と出典の監査 | **canceled**（→ 各段で消す + p043） |
| p014 | （改訂 3a）display 11 | **canceled**（→ p045） |
| p015 | （改訂 3a）規約の全文 | **canceled**（→ p046） |
| p047 | 回帰の証拠の道具: own の survey（PP_STATUS・PWM・frame counter・SURFLIVE・pipe CRC・underrun・TRANS_CONF などを picture up の後に dmesg へ）と `mmio.c` の書き込みの log（`i915.debug=display`）、`5330-survey.sh`（改訂 5、レビュー 4 の B3・M6） | cleared（4 LW。2026-10-16 main 3e1e0c687。LCD の目視は beta2.md の U11） |
| [p016](phase016/phase.md) | OpRegion の場所と map（xd-vbt の find、`struct i915_xd` と io・platform の用意）。opregion.c を消す | cleared（2026-10-16 5330 の回帰に合格、main 9244ac507、旧 file の削除 54fc334c5。SV7 は beta2 の U12） |
| [p017](phase017/phase.md) | VBT の解析（xd-vbt を真に、旧 parser と影の比較を 1 段） | cleared（2026-10-16 5330 の回帰に合格、main 45c83da79） |
| [p018](phase018/phase.md) | 旧 VBT parser の本体の削除（本番の呼び手 10 関数を殻 `vbt-shell.c` に、ktest-display の 2 file を削除）。2026-10-16 に p018 と p053 の 2 段に分けた | cleared（2026-10-16 5330 の回帰に合格、merge 8fea53a1d、旧 4 file は Q1 が削除） |
| [p053](phase053/phase.md) | EDID の decode を xd-edid に（`edid.c` の decode を消し、`state.c` の mode を xd-edid の first timing に。drm_modes の helper は edid.c に殻として残る）。p018 から分けた | cleared（2026-10-16 T1-702 で 5330 の回帰 PASS、全部の列の差 0、merge 7e3bf81cf） |
| [p019](phase019/phase.md) | AUX の transaction（xd-aux。well・DC off・VDD は呼ぶ側） | in-progress（4 LW。2026-10-16 q1225-i02 で実装・host 試験・build 済み（343c8516a）、5330 の回帰を T1 に依頼（Q1 経由）） |
| [p020](phase020/phase.md) | GMBUS と DDC の EDID の読み（xd-gmbus） | in-progress（3 LW。2026-10-16 q1228-i02 で実装・host 試験・build 済み、5330 は全部の段の後に 1 回。HDMI の plug はユーザーの一覧） |
| [p021](phase021/phase.md) | backlight の PWM（xd-panel） | in-progress（2 LW。2026-10-16 実装・host 試験・build 済み、5330 は全部の段の後に 1 回） |
| [p022](phase022/phase.md) | panel power（PPS、xd-panel） | in-progress（3 LW。2026-10-16 q1229-i01 で実装・host 試験・build 済み、5330 は全部の段の後に 1 回） |
| [p023](phase023/phase.md) | power well の操作（W1、xd-power。p006 の差分を当て直す） | in-progress（5 LW。2026-10-16 q1229-i01 で実装・host 試験・build 済み、5330 は全部の段の後に 1 回） |
| [p024](phase024/phase.md) | well の所属（W3）と refcount（xd-power） | in-progress（4 LW。2026-10-16 q1229-i02 で実装・host 試験・build 済み、5330 は全部の段の後に 1 回。節目: HDMI・USB-C DP の 2 画面、5320 はユーザーの一覧） |
| [p025](phase025/phase.md) | DC state と DC9（xd-power、dc9.c を消す） | in-progress（3 LW。2026-10-16 q1229-i02 で実装・host 試験・build 済み、5330 は全部の段の後に 1 回。dc9.c・dc9.h と WS052 の host-i915-dc9 の削除は Q1 に依頼） |
| [p026](phase026/phase.md) | DMC（xd-dmc） | in-progress（4 LW。2026-10-16 q1229-i02 で実装・host 試験・build 済み、5330 は全部の段の後に 1 回。dmc.c の削除は merge の時に Q1） |
| [p027](phase027/phase.md) | CDCLK（xd-clock） | in-progress（3 LW。2026-10-16 q1229-i02 で実装・host 試験・build 済み、5330 は全部の段の後に 1 回。節目: suspend → resume、5320 の起動はユーザーの一覧） |
| [p028](phase028/phase.md) | combo DPLL（xd-clock） | in-progress（5 LW。2026-10-16 q1229-i03 で実装・host 試験・build 済み、5330 は全部の段の後に 1 回。節目: 5320 の起動。`host-vbt-pll.sh` の PLL の check は host-xd-dpll-test に移した: Tools の行は Q1） |
| [p029](phase029/phase.md) | combo PHY（xd-ddi: init・lane・swing） | in-progress（4 LW。2026-10-16 q1229-i03 で実装・host 試験・build 済み、5330 は全部の段の後に 1 回。phy.c・phy.h の削除は merge の時に Q1） |
| [p030](phase030/phase.md) | DDI の buffer（xd-ddi: BUF_CTL・DP_TP・idle） | in-progress（4 LW。2026-10-16 q1229-i03 で実装・host 試験・build 済み、5330 は全部の段の後に 1 回） |
| [p031](phase031/phase.md) | DP の訓練（xd-dp） | in-progress（6 LW。2026-10-16 q1229-i04 で実装・host 試験・build 済み、5330 は全部の段の後に 1 回。節目: USB-C DP と HDMI の 2 画面はユーザーの一覧） |
| [p032](phase032/phase.md) | transcoder と pipe（xd-pipe） | in-progress（5 LW。2026-10-16 q1229-i04 で実装・host 試験・build 済み、5330 は全部の段の後に 1 回） |
| [p033](phase033/phase.md) | plane と flip（xd-pipe） | in-progress（3 LW。2026-10-16 q1229-i04 で実装・host 試験・build 済み、5330 は全部の段の後に 1 回） |
| [p034](phase034/phase.md) | DBUF と watermark（xd-dbuf） | in-progress（7 LW。2026-10-16 q1229-i04 で実装・host 試験・build 済み、5330 は全部の段の後に 1 回。HDMI の 2 画面はユーザーの一覧） |
| [p035](phase035/phase.md) | display の init/uninit の列（xd-core、司令） | in-progress（4 LW。2026-10-16 q1229-i04 で実装・host 試験・build 済み、5330 は全部の段の後に 1 回。5320 の節目。takeover の WA・HTI・GMBUS reset は p036・p037 へ） |
| [p036](phase036/phase.md) | GOP の読み出しと判断（N0、xd-takeover。旧 N0 と影の比較） | in-progress（4 LW。2026-10-16 q1229-i05 で実装・host 試験・build 済み、5330 は全部の段の後に 1 回） |
| [p037](phase037/phase.md) | 引き継ぎ（N1）の判断と GOP の出力の止め方（xd-takeover。readout・setup_outputs・n1 の helper は p050 まで旧） | in-progress（5 LW。2026-10-16 q1229-i05 で実装・host 試験・build 済み、5330 は全部の段の後に 1 回） |
| [p038](phase038/phase.md) | screen の plan（純関数。eDP・HDMI・DP-ext の 3 種を同じ段で） | in-progress（5 LW。2026-10-16 q1229-i05 で実装・host 試験・build 済み、5330 は全部の段の後に 1 回。拒む 3 条件の判断は旧の check、plan は影） |
| [p048](phase048/phase.md) | screen の enable/disable の列（3 種を同じ段で。retained） | in-progress（7 LW。2026-10-16 q1229-i05 で実装・host 試験・build 済み、5330 は全部の段の後に 1 回。step の中身は殻、順と失敗の扱いが xd-screen） |
| [p049](phase049/phase.md) | flip と resident run の契約（EAGAIN・UNPLUGGED・RELIGHT・acquired） | in-progress（4 LW。2026-10-16 q1229-i05 で実装・host 試験・build 済み、5330 は全部の段の後に 1 回） |
| [p050a](phase050a/phase.md) | takeover の readout と sanitize を xd-takeover の record と stop に（takeover.c の registry を消す）。2026-10-16 Q1 の判断で p050 を 4 段に分けた（design の 4 LW を大きく超えるため） | in-progress（2026-10-16 q1229-i06 で実装・host 試験・build 済み、5330 は全部の段の後に 1 回） |
| [p050b](phase050b/phase.md) | eDP の step の本体を xd の部品（PPS・backlight・AUX・DDI の IO power・訓練の DPCD・WM/DBUF の書き込み・vblank の event）を直に呼ぶ形に | in-progress（2026-10-17 q1229-i07 で実装・host 試験・build 済み（aa4374975、新しい xd-output.c と殻 output-shell.c）、5330 は全部の段の後に 1 回） |
| [p050c](phase050c/phase.md) | HDMI と DP-TC の step の本体 | in-progress（2026-10-17 q1229-i07 で実装・host 試験・build 済み（81332cf8e）、5330 は全部の段の後に 1 回。hdmi-mode.c/.h の削除は merge の時に Q1） |
| [p050d](phase050d/phase.md) | 口 `xd.h` と own の file の付け替え（backlight.c を含む）、旧 file（modeset・state・ddi・dp-sink・pipe・plane・aux・panel・panel-backlight・vblank・takeover の残り・internal.h の Linux の型）と旧 host 試験（host-dp-test・host-lcd-modeset-test）の削除、oracle の check を xd の試験へ | 4 段に分けた（2026-10-17 Q1、q1229-i08）: 下の p050d1〜d4 |
| [p050d1](phase050d1/phase.md) | 内蔵 panel の sink の取得（PPS の用意・VDD・DPCD・EDID）と kernel の panel・DPCD の hook を xd の部品に（旧の Linux の eDP の world は旧 host 試験の間残す） | in-progress（2026-10-17 q1229-i08 で実装・host 試験・build 済み（3604ddfa1、新しい xd-edp.c、I2C over AUX、EDID の読み）、5330 は全部の段の後に 1 回） |
| [p050d2](phase050d2/phase.md) | prepare の check（state.c の compute・CDCLK）と WM/DBUF を xd-screen の plan・xd-dbuf に | in-progress（2026-10-17 q1229-i10 で実装・host 試験・build 済み（38eef56d3）、5330 は全部の段の後に 1 回） |
| [p050d3](phase050d3/phase.md) | plane・flip・vblank を xd-pipe・xd-screen に | in-progress（2026-10-17 q1229-i10 で実装・host 試験・build 済み（c93202cf5）、5330 は全部の段の後に 1 回） |
| [p050d4](phase050d4/phase.md) | 口 xd.h と own の file の付け替え、旧 file と旧 host 試験・ktest の削除（oracle を xd の試験へ） | 4 段に分けた（2026-10-17 Q1、q1229-i11）: 下の p050d4a〜d4d |
| [p050d4a](phase050d4a/phase.md) | 画面の口: 新しい xd-picture.c に画面の object と prepare・light・dark・plane・flip・status・retained・backlight を own の型で、head.c・present.c・backlight.c を付け替え | in-progress（2026-10-17 q1229-i11 で実装・host 試験・build 済み（86041583b、新しい xd-backend.h・xd-picture.c と host-xd-picture-test、display の ktest の組を外した）、5330 は全部の段の後に 1 回） |
| [p050d4b](phase050d4b/phase.md) | 走らせ方の口: resident run・show run・kernel の binding を own の resident.c へ（fill が picture の config を直に）、output.c の link を xd-screen の plan_link に | in-progress（2026-10-17 q1229-i12 で実装・host 試験・build 済み（9915b53dc、新しい resident.c/h と host-output-link-test、modeset.c は world だけ）、5330 は全部の段の後に 1 回） |
| [p050d4c](phase050d4c/phase.md) | 始まりと終わり: port の表（xd）、eDP の device を own の edp-device.c へ、始まりの前半を xd の部品で、VGA を own の vga.c へ、takeover.c を空に | in-progress（2026-10-17 q1229-i12 で実装・host 試験・build 済み（43c3ed47c・f491cfcad）、5330 は全部の段の後に 1 回。world の削除は d4d へ） |
| [p050d4d](phase050d4d/phase.md) | 旧 file の削除と旧 host 試験・fake の終わり（size、`I915_XD_STRICT`、5320・SV7・QEMU の節目） | in-progress（2026-10-17 q1229-i12 で一部（854bd85fb）、q1229-i13 で残りを実装・host 試験・build 済み（f25557317: modeset・state・ddi・dp・pipe・plane・panel・edid を空に、旧 host 試験と fake を空に、internal.h の Linux 型を削減、display-host-lib.sh に mmio-record.c）、5330 は全部の段の後に 1 回） |
| [p039](phase039/phase.md) | hotplug（xd-hpd。hotplug.c を消す） | in-progress（6 LW。2026-10-17 q1229-i13 で実装・host 試験・build 済み（02dc4b5c8: 新しい xd-hpd.c と own の hpd.c、hotplug.c・hdmi.c・gmbus.c を空に、host-xd-hpd-test）、5330 は全部の段の後に 1 回。節目: HDMI の plug で 2 画面はユーザーの一覧） |
| [p051](phase051/phase.md) | own の output.c の port の規則と head-rules の pipe の規則を platform の記述に（hdmi.c・hdmi-mode.c・head-rules.c を消す） | in-progress（3 LW。2026-10-17 q1229-i13 で実装・host 試験・build 済み（1388dbae8: GOP の分類と HDMI の port を platform の記述で、output の pipe、head-rules を head.c に）、5330 は全部の段の後に 1 回） |
| [p040](phase040/phase.md) | Dekel の窓と TC PHY、TC PLL（xd-tc の窓を dkl-phy.c と一緒に入れ替える、clock.c の残りを消す） | in-progress（5 LW。2026-10-17 P6 が q1236-i01 で並行に実装・host 試験・build 済み（806e0b063）。P1 の branch に merge（bd91827a5）し、xd-output の Type-C の DP mode・levels・PHY の WA・自前の TC PLL・swing の上限を xd-tc・xd-clock の直の呼びにした（6cbff0b73）。5330 は全部の段の後に 1 回。WS051 の host-tc.sh の DKL の行の直しは Q1） |
| [p052](phase052/phase.md) | Type-C の状態機械（流れ A）と USB-C の DP（UCSI、DP-ext の probe。tc.c・tc-kern.c・dp-ext*.c を消す） | in-progress（6 LW。2026-10-17 q1229-i13 で 1 つ目（200128cc0: 外部 DP の AUX を xd-aux に、aux.c・dp-sink.c・edid-read.c・dp-internal.h・vbt.h を空に）、q1229-i14 で残り（e92b2bee0: xd-tc-port.c の状態機械・own の tc-port.c・host-xd-tc-port-test、e466cb254: probe を xd-dp.c に）、5330・5320 は全部の段の後に 1 回。intel/ の header は p043。節目: USB-C の plug はユーザーの一覧） |
| [p041](phase041/phase.md) | suspend の戻り（xd-display） | in-progress（4 LW。2026-10-17 q1229-i14 で実装・host 試験・build 済み（4045c6b0b）、5330 は全部の段の後に 1 回。suspend はユーザーの一覧） |
| [p042](phase042/phase.md) | display.c の段の縮約（§7 #12）・log の文言・xd-diag | in-progress（4 LW。2026-10-17 q1229-i15 で実装・host 試験・build 済み（93ec4f94a: 段の縮約は Q1 の判断 (a)、DRM・PM demand・modeset/flip の workqueue を消した、diagnostics.c を xd-diag.c と own の run-log.c に、log の一覧は design §6.4.1）、5330・SV7 は全部の段の後に 1 回） |
| [p043](phase043/phase.md) | 旧 code の残りの削除・試験の整理・出典の監査 | 2 段に分けた（2026-10-17 Q1、q1229-i15）: 下の p043a・p043b |
| [p043a](phase043/phase.md) | 試験の整理・intel/ の Gen12 だけの header・PCH の判定・clock.c・§5.4 の監査 | in-progress（2026-10-17 q1229-i15 で実装・host 試験・build 済み（35b24c912・7de79d444）、5330・SV7 は全部の段の後に 1 回） |
| [p043b](phase043/phase.md) | watermark.c の帯域を xd に（段の境 1）、power.c の domain 層を xd-power に（段の境 2）、Derived の表示の削除 | in-progress（2026-10-17 q1229-i15 で帯域を実装・host 試験・build 済み（d16688474）、q1229-i16 で power を実装・host 試験・build 済み（57967dfb0: own の display-power.c）、5330・SV7 は全部の段の後に 1 回） |
| [p044](phase044/phase.md) | platform の網羅（display 12 の行）と TGL の TC（流れ B、host だけ）。#21 で全部 xd | in-progress（4 LW。2026-10-17 P6 が q1236-i01 で一部（流れ A・B の部品と connect、TGL の TC_cold_off、kind の分岐の除去、9 行の plan の試験）を実装（aa14ff623）、P1 の branch に merge（bd91827a5）。残り: 9 行 × 出力の enable/disable の列の比べ、RKL・ADL-S の takeover の列、流れ B の配線（p052）。5320 はユーザーの一覧） |
| [p045](phase045/phase.md) | display 11（Ice Lake） | in-progress（8 LW。実機は無い。2026-10-17 P6 が q1236-i02 で部品を実装（c85e8569e）、P1 が q1229-i10 で merge（c3d27ec57）し xd-output に display 11 の分岐（88e99dbe2）。残りは phase045 の「P1 の branch との合わせ」） |
| [p046](phase046/phase.md) | 規約の全文の見直しと p045 の出典の監査 | in-progress（3 LW。2026-10-17 q1229-i16 で機械の規則の全部と出典の監査を実装・host 試験・build 済み（16de8c315）。全文の目視の通読は未実施（Q1 の判断）。5330 の q1229-i18 の差を q1229-i19 で分類し takeover の不具合を直した（26b307bd9）、T1 の再試験待ち） |
合計 38 段、161 LW（±40 %。改訂 6 の 159 に、p018 を p018・p053 に分けた +2: 2026-10-16 q1223）。5330 の素の起動は段の kernel で最少 37 回、基準の起動と `i915.debug=display` の起動を含めて見込み約 50 回（最大 70〜85 回、design.md §6.6）。

### 段の境の SHA（二分探索用、2026-10-16 から。branch agent/p1-ws245）

各段の最後の WIP commit。5330 の回帰で失敗した時は、この表の真ん中の段までを main に merge した kernel で流す。

| 段 | 最後の commit | 親（前の段の境） |
| --- | --- | --- |
| p019 | 343c8516a | fb66c84d0 |
| p020 | 167786574 | 2f34bc94c（p019 の記録の commit。段の source は 343c8516a のまま） |
| p021 | c619c69c0 | 09b7da423 |
| p022 | 92ee105c0 | e58dc6f48（p021 の記録の commit） |
| p023 | 6d4e2f41d | 92ee105c0 |
| p024 | a249461a9 | 9a98be12f（p023 の記録の commit） |
| p025 | 25bdbeb3e | 3a8408e90（p024 の記録の commit） |
| p026 | 8e7b845fd | 92c67726b（p025 の記録の commit） |
| p027 | 9833f6172 | 76fefb632（p026 の記録の commit） |
| p028 | c90812662 | 47d0e157a（p027 の記録の commit） |
| p029 | 7e23af604 | a8cb64e8b（p028 の記録の commit） |
| p030 | baac77f10 | 05775ce1f（p029 の記録の commit） |
| p031 | a10fc0bd5 | f86a468da（p030 の記録の commit） |
| p032 | 975bc6c1a | 55f9b8b59（p031 の記録の commit） |
| p033 | 04b56a2dc | a1a993d8f（p032 の記録の commit） |
| p034 | 56f741b86 | 52261f591（p033 の記録の commit） |
| p035 | 7dc5d1188 | fd3672e16（p034 の記録の commit） |
| p036 | 548e03d0a | bde6f680e（p035 の記録の commit） |
| p037 | 2656d1676 | a5add86c9（p036 の記録の commit） |
| p038 | adbbc09b0 | 2153d2c3a（p037 の記録の commit） |
| p048 | 2589735e4 | 44eecb132（p038 の記録の commit） |
| p049 | 7042a8a03 | 5d36d047b（p048 の記録の commit） |
| p050a | b295e46fd | 3429a6af6 |
| p050b | aa4374975 | 5342e0844（p050a の記録の commit） |
| p050c | 81332cf8e | c569cd427（p050b の記録の commit） |
| p040（P6、branch agent/p6-ws245） | 806e0b063 | 5342e0844（p050a の記録の commit。P1 の p050b・c と並行） |
| p044 の一部（P6） | aa14ff623 | 806e0b063 |
| p040・p044 の merge と Type-C の付け替え（P1） | 6cbff0b73 | bd91827a5（merge: 親 e825d6e6e（p050c の記録の commit）と aa14ff623） |
| p050d1 | 3604ddfa1 | b0c289c9d（p050d1〜d4 の phase.md の commit） |
| p050d2 | 38eef56d3 | 203c3e5fe（p050d2 の再開の手順の commit） |
| p050d3 | c93202cf5 | ef3fc9723（p050d2 の記録の commit） |
| p045（P6、branch agent/p6-ws245） | c85e8569e | aa14ff623（p044 の一部） |
| p045 の merge（P1） | c3d27ec57 | b720306ed（p050d3 の記録の commit）と c85e8569e |
| p045 の xd-output の display 11（P1） | 88e99dbe2 | c3d27ec57 |
| p050d4a | 86041583b | 794c535ff（p050d4a〜d4d の phase.md の commit） |
| p050d4b | 9915b53dc | ec4951551（p050d4a の記録と d4b の再開の手順の commit） |
| p050d4c | f491cfcad | 7211a06ef（p050d4c の段取りの commit。途中の commit 43c3ed47c） |
| p050d4d | f25557317 | d5fe3c20a（途中の commit 854bd85fb・a702fb741・8f2d1f564） |
| p039 | 02dc4b5c8 | c3dea0b0e（p050d4d の記録の commit） |
| p051 | 1388dbae8 | 0b0102e81（p039 の記録の commit） |
| p052 の 1 つ目 | 200128cc0 | fbf1fcda9（p051 の記録の commit） |
| p052 の残り | e466cb254 | 421e5ca1c（p052 の 1 つ目の記録の commit。途中の commit e92b2bee0） |
| p041 | 4045c6b0b | 75cd8f58a（p052 の残りの記録の commit） |
| p042 | 93ec4f94a | 7ac68c76d（p042 の調べの記録の commit） |
| p043a | 7de79d444 | d52337e50（p042 の記録の commit。途中の commit 35b24c912） |
| p043b の帯域（段の境 1） | d16688474 | 5733e73dc（p043a の記録の commit） |
| p043b の power（段の境 2） | 57967dfb0 | 2c49ef470（p043b の帯域の記録の commit） |
| p046（規約と出典、振る舞いは変えない） | 16de8c315 | 641c3b353（p043b の power の記録の commit。途中の commit 0e85d1ff1・a5e461698・a3849a76f） |
| q1229-i19（5330 の差の直し: takeover の AUX・DDI IO の well と VDD、serving の log） | 26b307bd9 | 3bd0bd87f（T1 の q1229-i18 の証拠の commit。親 db95b5a6c） |

### merge の時に Q1 が消す物（段の殻で中身を空にし vmunix.mk から外した file と、範囲の外の試験）

| 段 | path | 理由 |
| --- | --- | --- |
| p025 | `src/drivers/gpu/i915/display/dc9.c`、`src/drivers/gpu/i915/display/dc9.h` | 本番の呼び手 0（xd-power の DC9） |
| p025 | `plan/ws052/tests/host-i915-dc9.c` と `plan/ws052/tests/Makefile` の `i915-dc9` の規則 | dc9.c を compile する試験（期待値は host-xd-power-test に移した） |
| p026 | `src/drivers/gpu/i915/display/dmc.c` | 中身を消した（xd-dmc） |
| p028 | `plan/ws051/tests/host-vbt-pll.sh`・`host-vbt-pll.c` の adlp_plls の部分（master の Tools の行） | takeover.c の表を xd の platform の表に移した。PLL の check は host-xd-dpll-test に（DVO の check は残る: 道具を直すか DVO だけにする） |
| p029 | `src/drivers/gpu/i915/display/phy.c`、`src/drivers/gpu/i915/display/phy.h` | 中身を消した（xd-ddi） |
| p037 | `src/drivers/gpu/i915/tests/display/host-native-decide-test.c`、`plan/ws084/tests/run-native-decide-host-test.sh` | 中身を空にした旧 N0 の試験（xd-takeover の試験に移した）と、その runner（参照なし） |
| p050c | `src/drivers/gpu/i915/display/hdmi-mode.c`、`src/drivers/gpu/i915/display/hdmi-mode.h` | 中身を消した（HDMI の出力は xd-output） |
| p050d4a | `src/drivers/gpu/i915/display/output-shell.c`、`panel-backlight.c`、`panel-backlight.h`、`color.c`、`color.h` | 中身を消した（xd-picture） |
| p050d4a | `src/drivers/gpu/i915/tests/display/` の `lcd-run.c/.h`・`hdmi-output.c`・`lcd-flip.c`・`lcd-gpu.c/.h`・`hdmi-hotplug.c`・`aux.c`・`hpd-model.c/.h`・`display-ktest.c/.h`・`edp-ktest.c/.h`・`edp-sync-ktest.c`・`lcd-modeset-ktest.c`・`scanout-ktest.c`・`lcd-show-ktest.c`・`lcdg-ktest.c`・`hpd-ktest.c` | 中身を空にした（display の ktest の組を vmunix.mk で外した、design §6.4） |
| p050d4a | `plan/ws051/tests/host-dkl.sh`・`host-dkl-test.c`・`host-dkl-linux.c`（判断は Q1） | 参照なし、消した PLL の pool の関数を呼ぶ。`plan/ws031/tests/vkloop-hw.sh` の display の scene と `plan/tools/hw5330/README.md:302-303` の記述の直しも Q1 |
| p050d4b | `plan/ws051/tests/host-dpext-link.sh`・`host-dpext-link.c`（判断は Q1） | 消した `drv_i915_lcd_compute_dp_ext` を呼ぶ（参照なし、oracle は host-output-link-test へ）。`plan/ws084/tests/reboot-loop.sh:10,71,82` の `LCD-B preflight` の grep の直しも Q1 |
| p050d4c | `src/drivers/gpu/i915/display/takeover.c`、`takeover.h`、`aux.h`、`panel.h` | 中身を空にした（port の表・edp-device.c・vga.c・xd の部品へ） |
| p050d4d | `src/drivers/gpu/i915/display/vblank.c`、`vblank.h` | 中身を空にした（resident.c の hook へ） |
| p050d4d | `src/drivers/gpu/i915/display/` の `modeset.c`・`modeset.h`・`modeset-internal.h`・`state.c`・`state.h`・`ddi.c`・`ddi.h`・`dp.c`・`dp.h`・`pipe.c`・`pipe.h`・`plane.c`・`plane.h`・`panel.c`・`takeover-internal.h`・`watermark-internal.h`・`edid.c`・`edid.h`、`src/drivers/gpu/i915/intel/clock.h` | 中身を空にした（xd の部品と output.c・watermark.c へ） |
| p050d4d | `src/drivers/gpu/i915/tests/display/` の `host-lcd-modeset-test.c`・`host-lcd-test.c`・`host-dp-test.c`・`lcd-fake-hw.c/.h`・`dp-fake-hw.c/.h` | 中身を空にした旧 host 試験と model（oracle は xd の試験） |
| p050d4d | `plan/ws051/tests/host-n1-tc.c/.sh`・`host-power-map.c/.sh`（判断は Q1） | 空にした header を読む（host-power-map は base で既に壊れていた）、参照なし |
| p039 | `src/drivers/gpu/i915/display/` の `hotplug.c`・`hotplug.h`・`hotplug-internal.h`・`hdmi.c`・`hdmi.h`・`gmbus.c`・`gmbus.h`、`src/drivers/gpu/i915/intel/hotplug-types.h` | 中身を空にした（xd-hpd と own の hpd.c） |
| p051 | `src/drivers/gpu/i915/display/head-rules.c`・`head-rules.h` | 中身を空にした（判断は head.c） |
| p051 | `plan/ws113/tests/host-head-rules.c/.sh`・`host-gop.c/.sh`（判断は Q1） | 空にした head-rules.c を link する／base で既に壊れていた、参照なし |
| p052 | `src/drivers/gpu/i915/display/` の `aux.c`・`dp-sink.c`・`dp-sink.h`・`edid-read.c`・`dp-internal.h`・`vbt.h` | 中身を空にした（外部 DP の AUX は xd-aux） |
| p052（q1229-i14） | `src/drivers/gpu/i915/display/` の `tc.c`・`tc.h`・`tc-kern.c`・`tc-kern.h`・`dp-ext.c`・`dp-ext.h` | 中身を空にした（xd-tc-port.c・tc-port.c・xd-dp.c の probe） |
| p052（q1229-i14） | `plan/ws051/tests/host-tc.sh`・`host-tc.c`・`host-tc-tables.c`（master の Tools の行 `ws051/tests/host-tc.sh` を run-xd-host-tests の host-xd-tc-port-test に替える） | 検査は host-xd-tc-port-test（Q1 の判断 2026-10-17: 移したら消す）。tables は元の file が p039・p040 で空 |
| p052（q1229-i14） | `plan/ws051/tests/host-dpext.sh`・`host-dpext.c`（判断は Q1） | 空にした dp-ext.h を読む、base で既に link error、参照なし |
| p042 | `src/drivers/gpu/i915/display/diagnostics.c`、`diagnostics.h` | 中身を空にした（xd-diag.c と run-log.c へ） |
| p042 | `plan/bugs/BUG-266/tests/host-trace-tc.c`・`run-host-trace-tc.sh`（判断は Q1） | 空にした diagnostics.h を読む、resolved の bug の試験で参照なし |
| p043a | `src/drivers/gpu/i915/intel/` の `connector.h`・`ddi.h`・`dkl.h`・`dp.h`・`hotplug.h`・`mreg.h`・`phy.h`・`plane.h`・`power-set.h`・`power.h`・`psr.h`・`ref.h`・`ref-inlines.h`・`trans.h`・`vbt.h`・`wm-dbuf.h`・`wm.h`・`fixed.h`・`fourcc.h`、`src/drivers/gpu/i915/tests/display/host-xd-regs-test.c`、`src/drivers/gpu/i915/display/color.h` | 中身を空にした（本番の include は 0、旧 header と比べる試験はそれと一緒に） |
| p043b | `src/drivers/gpu/i915/display/watermark.c`、`watermark.h` | 中身を空にした（帯域は xd-bandwidth.c、ktest-sync の DRAM・帯域の ktest は host-xd-bandwidth-test に） |
| p043b（power） | `src/drivers/gpu/i915/display/power.c`、`power.h`、`dc9.c`、`dc9.h` | 中身を空にした（display-power.c。dc9 は p025 の行と同じ物で、host の display の lib が compile して壊れるので空にした） |
| p040（P6） | `src/drivers/gpu/i915/display/dkl-phy.c`、`src/drivers/gpu/i915/display/dkl-phy.h`、`src/drivers/gpu/i915/intel/dkl.h` | 中身を消した（Dekel の窓は xd-tc）。WS051 の `plan/ws051/tests/host-tc.sh`・`host-tc-tables.c` の DKL の 3 check と master の Tools の行の直しも Q1（phase040） |

