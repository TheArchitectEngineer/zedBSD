<!-- awesome-plan project=zedbsd record=ws245-p044 -->

# ws245-p044: platform の網羅（display 12 の行）と TGL の TC（流れ B、host だけ）

Status: in-progress（2026-10-17 P6: 下の「この attempt で作った物」は実装・host 試験・build 済み。受け入れの「9 行 × {eDP、HDMI、DP-TC、TBT} の **enable → disable の列**」は、screen の step の中身（P1 の p050b・p050c）が xd の部品の直の呼びになるまで作れないので残る。5330 の回帰は全部の段の後に 1 回）
Parent: [WS245](../ws.md)
Queue: q1236-i01（P6。P1 の p050b〜d と並行: 2026-10-16 ユーザー「P1,P3の処理で、並列にオフロードできる実装があれば、それぞれ1エージェントずつ追加してほしいです。…並列な実装は完璧でなくてもよく、あとでマージするときに整合が必要でもOKです。」）
Branch: agent/p6-ws245（p040 の 806e0b063 の上）
設計: [design.md](../design.md) §6.2 の p044 の行、§2.8（流れ A・B）、§3.1（platform の記述、`kind` で分岐しない）、§7 #17・#21

## この attempt で作った物

| file | 中身 |
| --- | --- |
| `display/xd-tc.c/.h` | Type-C の接続の部品（流れ A・B を platform の `tc_flow` で選ぶ）: `drv_i915_xd_tc_fia`（TGL は FIA1 の DFLEXDPSP の modular の bit で、modular なら 2 port ごとに FIA 1〜3、でなければ FIA1。display 13 は modular 固定）、`_live`（TCSS: DE_HPD_ISR の TC/TBT の bit、FIA: DFLEXDPSP の live（全 1 なら TC cold で何も無し）、両方で SDEISR の TC の pin → legacy）、`_ready`（TCSS_DDI_STATUS か DFLEXDPPMS、全 1 は not ready）、`_own/_owned`（DDI_BUF_CTL の bit 6 か DFLEXDPCSSS の not-safe、FIA の全 1 は EIO）、`_pin`（DFLEXPA1）、`_max_lanes`（DFLEXDPSP の lane の割り当て: 1・2・4）、`_set_lanes`（DFLEXDPMLE1: ML0・ML1_0・ML3・ML3_2・ML3_0）、`_cold_functions`（TGL は TC_cold_off の well、display 13 は port の AUX の well で TBT は無し）、`drv_i915_xd_tc_connect/_disconnect`（流れ A: lane の well → 所有 → ready → AUX の well → lane の確かめ → lane の well を返す、失敗は巻き戻し。流れ B: TC cold の block → FIA → ready → 所有 → lane の確かめ。TBT は block だけ。legacy は ready・所有の失敗でも進む（Linux と同じ）。disconnect は流れ A が lane の well の下で block を外して所有を返す、流れ B が所有を返して block を外す）。状態機械（いつ繋ぐか、遅延の disconnect、partner gone、legacy の印の補正、UCSI）は p052 |
| `display/xd-power.c` | TGL の TC_cold_off の well（control が MAILBOX）の enable/disable を GT driver mailbox の TCCOLD の block/unblock に（500 µs、3 回、1 ms おき、block の exit failed は失敗、失敗は report の state_late に記録して続ける）。これまでは EINVAL だった |
| `intel/display-regs-xd.h`、`tests/display/xd-regs-map.h` | DFLEXDPSP の lane の割り当ての field と DFLEXDPMLE1 の ML の値（Values referenced、Linux との対 6 つ） |
| `display/xd-platform.h`、`intel/display-tables-xd.h`、`display/xd-screen.c` | `I915_XD_WA_CDCLK_PIXEL_RATE`（TGL・TGL-UY の 2 行）。xd-screen の plan の「Tiger Lake は pixel rate そのものを CDCLK の下限に」は `platform->kind == I915_XD_KIND_TGL` をやめて WA の bit を読む（受け入れの「`kind` を読む手順が無い」） |
| `display/xd-takeover.c/.h` | GOP の出力の分類に `I915_XD_FW_OUTPUT_DP_COMBO`（panel でない combo port の DP SST: RKL・ADL-S のデスクトップの Type-C の口は combo）、名前 "combo DP"。driver はこの出力を灯さない（OTHER と同じ扱い: 分類を読むのは log だけ） |
| `tests/display/host-xd-tc-test.c` | +44 check（tc cold 3、helpers 21、fia flow 12、tcss flow 8: 114） |
| `tests/display/host-xd-cover-test.c`（新） | 9 行のそれぞれで: PCI id で行が選ばれる、eDP（DDI A、HBR2 まで）の plan と combo PLL の語が rate を返す、HDMI（DDI A 以外の最初の combo port）の plan と WRPLL の語、最初の Type-C の口の DP の plan（Dekel なら TC PLL の語が rate を返し行に Type-C の流れがある、combo なら DPLL の語で流れが無い）、1080p60 の CDCLK の段、GOP の出力の分類（combo の HDMI、Dekel の DP は DP_TC、combo の Type-C の口の DP は DP_COMBO）。未知の ID（KBL・ICL・MTL）は行が無い。91 check |
| `tests/display/host-xd-takeover-test.c` | 「DP SST on DDI B, not the panel's」の期待を OTHER から DP_COMBO に |
| `plan/ws245/tests/run-xd-host-tests.sh` | host-xd-cover-test を足した。xd の .c に `->kind ==/!= I915_XD_KIND_*` と `verified` の読みがあれば失敗する grep を足した（§3.1、§7 #21） |

