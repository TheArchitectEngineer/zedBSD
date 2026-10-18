<!-- awesome-plan project=zedbsd record=ws245-p046 -->

# ws245-p046: 規約の全文の見直しと p045 の出典の監査

Status: in-progress（2026-10-17 q1229-i16: 機械で確かめられる規則の全部と、直した所の読み直しを実装・host 試験・build 済み（0e85d1ff1・a5e461698・a3849a76f・16de8c315）。全文の目視の読み通しは未実施（下の「制限」）。q1229-i18（T1、5330 の素の起動）は起動・survey は PASS、`i915.debug=display` の書き込みの列の比べが FAIL → q1229-i19 で差を分類し、不具合 1 件（takeover の AUX・DDI IO の well と VDD）を直した（26b307bd9）。5330 の再試験は T1 に（下の「q1229-i19 の後に T1 に流す項目」））
Parent: [WS245](../ws.md)
Queue: q1229-i16（P1）、q1229-i19（P1、5330 の差の解析と直し）
設計: [design.md](../design.md) §6.2 の p046 の行（`plan/coding-style.md` の全文の checklist、`git diff --check`、build、§5.4 の監査）、§5.4、guardrail「MIT・GPL の出典のデータの引用」「source の中の開発の管理情報」

## 範囲

WS245 が変えた C の file（rollback の基点 0c25c87a7 からの `git diff --diff-filter=AM` で中身のある 181 file: 本番 124、試験 57。`build/p043b2/ws245-files.txt`）。規則は `plan/coding-style.md` の全文。機械の検査は `plan/tools/style-check.py`（master の Tools の行）。指摘は WS245 が足した行（`git diff -U0 0c25c87a7` の + の行。新しい file は全部の行）に限った（WS245 の前からある行の古い書き方は、その file の持ち主の WS の物として触らない）。

## 行った事

1. 本番（0e85d1ff1）: style-check の指摘のうち WS245 の行のもの（call-in-condition 40、blank-after-brace 81、multi-line-body 2、comment-form 2、paragraph-comment 336、条件演算子 68 の読み直し）。
   - 条件の中の呼び出しは結果を名前の付いた変数に取ってから判断（短絡の順を保つ: xd-ddi.c の PHY A の確かめは phy == 0 の時だけ読む、xd-output.c の MG の判断は Type-C の時だけ、xd-takeover.c の DMC の有無は DMC がある時だけ）。関数の様な macro（`I915_XD_*(x)`）の 13 件は呼び出しでないので残した。
   - 閉じ括弧の後の文には空行と、その段落の意味の comment。`break` だけの所は空行だけ。
   - 段落の comment は意味を書いた（例: `/* The run the hook belongs to. */`、`/* Refuses a header whose length is not its version's. */`）。lock・access の begin/end・`record_enter/leave`・preempt の off/on で囲む critical section の境（取った直後と返す直前の空行）は規則の例外どおり comment を足さない: style-check はこれらの名前（`env.lock`/`env.unlock`、`i915_xd_output_lock(…, 0)`、`access_begin/_end`、`kern_preempt_*`）を lock と認めないので 44 件が残る（道具の限界）。
   - Boolean を式で作る return（`… ? 1 : 0`）は if に（xd-ddi.c 3、xd-clock.c 1）。入れ子・複数行の条件演算子は if に（xd-screen.c の lane の上限、xd-ddi.c の load generator）。短い対称の選び（`x ? "on" : "off"` など）は残した。
   - `(void)param;` は `UNUSED_PARAMETER(param);` に（xd-display.c・vbt-shell.c・xd-output.c）。
   - split した呼び出しは 1 行 1 引数に（xd-ddi.c の idle の待ち、xd-screen.c の dp_choose）。
