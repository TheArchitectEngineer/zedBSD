<!-- awesome-plan project=zedbsd record=ws245-p051 -->

# ws245-p051: port の規則と head の pipe の規則を platform の記述に

Status: cleared（2026-10-18 Q1 判定: q1229-i20・i21（5330 の実機）でこの段の「実機で確かめる事」の 5330 の項目を満たした（「判定の材料」の節、P1 q1241）。外部出力・suspend などの残りは beta2.md の U19〜U23 で追う）（旧: in-progress（2026-10-17 q1229-i13 で実装・host 試験・build 済み（1388dbae8）、5330 は全部の段の後に 1 回。外部出力: 未確認（p052 の節目で）））
Parent: [WS245](../ws.md)
Queue: q1229-i13（P1）
設計: [design.md](../design.md) §6.2 の p051 の行、§3.3（pipe の割り当て）

## 範囲

- own の output.c の port の規則（DDI B だけ・TC 4 つ・pipe B 固定）を platform の port の記述に。head-rules.c の pipe の規則を output の pipe に。hdmi.c・hdmi-mode.c・head-rules.c を消す（hdmi.c は p039 で、hdmi-mode.c は p050c で空にした）。

## 実装（2026-10-17 q1229-i13、P1。base 0b0102e81、commit 1388dbae8）

- GOP の分類（`drv_i915_gop_output_read(platform, firmware, gop)`）: DDI A の DP SST は panel、platform の combo PHY の port の HDMI・DVI は HDMI、platform の他の port の DP SST は外部 DP、それ以外（platform に無い port、Type-C の HDMI、DP MST）は灯さない。旧の「HDMI は DDI B だけ」「外部 DP は DDI 3〜6 だけ」を platform の記述に置き換えた（TGL の TC5/6 と RKL・ADL-S の combo の TC1 も通る。combo の TC1 の DP は dp-ext が Type-C でないと断るので、今は灯らず GOP の絵が残る）。
- HDMI: claim と GOP の道とも、connector の port が platform の combo PHY の port なら可（旧は DDI B だけ）。GOP の道は GOP の port の HDMI の connector を探す（旧は最初の HDMI の connector）。link の plan は connector の DDI で（`drv_i915_output_link_hdmi/_dp` に ddi の引数）。
- pipe: `struct i915_display_output` に `pipe` を足し、準備が決める（panel は A、HDMI と外部 DP は B: `I915_OUTPUT_PANEL_PIPE`・`I915_OUTPUT_OTHER_PIPE`）。`drv_i915_display_output_pipe` はそれを返す。resident.c の HDMI・DP の run の params・config・preflight は output の port と pipe を使う（`I915_OUTPUT_HDMI_PORT/_PIPE`・`I915_OUTPUT_DP_EXT_PIPE`・`I915_RESIDENT_TRANS_CONF_B` を消した。preflight の log は `HDMI-B preflight` → `HDMI preflight`、TRANSCONF の pipe 名を出す）。
- head-rules.c・head-rules.h を空にし、claim の判断（`i915_head_claim_way`）と型は head.c の static に、pipe の衝突は head.c の比べ 1 行に（R11: head-rules.c の ws113 の参照も消えた）。
- 既定の案（実機で確かめてから変える物）: design §3.3 の「resident は GOP が灯していた pipe」は採らず、外部出力は今の pipe B のまま（GOP が HDMI を pipe A で灯していても B で灯す: 今と同じ。pipe A の HDMI・DP は 5330 で試していない）。HDMI の mode の選択（`drv_i915_output_pick_mode`、own の code）は output.c に残した（xd-edid へ移すと WS075 の hdmi/host-output-test.sh が output.c だけを link する形が壊れる。Linux 由来の code ではないので出典の監査には関わらない。EDID の DTD の decode の重複（output.c と xd-edid）の整理は p042/p043 で）。

## 確かめた事

- host-output-link-test に GOP の分類の 11 checks を足した（32 checks 0 failures）: ADL-P の A の DP SST・B の DVI・A の HDMI・TC1 の DP SST、灯さない物（DDI C・TC1 の HDMI・DP MST）、TGL の TC5、RKL の combo TC1 の DP と HDMI、port を駆動しない lit の pipe。
- build 3 組 warning 0、run-xd-host-tests 27 本 all passed、WS075 hdmi/host-output-test 80 checks PASS。R11 0、`git diff --check` 0。

## 他の WS の試験（判断は Q1）

- `plan/ws113/tests/host-head-rules.c`・`host-head-rules.sh`: 空にした head-rules.c を link する（この段で壊れる）。master の一覧・未完の Phase の参照なし（phase の過去の記録だけ）。案: 削除（判断は head.c の static に移った）。
- `plan/ws113/tests/host-gop.c`・`host-gop.sh`: output.c から sed で 2 関数を取り出す試験で、旧の N0 の report の型を前提にしており base で既に壊れていた。案: 削除（oracle は host-output-link-test の GOP の checks）。

## 実機で確かめる事

- 5330 の起動（GOP は panel）で `display output: eDP panel, the firmware's output` が今と同じ。節目（p052 の後、ユーザーの一覧）: HDMI の plug で 2 画面（head は pipe B）、HDMI の monitor を挿したまま lid を閉じて起動した時の GOP の HDMI（port の connector の探し方が変わった）。

## 削除と掃除（Q1）

- `src/drivers/gpu/i915/display/head-rules.c`・`head-rules.h`（中身を空にした）。

## 判定の材料（2026-10-18 P1、q1241）

証拠: i20 = q1229-i20（T1、2026-10-17、5330 の実機、tip 92f134d99。survey の md は t1 の 396cbfce4 の `plan/ws245/tests/evidence/t1229i20-tip-20261017.md`・`t1229i20-tip-debug-20261017.md`、dmesg と `5330-survey-diff.py` の出力は t1 の worktree の `plan/ws245/temp/5330/t1229i20-*`（git の外））。i21 = q1229-i21（merge 後の main 61dde9d6b、24f2329ea の `t1229i21-main-merged-20261017.md`、dmesg は同じ場所）。基準 = q1229-i18 の main の dmesg（同じ場所の `t1229i18-main-*`）。debug の比べ（`t1229i20-diff-main-debug.txt`）の差は phase046 の q1229-i19 の表の I・H の行だけ（steady の差は PP_CONTROL の survey 2 の 0x6f/0x67 = H7 だけ）、素の起動の比べ（`t1229i20-diff-main.txt`）steady 差 0 PASS。i21 も steady 差 0。

満たした基準:

- 起動の基準（graphical、msgbuf_dropped 0、HANDOFF、lease 1 claimed、picture up、device lost 0、backlight-probe の get・set 100・get）は i20・i21 とも PASS
- `display output: eDP panel, the firmware's output (DP SST on DDI A, pipe A, lit pipes 0x1)` が基準と同じ

満たしていない・未確認の基準と残る物:

- HDMI の 2 画面（head は pipe B）、蓋を閉じた起動の GOP の HDMI: ユーザーの一覧（beta2.md の U19〜U23）の項目で、未実施

P1 の見立て（Status は変えない。判定は Q1）: cleared にしてよい。