段の phase.md の必須の項目:
- 消す file: 無し。消す関数: 無し（xd-screen の `kind` の分岐を WA の bit に置き換えた）。
- 殻が更新する旧の写し: 無し。表 R: 新しい書き手は無い（流れ A・B の関数はまだ殻から呼ばれない。TGL の TC_cold_off の well は旧 `power.c` の well の表に無いので、`power.c` の switch からは呼ばれない: 5320 の起動の振る舞いは変わらない）。
- R11: 触った file（xd-tc.c/.h・xd-power.c・xd-platform.h・display-tables-xd.h・xd-screen.c・xd-takeover.c/.h・display-regs-xd.h）の数は 0。
- spinlock の下から呼ばれる関数: 無し（流れの関数は所有者の power の hook を呼ぶので、`xd.lock` の下から呼ぶ。p052 で配線する時に phase.md に書く）。

## 残り（この Phase の受け入れのうち、まだ無い物）

- 9 行 × {eDP、HDMI、DP-TC（TGL・ADL-P）、TBT} の **enable → disable の列**を host で期待値（ADL-P 系は m3 の dump、TGL・RKL・ADL-S は Linux の表の値）と比べる事。今の xd-screen の light/dark は step の中身が殻の hook（旧 modeset の関数）で、xd の部品だけで列を組めない。P1 の p050b（eDP）・p050c（HDMI・DP-TC）で step が xd の直の呼びになった後に、この Phase の次の attempt で `host-xd-cover-test` に列の比べを足す。
- GOP が HDMI/DP を灯すデスクトップ（RKL・ADL-S）の **takeover の列**（xd-takeover の stop の列を RKL・ADL-S の行で）: 分類は済み、stop の列の比べは上と同じ時に。
- TGL の流れ B の **配線**（hotplug・screen から connect/disconnect を呼ぶ、TC_cold_off の well を domain の表に）: p052（流れ A の状態機械）と同じ口で。TGL の TC は今も未対応のまま（§1.1）。

## 実機で確かめる事

- 5330（全部の段の後の 1 回）: 振る舞いの変化は無い見込み（ADL-P の行の WA の bit は不変、分類の変化は log だけ: GOP が eDP なので panel のまま）。
- 5320（TGL、ユーザーの一覧、§6.2 の p044 の行）: 起動・eDP・backlight・suspend が今と同じ（CDCLK の下限の規則は WA の bit で TGL に同じく当たる）。USB-C DP（流れ B）は配線の後（p052 以降）の新しい機能。