2. 試験（a5e461698）: 同じ規則を tests/display の WS245 の行に（call-in-condition 26、blank-after-brace 67、paragraph-comment 106、入れ子の条件演算子 2）。`host-test.h` に `UNUSED_PARAMETER`（driver の i915.h と同じ定義）を足し、xd-fake-regs.c が include。
3. 出典と開発の管理情報（a3849a76f）: `plan/ws244/tests/inventory.py` の S（Linux の型・API の綴り）の 5 関数（resident.c・run-log.c の power の hook の `wakeref` という引数名）を `cookie` に、xd-backend.h の hook の引数名も。試験の comment の `plan/ws051/...`・`plan/ws031/...` の path（guardrail「source の中の開発の管理情報」）を「5330 の register を Linux の下で読んだ物」の言い方に。
4. 関数の結果の直の return（16de8c315）: 意味のある呼び出しの結果を直に返す所（xd-ddi.c の buffer の待ち、xd-screen.c の plan の check、xd-takeover.c の PLL の desc）を、変数に取って成功と失敗を分けた。算術の helper（div_round・field_put・sizeof）と errno の変換は残した。
5. host-capture-test の登録（Q1 の判断 (1)）: build でき 23 checks 0 failures なので run-xd-host-tests に入れた（0e85d1ff1）。試験の comment の存在しない runner（`plan/ws031/tests/run-capture-host-test.sh`）の名を run-xd-host-tests に直した。

## 出典の監査（§5.4、p045 の file を含む）

- `check-sources.py`（p043b で run-xd-host-tests に入れた）: 171 file、file の先頭に出典が無い、Derived・許諾文が無い、referenced: は複数行の comment で版と path がある。p045 の display 11 の部品（xd-tc.c・xd-clock.c・xd-power.c・xd-output.c の display 11 の分岐、host-xd-icl-test）を含む。
- `grep -L referenced:` の xd の file: xd-diag.c だけ（pipe と plane の停止と firmware が残した物の log。Linux の値・手順を引いていないので出典は要らない）。
- Derived の表示: gen9-* の外は 0（`grep -rln 'Derived from\|derived from the Linux' display intel`）。
- `inventory.py --summary-tsv`: display・intel の gen9 の外で S（出典を書かずに Linux の型・API の綴りを使う関数）は 0。C（Linux の関数・file を出典として名指す）は xd の file の `Procedure referenced:` の block の数で、guardrail の block ごとの引用そのもの（design §5.4 の「C と S の行が 0」は block ごとの引用の規則より前の基準: C は引用、S が写しの疑い）。
- 関数名の対応表の目視（xd の関数 ↔ Linux の関数の 1 対 1 の写しが無いか）: 未実施（下の「制限」）。

## 確かめた事

- build 3 組（16de8c315 の tree、`build/tmp/kbuild.sh p046`）: default rc=0 warn=0（vmunix sha256 の先頭 39c9a9dcecd21f02。a3849a76f の名前の変更と 16de8c315 の return の分割の後も同じ値 = 振る舞いを変えない）、vke2 rc=0 warn=0、execution rc=0 warn=0。0e85d1ff1 の前（p043b の power）の 539c8f4ecddac882 から変わったのは本番の書き直し（条件の分解・変数の取り出し・ループの外への移動）による LTO の配置の差。
- run-xd-host-tests（`build/p046-host`）all passed: 31 本 0 failures（host-display-power-test 19、host-capture-test 23 を含む）、段の順、check-sources 171 file。WS075 host-output-test 80/0、WS237 gen9 flip 48/0・mode 71/0・backlight 21/0、WS229 bootfb 20/0。`git diff --check` 0。
- style-check の WS245 の行の残り: 本番は条件演算子の短い対称の選び 49、macro の call-in-condition 13、critical section の境 44（道具の限界）。試験は条件演算子 10（短い対称）、macro 1。

## 制限と残り

- 全文の目視の読み通し（comment が意味を言っているか、名前、関数の形、1 対 1 の写しの目視）は、本 Phase では機械の検査で見つかった所と、その前後の読み直しまで。117,100 行（181 file）の通読は未実施。必要なら conformance-reviewer（P3）に file を分けて頼む（Q1 の判断）。
- R11: WS245 の行の開発の管理情報は 0。gen9-*.c・bootfb.c の先頭の `WS237`・`WS229`・`BUG-310` などは display 9 の file（WS244・WS251 の範囲、WS245 は p002 で移しただけ）なので触らない。
- style-check が lock と認めない名前（`env.lock`・`*_lock(…, 0)`・`access_begin/_end`・`record_enter/leave`・`kern_preempt_*`）を道具に足すか: Q1 の判断（道具は plan/tools の物）。

## q1229-i19: 5330 の差の分類（2026-10-17、T1 の q1229-i18 の解析）

