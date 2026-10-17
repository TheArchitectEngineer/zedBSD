<!-- awesome-plan project=zedbsd record=ws245-p046 -->

# ws245-p046: 規約の全文の見直しと p045 の出典の監査

Status: in-progress（2026-10-17 q1229-i16: 機械で確かめられる規則の全部と、直した所の読み直しを実装・host 試験・build 済み（0e85d1ff1・a5e461698・a3849a76f・16de8c315）。全文の目視の読み通しは未実施（下の「制限」）。5330・SV7・5320 は全部の段の後に T1 で 1 回）
Parent: [WS245](../ws.md)
Queue: q1229-i16（P1）
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

## T1 に渡す実機の確かめ（全部の段の後に 1 回。branch agent/p1-ws245 の tip、基準は main の kernel）

手順は ws.md「5330 の素の起動での段ごとの回帰の手順」と p047 の survey。失敗したら ws.md の「段の境の SHA」で二分探索。

### 5330（Latitude 5330、ADL-P、素の起動）

1. 起動の基準: graphical の login、`kern.msgbuf_dropped` 0、sessiond の SESSION start と HANDOFF、`lease 1 claimed`、`picture up` 2、device lost 0、`perf: … per submit` が基準と同じ桁、`backlight-probe` の get・set 100・get。
2. survey（`plan/ws245/tests/5330-survey.sh` と `5330-survey-diff.py`）: lit（PP_STATUS on、PWM の duty > 0、frame counter +3 以上、SURFLIVE が resident の buffer、CRC の 2 回が 0 でない、underrun 0、TRANS_CONF on）。register が基準と同じ: PWR_WELL_CTL2 0x00000c03、DPLL0 ENABLE 0xcc000000・CFGCR0 0x00e001a5・CFGCR1 0x88、DPCLKA_CFGCR0、DDI_BUF_CTL(A)、TRANS_DDI_FUNC_CTL 0x8a210002、PLANE_CTL 0x94000000、PLANE_WM・PLANE_BUF_CFG・WM_LINETIME・MBUS_CTL・DBUF_CTL・PIPE_MBUS_DBOX_CTL、DC_STATE_EN（DMC の program があり表示が止まっている間は DC6）。
3. `i915.debug=display` の起動で書き込みの全部の列を基準と比べる（difflib の整列、`5330-survey-diff.py`）。意図した差: power on の前の PP_ON/OFF_DELAYS（p022）、SOUTH_DSPCLK_GATE_D の bit 15（p035）、takeover と sanitize の順（PIPEDMC_CONTROL の 1 書き込み、PPS・PWM の順: p037・p050a）、rmw が read と write に（p050b）、別の thread の書き込みの相対の順（p019）。それ以外の差は FAIL。
4. 起動の log（今の文言、design §6.4.1）:
   - `i915: display power: <数> wells (Alder Lake-P), DC states allowed 0x4000000a, entered when idle 0x2`、`display power: combo PHYs up: N (mask …)`（0 か PHY B だけ）、`display: core started: cdclk=… vco=… dbuf=0x… start reference held`（cdclk は GOP の値を保つ）。
   - `display: PCH type=8 … source=1`（ADP）、`display: CD clock: …`、`display: memory: raw=… type=…`・`QGV n: … MB/s for one plane`・`display: bandwidth: restricted=1 … answer=0 sagv=1`、resident の `QGV mask 0x… -> allowed 0x… bw … MB/s` が基準の値と同じ。
   - `i915: firmware: the firmware's output: …`（DDI A の panel）、`i915: takeover: lit pipes 0x1, output panel (pipe A, DDI 0)`・`pipe A stopped: DDI 0 mode 2, PLL 0, late 0x0 (rc 0)`・`rc=0, crtcs stopped 1, still active 0x0`、`the start's power reference went back: wells on A -> B`、`i915: sanitize: lit pipes 0x1, … wells off 0`、`display: serving: wells_on A -> B dc_state=… verify_mismatches=0`、`display power: well …: N users, on` の一覧。
   - `i915: edp acquire: error=0`、`edp DPCD 000:` と `edp EDID sha256` が基準と同じ、`edp late: … off in 3000 ms`。`dmc worker: proceeding to load`、DMC の版 v2.20 と MAIN/A/B/C/D の present。
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