## 確かめた事

- host（2026-10-17）: `sh plan/ws245/tests/run-xd-host-tests.sh build/p044/xd-host` → 全部 pass（tc 114、cover 91、takeover 71、screen 60 ほか）、`I915_XD_STRICT` と `kind`・`verified` の grep も通る（grep が `kind ==`・`verified` を拾う事は手で確かめた）。
- Linux との比べ: `check-xd-regs-linux.sh` 1163 対一致、`check-xd-tables-linux.py` 全部の表が一致（Linux の tree は P1 の worktree の `build/ws229-p001/linux/linux-6.8.12` を読むだけ）。
- build（2026-10-17）: `sh build/tmp/kbuild.sh p044` で既定と `I915_TESTS=y` の 5 組が rc 0・warning 0（既定の vmunix の sha256 の頭 c1ed65f8dbbc4e6d）。`git diff --check` は空。
- 旧の host 試験（host-dp-test 72・host-lcd-modeset-test 120）は前の木（5342e0844）と出力が同じ（dp・lcd とも SAME）。
- 5330: 未実施（全部の段の後に 1 回）。

## 判定の材料（2026-10-18 P1、q1241）

証拠: i20 = q1229-i20（T1、2026-10-17、5330 の実機、tip 92f134d99。survey の md は t1 の 396cbfce4 の `plan/ws245/tests/evidence/t1229i20-tip-20261017.md`・`t1229i20-tip-debug-20261017.md`、dmesg と `5330-survey-diff.py` の出力は t1 の worktree の `plan/ws245/temp/5330/t1229i20-*`（git の外））。i21 = q1229-i21（merge 後の main 61dde9d6b、24f2329ea の `t1229i21-main-merged-20261017.md`、dmesg は同じ場所）。基準 = q1229-i18 の main の dmesg（同じ場所の `t1229i18-main-*`）。debug の比べ（`t1229i20-diff-main-debug.txt`）の差は phase046 の q1229-i19 の表の I・H の行だけ（steady の差は PP_CONTROL の survey 2 の 0x6f/0x67 = H7 だけ）、素の起動の比べ（`t1229i20-diff-main.txt`）steady 差 0 PASS。i21 も steady 差 0。

満たした基準:

- 5330 の起動に差が無い（ADL-P の行は不変の見込みどおり、i20・i21 の steady 差 0）

満たしていない・未確認の基準と残る物:

- host の受け入れの残り（9 行 × 4 種の enable → disable の列の比べ、RKL・ADL-S の takeover の列、TGL の流れ B の配線）は phase.md の「残り」のとおり未実施
- 5320: ユーザーの一覧（beta2.md の U19〜U23）の項目で、未実施

P1 の見立て（Status は変えない。判定は Q1）: cleared にしない（host の受け入れが残る）。

## host の受け入れの続き（2026-10-18 P1、q1244）

- 足した物（be2e6c243）: `host-xd-output-test` の `i915_xd_ot_test_cover`。表の全部の行（display 12・13 の 9 行と ICL の 2 行）で、同じ panel（DDI A、DPLL 0）の light と dark を xd-output の step で流す（display 11 は transcoder EDP、1 slice、pipe の DMC 無し）。各行で: light が通る（error・lock 0、lit）、DDI A の buffer と transcoder の function が on、dark でどちらも off、power の get と put（遅らせた put を含む）の数が同じで held 0。22 check（67 → 89）。最初の版で ICL の 2 行が transcoder A の panel で落ちた（transcoder EDP が要る: 期待どおり）ので display 11 の分岐を入れた。
- 確かめた事: `run-xd-host-tests.sh build/p244c-host` all passed（host_xd_output_test 89 checks 0 failures）。build は cebac0006 で 3 組 warning 0（試験だけの変更なので kernel は同じ）。
- まだ無い物: 列を Linux の手順・表の値（ADL-P 系は m3 の dump）と書き込みの 1 つずつで比べる事（足したのは各行で通る事と on/off と参照の釣り合いまで）、HDMI・DP-TC・TBT の 3 種を各行で、RKL・ADL-S の takeover の列、TGL の流れ B の配線（p052）、ICL の takeover の readout、WM の read latency、FEC stall の bit。期待の列を行ごとに Linux から組むのは 1 attempt では収まらない大きさ。