材料: T1 の q1229-i18（2026-10-17、5330 の素の起動の実機、基準は main の kernel（main の SHA は T1 の記録に無く未確認）、段は branch の tip（db95b5a6c。src は 16de8c315 と同じ: `git diff 16de8c315 db95b5a6c -- src` が空））の 4 起動の dmesg（t1 の worktree の `plan/ws245/temp/5330/t1229i18-*`、git の外）と survey の md（`plan/ws245/tests/evidence/t1229i18-*.md`）。書き込みの列は `5330-survey-diff.py` の read() と同じ規則で読み、difflib で整列（scratch の wdiff.py）。起動の基準は PASS、survey（bare）の steady の差 0、desktop まで出た（T1）。

| # | 差（基準 main / 段 tip） | 分類 | 理由・根拠 |
| --- | --- | --- | --- |
| 1 | takeover の stop で AUX A に DPCD 0x600=2（D3）の native write が 160 回（64014=80060000・64018=02000000・64010=fe50023f/7e10023f。基準は 1 回）。log `takeover: pipe A: the sink did not go to sleep (rc 42)`（42 は ETIMEDOUT） | **不具合**（p050a） | 160 = xd-aux の 1 transaction の 5 回 × DPCD の 32 回の再試行 = 全部 timeout。原因: stop の間 AUX A の well を誰も持っていない。p050a は「INIT の参照が takeover の間すべての well と DC off を持つ」として旧の readout の参照（DDI IO・AUX）を取らなくしたが、start の参照（`I915_DISPLAY_POWER_START`）は PIPE 0〜3 と DC_OFF だけで AUX・DDI IO を含まない（Linux の INIT の domain も AUX・DDI IO の well に無い）。edp late の VDD が 3000 ms 後に落ちる時に VDD の持つ AUX A の参照も返る（列の `c7204=00000067 45444=00000001`、基準も同じ）ので、その後の takeover では AUX A が off。基準はその後に readout の参照で `45454=00000003 45444=00000002` を取っていた。eDP の AUX は Linux では transfer が VDD を立てる（基準の `c7204=0000006b` が D3 の直前）が、段は D3 の後の panel off まで VDD を立てなかった。**直した（26b307bd9）**: `drv_i915_xd_takeover` に caller の power（`struct i915_xd_takeover_power`、resident.c が display の power を渡す）を足し、lit の pipe ごとに stop の前に DDI IO、DP なら AUX の well を取り、stop の `port_release`（DDI の後、PLL の前: 基準の `45454=1` の位置）・`link_release`（PLL の後: 基準の `45444=1` の位置）で返す。sink_after は panel の DDI の時 VDD が立っていなければ立てる（panel に電源が無ければ power-up の待ち）、panel_off は VDD が立っていれば書き直さない（基準と同じ `6b` → `60` の 2 書き込み）。実機の確かめは T1（下の項目） |
| 2 | 基準だけ `45454=3`（DDI IO A の request）・`45444=2`（AUX A）を stop の前、`45454=1`・`45444=1` を stop の後。enable の時は基準 `45454=2`、段 `45454=3` | #1 の一部（直した） | 段は DDI IO A を一度も request せず firmware の request のまま on（段の log `well DDI_IO_A reads on with 0 users`）。26b307bd9 の後は基準と同じ列の見込み（enable の値 2 は well が一度 off になった印） |
| 3 | 基準だけ D3 の直前の `c7204=0000006b`、段は D3 の後（`46140=0` の後）に `6b` | #1 の一部（直した） | 26b307bd9 の後は D3 の前に `6b`、panel off は `60` だけ |
| I1 | 段だけ core の start の `c2020=00008000` | 意図（p035） | SOUTH_DSPCLK_GATE_D の bit 15 |
| I2 | 段は sanitize の `c7208・c720c`（PPS の delay）・`138124=8000001a`（mailbox）・`c4038`（SHPD filter）・`45004=8`（DISP_ARB_CTL2）を edp acquire の直後に、基準は DMC の load の後の takeover の sanitize で | 意図（p037・p050a: takeover と sanitize の順） | 同じ値の書き込みの位置だけ（T1 の「write in another place」の 138124・138128・13812c・45004・c4038・c7208・c720c） |
| I3 | 基準だけ `45250=00000001`（PIPEDMC_CONTROL A）2 回（sanitize と takeover の readout） | 意図（p050a の sanitize の (1)・takeover の (4)） | 段は pipe の DMC を payload がある時だけ、readout の sanitize は無い。stop の `45250=0` と enable の `45250=1` は両方にある |
| I4 | 段だけ takeover の plane off の後に `c8258=0`・`c8250=0`（PWM の duty 0 と停止）。基準は enable の最初に `c8250=0` | 意図（p037・p050a: PPS・PWM の順） | 段は takeover で PWM を止めるので enable の「動いている PWM を一度止める」書き込みが要らない |
| I5 | enable の power on の前の `c7208=07d00001 c720c=044c0001` | 意図（p022） | PP_ON/OFF_DELAYS |
| I6 | `c4030=0000008a`・`89`（SHOTPLUG_CTL_DDI の status の ack）の位置 | 意図（p019: 別の thread） | eDP の HPD の pulse の割り込みの時刻 |
| H1 | `162100=80005f2a` / `80005f29` | 無害 | VARYING（PHY A の COMP_DW0 の rmw が hardware の status を持ち返す。同じ kernel でも起動ごとに違う: 2026-10-16） |
| H2 | 基準だけ edp acquire の DPCD の read 2 つ（`90000000`（0x000 の 1 byte の起こし）と `90010001`（0x100〜0x101 の 2 byte）） | 無害 | 旧の acquire の診断の read（基準の log `| 100: 0a 02`）。段は使わない。link の選び（270000 × 2）は同じ |
| H3 | 基準だけ takeover の前に `70034=0`（PIPE_BOTTOM_COLOR）・`4a480=0`（GAMMA_MODE）・`49028=0`（CSC_MODE） | 無害 | 旧の readout の sanitize が灯っている crtc の色を firmware の値から既定に戻す（Linux intel_sanitize_crtc の color commit）。段の takeover はその pipe を直後に止め、enable が同じ 3 つを書く（両方の enable にある） |
| H4 | 基準だけ plane off の時の `70240〜7027c`（PLANE_WM・PLANE_BUF_CFG） | 無害 | 旧の plane の disable が readout の WM（hardware から読んだ値）を書き戻した。止める plane の WM で、enable が新しい WM を書く（両方同じ値） |
| H5 | 基準だけ takeover の sanitize の 2 回目の GMBUS の reset（`c5100=0`・`c5110=0`） | 無害 | 両方とも start で 1 回 reset している。その間 GMBUS は使われない |
| H6 | 基準だけ `45504=00000002`（DC_STATE_EN の DC6）→ `0` | 無害（時刻）、ただし潜在の欠陥は main と共通 | DC off の well を返す時の DC6 は DMC の program がある時だけで、その判断は serve の時の 1 回の写し（基準 `display->pwc.dmc_has_payload`、段 `display->power.dmc_loaded`）。基準の debug の起動は DMC の file が serve より前に読めた（`dmc_has_payload=1`）、段の debug の起動は後（`DMC program not loaded`）。素の起動は両方とも後（`dmc_has_payload=0`／`not loaded`）。survey の DC_STATE_EN は両方 0。DMC が serve より後に載ると以後 DC6 に入らないのは main からの性質（Linux は毎回 has_payload を見る）: Q1 の判断に（下） |
| H7 | survey 2 の PP_CONTROL 基準 0x6f、段 0x67 | 無害（段が Linux の振る舞い） | 段は edp の VDD を最後の使用から 5 × power cycle（3000 ms）後に落とす（survey 1 は 0x6f、survey 2 は 0x67）。基準の旧 code は `edp late … KEPT … (in 3000 ms)` と出しながら survey 2 でも 0x6f（遅い VDD off が起きない） |

