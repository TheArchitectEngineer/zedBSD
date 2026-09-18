# WS031 E-110 報告: scanout object（実機 PASS）、LCD-A の transcoder 語、SPIR-V 基礎 lowering

2026-09-19。対象機 = Latitude 5330（ADL-P 8086:46a8、VFIO/QEMU）。台帳 `plan/ws031/results-ws031.md` E-110。path は `agent-1:~/zedBSD/` 基点。
制約の維持: GPU = vfio-pci、HAL（10 ms tick）非変更、execlists、累積修正保持、git commit／push なし、著作権者名の推測記入なし。

## 0. 先に結論
| 項目 | 結果 |
|---|---|
| scanout object | 実装＋実機 **PASS**（PTE 2025／guard 168／内容の読戻し一致、GT 窓の PTE 不変、解放後 scratch へ復帰）。表示 engine の register には未接触 |
| LCD-A 第 2 片 | 正本の writer から transcoder／M-N／PIPESRC の **13 語を書込み順つきで生成、Linux の dump と全一致**（host／ktest／実機の実 AUX データ）。未書込み |
| sleep | `kern_usleep_range` へ統一（実機: 200 ms 待ち = tick 200.3 ms、busy 0） |
| 回帰 | 同一ソースで **8/8 PASS**（6 モード＋明示 VBT×TEX＋AUX/scanout）、ktest 449/0、`DC state mismatch` 0 |
| SPIR-V | **出荷版 `-O0` の vkdemo VS／FS が通常経路で lowering＋EU 生成まで通過**。GPU 検証は **0 件**（そう表示している） |
| capability | レビュー資料を作成（コード変更なし）。決定待ちの項目を表にした |
| 点灯 | **していない**。LCD-B は次の次 |

## 1. ご質問への回答（vkdemo の lower できない命令と難易度）— 実装して確かめた結果
- 該当命令: Function storage の OpStore／OpLoad（local 8 個）、float OpConstant の operand 9 個、OpFNegate 1 個、成分 OpAccessChain 17 個、3〜4 要素の OpCompositeConstruct 3 個。
- **個々は「簡単だが未実装だっただけ」**。分岐も phi も無い直線 code なので、SSA 構築・支配木・register 割当ての本格的な pass は不要でした。
- 本当の不足は IR の data model（「1 値 = 1 GRF」で成分が表せない）で、**IR を scalar 化**して解決しました。vector の構築・抽出・shuffle・local の store／load は IR を 1 個も出さず、parser が「どの scalar がどの成分か」を名前づけするだけになります（成分の欠落・重複が構造的に起きない）。SIMD8 では「float 1 個／channel」= GRF 1 本なので hardware の model とも一致します。
- **残っている重い部分は compiler pass ではなく hardware との規約**: payload の register 配置、VS の URB 書込み message、sampler message の descriptor、FS 入力の補間（barycentric）。ここは未検証で、「EU生成済み」に含めていません。

## 2. scanout object
- 仕様: XRGB8888／linear／1920×1080、cpp 4（link の 18 bpp とは無関係）、pitch 7680、`PLANE_STRIDE` 単位 120（Linux の dump 0x70188 = 0x78 と一致）、2025 page、surf 整列 256 KiB、guard 168 page、linear は DPT 不使用。値は正本の関数を読んで得た期待値で、試験の比較先。
- 配置: `gt_mem` に表示用窓（8192 page、GT 窓の直下、独立 bitmap、明示的に確保した構成だけ）。全 page を encode してから PTE を書く。無関係な PTE の初期化や既存 object の移動はしない。
- 解放規則: IN_USE の buffer は解放拒否。停止未確認なら `abandon` → `gt_mem_fini` は解放せず log（cleanup の偽装なし）。
- 実機 log（抜粋）:
```
SCANOUT-TEST window: ggtt_entries=1048576 gt_window=[1048320,+256) display_window=[1040128,+8192) gt_pages_in_use=185 objects_live=52
SCANOUT-TEST buffer: XR24 linear 1920x1080 cpp=4 pitch=7680 (stride units 120) size=8294400 pages=2025 align=0x40000 guard=168 | surf=0xfdfc0000
SCANOUT-TEST check: pte_bad=0/2025 guard_bad=0/168 gt_window_ptes_changed=0 | pattern id=110 fnv=ce63f20b23f91f85 (pinned ce63f20b23f91f85) readback_bad=0
SCANOUT-TEST verdict: PASS (unpin=0 destroy=0 ptes_back_to_scratch_bad=0 display_pages_in_use=0 gt_window_ptes_changed=0 display_pte_writes=4386)
```
- ktest `scanout:` 15 件（正常／既存 object と共存／窓不足で無書込み失敗／IN_USE 解放拒否／abandon 保持／整列・guard）。