## 判定の材料（2026-10-18 P1、q1244）

- 満たした: 全部の行で eDP の light・dark が xd の部品で通る（host）。5330 の steady 差 0（q1229-i20・i21、前の節）。
- 満たしていない: 上の「まだ無い物」（Linux の列との比べと 3 種の kind）。
- P1 の見立て: cleared にしない。

## host の受け入れの続き（2026-10-19 P2、q1259）

- 足した物:
  - `host-xd-output-test` の `i915_xd_ot_test_cover_ports`: 表の全部の行（11 行）で HDMI（DDI A の後の最初の combo port、DPLL 1、pipe・transcoder B、74.25 MHz）の light と dark。Type-C の PHY（Dekel・MG）の TC1 を持つ行（TGL・TGL-UY・ADL-P・ADL-N・RPL-P・RPL-U・ICL の 2 行）で DP（TC1、自分の TC PLL（MG は 38.4 MHz の参照からの語、Dekel は 5330 の RBR の語）、1.62 GHz × 4 lane）を DP-alt と TBT の 2 つの mode で light と dark。各組で: light が通る（error・lock 0、DP は訓練が通る）、DDI buffer と transcoder の function が on、dark で両方 off、power の get と put の数が同じで held 0、Type-C の link を返す。54 check（89 → 143）。combo の Type-C の口の行（RKL・ADL-S・RPL-S）は Type-C の mode が無いので HDMI だけ。
  - `host-xd-takeover-test` の `i915_xd_to_test_rows_hdmi`: 全部の行で、GOP が pipe B に灯した HDMI（DDI A の後の最初の combo port、行の select の field、DPLL0）を takeover の stop で止める（デスクトップの RKL・ADL-S の GOP が monitor を灯す場合の列）: report の DDI と mode、transcoder・function・DDI buffer が off、port の clock が gate、DPLL0 off、HDMI の sink の hook が先頭、pipe が dark。11 check（88 → 99）。
- 確かめた事: `run-xd-host-tests.sh build/p2-ws245-host` all passed（output 143・takeover 99、warning 0）。kernel の source は変えていない（試験だけ）。QEMU・実機は未実施。
- まだ無い物: 列を Linux の手順・表の値（ADL-P 系は m3 の dump）と書き込みの 1 つずつで比べる事（足したのは各行・各 kind で通る事、on/off、参照と link の釣り合い、stop の後の状態まで）、TGL の流れ B の配線（p052）。

## 追加の証拠 1: ADL-P の終わりの状態（2026-10-19 P2、Q1 の許可「案の 1…を追加の証拠として作ってよい」）

- `host-xd-output-test` の `i915_xd_ot_test_end_state`: fake model の上で 5330 の 2 画面（panel: pipe A・DDI A・DPLL 0・HBR × 2・18 bpp・dither、monitor: TC2（DDI 4・PHY 6）・pipe B・TC PLL 2・RBR × 4・30 bpp・1920x1280@164.36 MHz、DP-alt）を xd-output の step で順に灯し、42 個の register の最終値を m3 の dump（`plan/ws051/tests/m3-5330-20261007/range-0x*.txt`、Linux 6.19 が 2 画面を駆動中の 5330）と比べる。transcoder A・B の timing・source size・DATA/LINK M/N・DDI function・MSA misc・DP transport・configuration・pipe misc・transcoder の clock select、DDI A と TC2 の buffer、DDI TC2 の clock select、DPLL 0 の enable と CFGCR0/1、TC PLL 2 の enable、DDI の clock map。44 check（143 → 187）、全部一致。
- 最初の比べで食い違った 3 つと扱い:
  1. transcoder A の DP transport の enhanced framing（xd 0x80040300、machine 0x80000300）: 試験の panel の sink の model が enhanced framing の能力（DPCD の 3 byte 目 0x82）を持っていたため。実物の panel の DPCD は 0x02（`plan/ws031/display-ref/dpcd-drm_dp_aux0-000.bin`）で、そう直すと一致（driver の不具合ではない）。
  2. TC2 の DDI buffer（xd 0x80000006、machine 0x80001146）: bit 6（PHY の所有）は Type-C の接続（試験では hook の model）が立てる物なので、model の初期値に立てた。bit 12・8 は Linux v6.8.12 の定義に field が無い（machine は後の版の Linux）ので比べから外した（mask 0xffffeeff）。
  3. DDI の clock map（xd 0、machine 0x01e05800）: 使っていない port の gate は firmware と sanitize が残す物で、試験の 2 つの port の bit（DDI A の gate と PLL の選び、TC2 の gate: mask 0x00002403）だけを比べる。