log の差:

| log | 分類 | 理由 |
| --- | --- | --- |
| `combo PHYs up: 1 (mask 0x1)`（例は 0 か PHY B） | 手順の例の誤り | 書き込みの列は基準と同じ PHY A の COMP・PORT の書き込み（162104・162124・162128・162120・162100・162014）。PHY A は firmware の後も確かめに通らず入れ直す。旧の log は `(2 initialised)`。手順を直した |
| `pipe A stopped: … PLL 1`（例は PLL 0） | 手順の例の誤り | 5330 の GOP は DPLL1（`firmware left PLL 1 (0x46014): ENABLE=0xcc000000`、基準の `PHY A -> DPLL 1`、列の `46014=4c000000 04000000`）。手順を直した |
| `serving: wells_on 0 -> 0` | log の欠陥（直した） | takeover のある起動では start の参照を takeover まで持ち `drv_i915_display_power_serve` を呼ばないので、数が 0 のまま出た。26b307bd9 で、持ち続ける時は今の well の数を前後に入れる（`N -> N`）。takeover の後の `wells on 8 -> 3` は前から正しい |
| DMC の v2.20・present の行が無い、`DMC program not loaded` | 手順の例の誤り（版の行は基準にも無い）、not loaded は時刻（H6） | 載った program は resident の inputs の `DMC programs 0x1f`（基準 `DMC fw mask 0x1f`）。手順を直した |
| reboot の止める道の行 | 未確認 | log が届かず（T1）。再試験の項目に残す |
| `the sink did not go to sleep (rc 42)` | 不具合 #1（直した） | — |