## 3. LCD-A 第 2 片
正本の書込み関数そのもの（`intel_cpu_transcoder_set_m1_n1`／`intel_set_transcoder_timings`／`intel_set_pipe_src_size`）を生成 file に入れ、`intel_de_write` を emit hook へ。13 語が正本の順（LINK_N 最後）で Linux の dump と一致。生成 file は **source ごとに分割**（1 file に 3 つの元が混在していた E-109 の状態を解消）。`check_generated.sh` = 全一致。

## 4. SPIR-V 基礎 lowering（LCD と並行、host のみ）
助言の 5 単位（型・定数／Function storage と成分 access／composite・vector／FNegate・DOT／I/O・resource）を一括で実装。状態は三段階で、**上の段は下の段を含みません**。
| 段 | 確認方法 | vkdemo VS | vkdemo FS |
|---|---|---|---|
| lowering済み | IR を interpreter で実行 → 独立に書いた式と比較（新規 `i915-vk-lower-test.c`） | ✔ 50 IR 命令・44 値、4 頂点×3 時刻で `gl_Position`＋`texture_coordinate` 一致 | ✔ u・v の順、set／binding |
| EU生成済み | **生成 word を bit field から decode して 8 channel 実行する model** → 同じ独立式（`i915-vk-compile-test.c`） | ✔ 51 EU 命令、r16〜r23、8 頂点同時で一致 | 形だけ（sampler SEND の descriptor = 0） |
| GPU検証済み | 実機 | ✘ | ✘ |

偶然一致しない入力（助言どおり）: local に 5→load→2 で上書き→load で (5, 2, 3, −3)／(1,2,4,8) を成分ごとに抽出・逆順・shuffle／local の 1 成分だけ上書き／dot = 70、negate −70、70−5 = 65 と 5−70 = −65。EU model では push register の残り 7 float と、属性に無い入力成分に junk を置き、誤読を検出します。
拒否は維持（一括許可なし）: 分岐・call・FDiv・比較・変換、動的 index、initializer／struct／array の local、未解釈の decoration（Flat、Component、ArrayStride …）と module-level 命令。decoration は「解釈する／名前を挙げて無視（RelaxedPrecision のみ）／それ以外は拒否」の明示表。pipeline 拒否試験は「出荷版 VS の OpFMul 1 個を OpFDiv に変えた妥当な module」で継続。
同時に見つけて直した誤実装: `i915_vk_eu_mad` が operand を encode していなかった（IR から FMAD を削除、encoder は呼ばれたら error）／parser が IR 配列満杯時に黙って命令を捨てていた。
項目別の表（使用箇所・受理条件・状態・小試験）: `handover/notes/vk-lowering-status.md`。VK host fixture 10 本 PASS（通常＋ASan/UBSan）、kernel build（-Werror）通過。
注意: compiler の変更は sweep の build 完了後に適用したので、§5 の回帰 image には入っていません（vk/ は parity 経路から呼ばれません）。次の sweep で同一ソースになります。

## 5. 回帰（同一ソース、clean build → GPU-free 449/0 → 実機 1 回ずつ）
EU-REPEAT 5/5／DRAW 1024/1024／R1 12/12／TEX 1024/1024／T3 9/9／BL 4/4／明示 VBT×TEX 1024/1024（VDD-off worker が並行して発火、`edp fini` refs 0・lock_errors 0）／AUX-TEST PASS＋SCANOUT-TEST PASS。全て `probe=COMPLETE cleanup=1`。P1 reset 周りの既知行は E-109 と同数。

## 6. レビューをお願いしたいもの
`handover/notes/vk-capset-contract-review.md` — STRICT_QUEUE（順序の単位、成功通知の時点、複数 request への展開、失敗時の後続）と QUIESCE（範囲、返却時の保証、hang 時、`RETAINED`）の決定項目。決定まで capset の suffix／flags は立てません。

## 7. 次
1. LCD-A の残り: plane 語（`skl_plane_ctl`／`glk_plane_color_ctl`／stride・size・surf を scanout の layout から。ADL-P の `PLANE_CTL_ARB_SLOTS` WA を含む）、`TRANS_DDI_FUNC_CTL`（比較先 0x8a210002）／`TRANSCONF`／MSA／`DDI_BUF_CTL`、DDI buf trans と signal level、DPCLKA／DPLL enable 列、CDCLK／帯域／DBUF／WM、enable／disable の状態列と前提条件表。
2. backlight（`cnp_pwm_funcs`、rawclk readout）、link training（CR／EQ／symbol lock／alignment を成功の証拠として記録）。
3. fake 試験 3 系統 → LCD-B 実機 1 回（buffer 読戻し＋register／link 状態＋写真）。
4. compiler: SEND descriptor（出典つき転記）→ 3D state との突合せ → 初の GPU 実行試験。