- 比べていない物: DBUF・WM・DBOX・line time（plan（xd-screen）の出力を試験が入力として渡すので、output の比べにならない）、Dekel PHY の register（dump は index の窓の 1 つの面だけ）、plane（xd-picture の担当）。
- 確かめた事: `run-xd-host-tests.sh build/p2-ws245-host` all passed（output 187）、warning 0。試験だけの変更（kernel の source は不変）。

## 追加の証拠 2: 版ごとの順の anchor（2026-10-19 P2、Q1 の許可）

- `host-xd-output-test` の `i915_xd_ot_test_order`: display 11（Ice Lake）・12（Tiger Lake）・13（Alder Lake-P）の各行で、eDP（DDI A、display 11 は transcoder EDP）・HDMI（DDI A の後の最初の combo port、DPLL 1）・DP-TC（TC1、DP-alt）・TBT（TC1）の light と dark を xd-output の step で流し、Linux v6.8.12 の `hsw_crtc_enable`・`hsw_crtc_disable`・`tgl_ddi_pre_enable_dp`・`hsw_ddi_pre_enable_dp`・`intel_ddi_pre_enable_hdmi`・`intel_enable_ddi(_hdmi)`・`intel_ddi_post_disable(_dp/_hdmi)` の順から取った要の書き込みが、その順で現れる事を見る（値は表の比べで済んでいるので順だけ）。
  - light: PLL（TBT は caller の PLL）→ panel の電源（eDP）→ DDI の clock（combo は clock map、TC は DDI_CLK_SEL の MG/TBT の選び）→ display 12 以降は transcoder の clock（PHY+1、版の field）と function の enable 無しの config → DP は sink の D0 → transport の訓練 → DDI buffer → normal pattern → display 11 の DP は訓練の後に transcoder の clock → timing → function の enable → transcoder の start → HDMI は transcoder の後に buffer。
  - dark: transcoder の stop → function off → DP は sink の D3 → display 11 は buffer の前に transcoder の clock off → buffer の stop → display 12 以降は buffer の後に clock off → panel の電源（eDP）→ DDI の clock → PLL off（TBT は caller の PLL）。
  - 無い事の確かめ: display 11 の DP で訓練の前に function を書かない・訓練の終わりの前に transcoder の clock を書かない、HDMI で transcoder の前に buffer を始めない、display 12 以降の dark で buffer の前に clock を外さない。
- 219 check（187 → 406）、全部通る。試験の側の確かめ: dark の「buffer の前に clock を外さない」を display 11 に当てる様に一時的に変えると Ice Lake の HDMI・DP-TC・TBT の 3 つが落ちる（display 11 は Linux どおり buffer の前に外す: 検出できる事の確かめ。変更は戻した）。
- 確かめた事: `run-xd-host-tests.sh build/p2-ws245-host` all passed（output 406）、warning 0。試験だけの変更。
- 範囲の外: 行ごとの全部の書き込みの 1 つずつの一致（受け入れの読み替えは朝にユーザーへ: Q1）。RKL・ADL-S・ADL-N・RPL の行は版 12・13 と同じ道（版の分岐だけを見る試験なので代表の 3 行）。
- 使い捨ての出力 `build/p2-ws245-mut/`（上の確かめの build）が worktree に残る（消すなら Q1 に）。