実機でしか分からない事（推定）: #1 の直しの後、AUX A の D3 が 1 回で ACK される見込み（基準は同じ条件で 1 回: 両方とも panel の電源は on、AUX A の well と VDD が on）。根拠は基準の列。DDI IO A の request を返すと well が off になる見込み（基準の enable の `45454=2`）。takeover の時間は 160 回の timeout の分（1 回 約 4 ms の hardware の timeout と待ちで、推定 0.6〜1 秒）短くなる見込み。

確かめた事（q1229-i19、26b307bd9）: build 3 組（`sh build/tmp/kbuild.sh p046fix`）default rc=0 warn=0（vmunix sha256 の先頭 83bc0eb398998bd4）、vke2 rc=0 warn=0、execution rc=0 warn=0。`run-xd-host-tests.sh build/p046fix-host` all passed（host-xd-takeover-test 75 checks 0 failures: 足した 4 check は DDI IO A → AUX A の get が D3 の前、DDI IO の put が panel off の後・PLL の前、AUX の put が D3 と PLL off の後、VDD の書き込みが 1 回で D3 の前）、check-sources 171 file。WS075 host-output-test 80/0、WS237 gen9 flip 48/0・mode 71/0・backlight 21/0、WS229 bootfb 20/0。`git diff --check` 0、style-check は変えた行で指摘 0（試験の 2 件を直した後）。5330・QEMU は起動していない。

### q1229-i19 の後に T1 に流す項目（5330、素の起動、kernel は branch の 26b307bd9 以降の tip、基準は前回と同じ main の kernel）

1. `i915.debug=display` の起動 1 回（段だけでよい: 基準の main-debug の dmesg は q1229-i18 の物を使える）: `5330-survey-diff.py <q1229-i18 の main-debug の dmesg> <段の debug の dmesg>` の whole lists の差が上の表の I・H の行だけである事。特に: AUX A の `64014=80060000 64018=02000000` が 1 回、その前に `45454=00000003 45444=00000002` と `c7204=0000006b`、後に `45454=00000001`（PLL の off の前）・`45444=00000001`（PLL の off の後）。
2. その起動の log: `takeover: pipe A: the sink did not go to sleep` が無い、`the DDI IO well did not come up`・`the AUX well did not come up` が無い、`takeover: pipe A stopped: DDI 0 mode 2, PLL 1, late 0x0 (rc 0)`、`display: serving: wells_on N -> N`（N は 0 でない同じ数）、`display power: well DDI_IO_A reads on with 0 users` が無い、`picture up` 1、`device lost` 0、survey の判定 lit。
3. reboot（止める道）: `stop: pipe … is still on` が無い、`stop: display power released`（前回は log が届かず未確認）。
4. 素の起動（debug なし）1 回: survey の steady の差 0（PP_CONTROL の survey 2 の差 0x6f/0x67 は H7 で意図）、起動の基準（graphical の login、HANDOFF、lease 1 claimed、backlight-probe）。

## T1 に渡す実機の確かめ（全部の段の後に 1 回。branch agent/p1-ws245 の tip、基準は main の kernel）

手順は ws.md「5330 の素の起動での段ごとの回帰の手順」と p047 の survey。失敗したら ws.md の「段の境の SHA」で二分探索。

### 5330（Latitude 5330、ADL-P、素の起動）

1. 起動の基準: graphical の login、`kern.msgbuf_dropped` 0、sessiond の SESSION start と HANDOFF、`lease 1 claimed`、`picture up` 2、device lost 0、`perf: … per submit` が基準と同じ桁、`backlight-probe` の get・set 100・get。
2. survey（`plan/ws245/tests/5330-survey.sh` と `5330-survey-diff.py`）: lit（PP_STATUS on、PWM の duty > 0、frame counter +3 以上、SURFLIVE が resident の buffer、CRC の 2 回が 0 でない、underrun 0、TRANS_CONF on）。register が基準と同じ: PWR_WELL_CTL2 0x00000c03、DPLL0 ENABLE 0xcc000000・CFGCR0 0x00e001a5・CFGCR1 0x88、DPCLKA_CFGCR0、DDI_BUF_CTL(A)、TRANS_DDI_FUNC_CTL 0x8a210002、PLANE_CTL 0x94000000、PLANE_WM・PLANE_BUF_CFG・WM_LINETIME・MBUS_CTL・DBUF_CTL・PIPE_MBUS_DBOX_CTL、DC_STATE_EN（DMC の program があり表示が止まっている間は DC6）。
3. `i915.debug=display` の起動で書き込みの全部の列を基準と比べる（difflib の整列、`5330-survey-diff.py`）。意図した差と無害な差は下の「q1229-i19: 5330 の差の分類」の表（意図: power on の前の PP_ON/OFF_DELAYS（p022）、SOUTH_DSPCLK_GATE_D の bit 15（p035）、takeover と sanitize の順（PIPEDMC_CONTROL、PPS・PWM の順: p037・p050a）、rmw が read と write に（p050b）、別の thread の書き込みの相対の順（p019）。無害: 表の H の行）。それ以外の差は FAIL。survey 2 の PP_CONTROL は基準 0x6f・段 0x67（VDD の 3 秒後の off、表の H7）。
4. 起動の log（今の文言、design §6.4.1）:
   - `i915: display power: <数> wells (Alder Lake-P), DC states allowed 0x4000000a, entered when idle 0x2`、`display power: combo PHYs up: 1 (mask 0x1)` の後に `combo PHYs up: 0 (mask 0x0)`（PHY A は firmware の後も確かめに通らず入れ直す: 旧も同じ PHY A の書き込みで、旧の log は `combo PHYs A/B (2 initialised)`。q1229-i19 で手順の例「0 か PHY B だけ」を直した）、`display: core started: cdclk=… vco=… dbuf=0x… start reference held`（cdclk は GOP の値を保つ）。
   - `display: PCH type=8 … source=1`（ADP）、`display: CD clock: …`、`display: memory: raw=… type=…`・`QGV n: … MB/s for one plane`・`display: bandwidth: restricted=1 … answer=0 sagv=1`、resident の `QGV mask 0x… -> allowed 0x… bw … MB/s` が基準の値と同じ。
   - `i915: firmware: the firmware's output: …`（DDI A の panel）、`i915: takeover: lit pipes 0x1, output panel (pipe A, DDI 0)`・`pipe A stopped: DDI 0 mode 2, PLL 1, late 0x0 (rc 0)`（5330 の GOP は DDI A を DPLL1 で出す: `firmware left PLL 1 (0x46014): ENABLE=0xcc000000`、旧の `DPCLKA_CFGCR0=0x01e07801 (PHY A -> DPLL 1)`。q1229-i19 で例の「PLL 0」を直した）・`rc=0, crtcs stopped 1, still active 0x0`、`the start's power reference went back: wells on A -> B`、`i915: sanitize: lit pipes 0x1, … wells off 0`、`display: serving: wells_on N -> N dc_state=…`（takeover のある起動では start の参照を takeover まで持つので前後が同じ数。26b307bd9 より前は 0 -> 0 と出た）、`display power: well …: N users, on` の一覧。
   - `i915: edp acquire: error=0`、`edp DPCD 000:` と `edp EDID sha256` が基準と同じ、`edp late: … off in 3000 ms`。`dmc worker: proceeding to load`、resident の inputs の行の `DMC programs 0x1f`（MAIN と pipe A〜D。基準の `DMC fw mask 0x1f` と同じ。DMC の版を出す行は main にも tip にも無い: q1229-i19 で例の「v2.20 と present」を直した）。serve の時の `DMC program not loaded`／`loaded` は firmware の file の読みの速さ次第（基準の `dmc_has_payload=0/1` と同じ）。
   - takeover の行に `the sink did not go to sleep`・`the DDI IO well did not come up`・`the AUX well did not come up` が無い事（q1229-i18 で `the sink did not go to sleep (rc 42)` が出た: 下の q1229-i19 の表の #1）。
   - `i915: hpd arm:`（pin の register の値）、`hpd start: connectors=6 hdmi=HDMI-A-1`、`hotplug path: started`、`i915: TC2: declared`・`TC2 readout: …`、`DP-ext TCn: bound (DDI ., AUX channel N, …)`。
   - 無い事: `N0:`・`LCD-B`・`drm_vblank_init`・`intel_power_domains_init`、`xd-screen: the plan's check differs`、`PLL … not locked`、`Timeout waiting for DDI BUF`、`display power: well …: its state came late`・`the fuses of power gate … came late`、`display power: DC state … did not read back`、`TC: power functions … did not come`、`display run: power get … FAILED`。
5. reboot（止める道）で `stop: pipe … is still on` が出ない、`stop: display power released`。
6. ユーザーの一覧（T1 は流さない）: suspend → resume（`display power: DC9 entered`・`DC9 left`、DMC の載せ直し、`hpd suspend`・`TC<n> resume`・`resume: vga …`・`hpd resume`、backlight と desktop が戻る、寝ている間に抜いた monitor が ABSENT）、HDMI の plug で 2 画面（head は pipe B）・unplug、USB-C DP（TC2）の plug で 2 画面・unplug、HDMI を挿したまま蓋を閉じた起動（GOP が外部を灯す道）、GOP の画面から desktop の黒の時間、LCD の目視。

### SV7（Gen9）

- 起動: Gen9 の firmware の check が start の中（`N0-lite (gen9)` の行）、GPU の node、OpRegion の行が今と同じ。
- backlight: PCH の SPT・CNP の判定が xd の表（`gen9 … backlight` の行、backlight-probe）。

### 5320（Latitude 5320、TGL、ユーザーの一覧）

- 起動・内蔵 eDP・backlight・suspend が今と同じ（TGL の well の木 PW_1〜PW_5、CD clock の relock、DBUF 2 slice、display 12 の DMC、tracker 8・ABOX・buddy）。
- `i915: TC<n>: declared`・readout の行（display 12 の TC も driven にした: p052）。GOP の出力と内蔵 LCD が変わらない事。
- USB-C DP（流れ B）は新しい機能（動かなくても p044 は cleared、結果は Bug か Future Work）。

## 判定の材料（2026-10-18 P1、q1241）

証拠: i20 = q1229-i20（T1、2026-10-17、5330 の実機、tip 92f134d99。survey の md は t1 の 396cbfce4 の `plan/ws245/tests/evidence/t1229i20-tip-20261017.md`・`t1229i20-tip-debug-20261017.md`、dmesg と `5330-survey-diff.py` の出力は t1 の worktree の `plan/ws245/temp/5330/t1229i20-*`（git の外））。i21 = q1229-i21（merge 後の main 61dde9d6b、24f2329ea の `t1229i21-main-merged-20261017.md`、dmesg は同じ場所）。基準 = q1229-i18 の main の dmesg（同じ場所の `t1229i18-main-*`）。debug の比べ（`t1229i20-diff-main-debug.txt`）の差は phase046 の q1229-i19 の表の I・H の行だけ（steady の差は PP_CONTROL の survey 2 の 0x6f/0x67 = H7 だけ）、素の起動の比べ（`t1229i20-diff-main.txt`）steady 差 0 PASS。i21 も steady 差 0。

満たした基準:

- 機械の規則・出典の監査・5330 の差の直し（26b307bd9）は i20 で PASS

満たしていない・未確認の基準と残る物:

- 全文の目視の読み通しは q1241 で続ける（下の節）
- style-check の lock の名前の扱いは Q1 の判断

P1 の見立て（Status は変えない。判定は Q1）: cleared にしない（読み通しが残る）。

## 読み通しの続き（2026-10-18 P1、q1241）

範囲: 本番の xd-*.c（27 file、34,888 行）を、style-check が見ない規則で grep と目視で見た（禁止の comment の形、`_local` の名前・`retval`・`function_result`、prose で始まる複数行の comment、`for` の宣言、関数の最後の失敗の return、1 行の 3 つ以上の節の条件）。

- 直した物（書き方だけ、動作は変えない）: 3 つ以上の節を 1 行に書いた条件 16 個を 1 節 1 行に（xd-edp.c・xd-hpd.c・xd-clock.c 2・xd-tc-port.c・xd-output.c 2・xd-picture.c 6・xd-screen.c 3）。xd-edid.c の block の読みは失敗の `return error;` で終わっていたので、ループを break で抜けて失敗と成功（`Succeeded: the block is read.`）を分けた（try の回数・ENXIO で止める所・既定の EIO は同じ）。
- 見て残した物: xd-aux.c・xd-hpd.c の最後の `return status;` は誤りの code ではなく判断の値（制御の word、connector の状態）を返すので規則 §11 の「分類を返す」形。xd-dp.c の `first_error` も値の選び。
- 確かめた事: `sh build/tmp/kbuild.sh p046r` default rc=0 warn=0（sha256 の先頭 c5a67928fe15d677）、vke2 rc=0 warn=0、execution rc=0 warn=0。`run-xd-host-tests.sh build/p046r-host` all passed（check-sources 106 file: 旧の file を merge で消した後の数）。`git diff --check` 0。5330・QEMU は起動していない。
- 残り: 試験の file（tests/display）と xd 以外の本番の file の目視、comment が意味を言っているかの 1 行ずつの目視、関数名の 1 対 1 の写しの目視は未実施。

## 読み通しの続き 2（2026-10-18 P1、q1244）

範囲: WS245 が足した行（`git diff -U0 0c25c87a7`）の、xd 以外の本番の file（display・intel・mmio-record.c・device.c の 60 file、gen9-*・bootfb は WS244・WS251 の範囲で除く）と tests/display の 44 file。style-check（足した行だけに絞る scratch の filter）と grep、目視。

- 直した物（cebac0006、書き方だけ）: edp-device.c の `x != 0 ? 1 : 0` の Boolean 3 つを if に（VDD の保持 2、pps_valid）。3 つ以上の節・`&&` と `||` の混ざった 1 行の条件 39 個を 1 節 1 行に（本番: hpd.c・resident.c・survey.c・output.c・tc-port.c・mmio-record.c、試験: platform・gmbus・dpll・icl・tc・vbt・power・fake-regs）。言い直しの comment 2 つ（vbt-shell.c「Checks the copy.」、hpd.c「Checks the sink.」）を意味の comment に。
- 見て残した物: style-check の paragraph-comment の残り 12 は全部 critical section の境（`i915_dp_ext_access_end`・`i915_mmio_record_enter/leave`、道具の限界）。call-in-condition 2 は関数の様な macro。条件演算子 38 は log の短い対称の選びと register の macro。run-log.c の hook の `return result;` 7・vga.c・dp-ext-kern.c・hpd.c の `return status;` は下の hook の答えの写し・分類の値。試験の `i915_host_check` の引数の複数の節の式（約 640 行）は条件ではなく check の述語として残した（Q1 の判断に）。
- 確かめた事: `sh build/tmp/kbuild.sh p244a` default rc=0 warn=0（sha256 の先頭 a80a6ffcb8951d01）、vke2・execution rc=0 warn=0。`run-xd-host-tests.sh build/p244a-host` all passed（31 本、全部 0 failures を数えた）。`git diff --check` 0。gen9・bootfb の試験は触った file が無いので流していない。
- 残り（未実施）: comment の 1 行ずつの目視（意味を言っているか）は上の grep で拾える形だけ。関数名の Linux との 1 対 1 の写しの目視は未実施。

## 判定の材料（2026-10-18 P1、q1244）

- 満たした: 機械で見られる規則は WS245 の行で残り 0（道具の限界と短い対称の選びを除く）、上の読み通し 2 の直し、build warning 0、host 試験 all passed。
- 満たしていない: comment の 1 行ずつの目視と関数名の写しの目視（117,100 行の通読）。試験の check の述語の式の扱い（Q1 の判断）。
- P1 の見立て: cleared にしない（通読が残る）。通読を範囲から外すか、conformance-reviewer に file を分けるかは Q1 の判断。
