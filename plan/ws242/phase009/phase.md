<!-- awesome-plan project=zedbsd record=ws242-p009 -->

# ws242-p009: 規約の全文と出典の監査

Status: cleared（2026-10-29 P5、q1678-i02。下の「見直し」）
Disposition: normal
Parent: [WS242](../ws.md)
Queue: q1678-i02（P5）
依存: ws242-p003〜p008
設計: [design.md](../design.md) 第 2 版の表の p007

## 範囲

- WS242 が変えた C の全文を `plan/coding-style.md` の全文で読み直す（書き方だけ）。出典の block ごとの「Value(s) referenced:」「Procedure referenced:」
  （guardrail）、`sm75-schedule.c` の Collabora の表示の扱い（p002 §3.1 の Q1 の判断）。

## 見直し（2026-10-29 P5、q1678-i02）

読んだ物: q1678 の差（`git diff 2d551be4a HEAD -- src`: compute-launch.c・nvrtx-compute.h・compute.c・draw.c・gsp.c・hold.c・identify.c・layout.c・node.c・nvrtx-gsp.h・nvrtx-gpu.h・nvrtx-private.h・nvrtx.c・stage.c・vk/nvrtx-vk.h・vk/vk-execute.c・vk/vk-pipeline.c）の全文、
p001 の identify.c の family の record と products.c、p002 の compiler の SM で分かれる所（compile.c の `_sm` の 2 関数と `nvrtx_sm75_known_model`、encode.c の `nvrtx_sm75_memory_order`、schedule.c の `nvrtx_sm75_raw`・`_raw_turing`・`_raw_ampere`・`nvrtx_sm75_predicate_raw`）を `plan/coding-style.md` の全文と guardrail の出典の節で。

直した（書き方だけ、動作は不変）:

| file | 直し |
| --- | --- |
| `nvrtx-compute.h` | file の説明の comment の 1 行が 100 桁を超えていたのを折り返した |
| `nvrtx-gsp.h` | `nvrtx_gsp_hold_falcons()` の宣言が layout の宣言の群の途中に挟まっていたのを、record の群の後の独立の行に |
| `compiler/sm75/compile.c` | `nvrtx_sm75_compile_stage_sm` の「Parses the compute stage.」（全部の stage を読む）を「Parses the module's stage.」に、`nvrtx_sm75_known_model` の最後の return の comment の「Succeeded:」（失敗の答え）を外した |
| `identify.c` | 出典が TU10x の block に 4 family 分まとめて書かれていたのを、family ごとの block（tu102.c・tu116.c・ga102.c・ad102.c と open-gpu-doc の class・QMD の版）に分けた（guardrail の「block 単位」） |
| `products.c` | 表の comment と `products_table` の間の空行を外した（comment は隣に） |

確かめ: `make -j16 ZEDBSD_CONFIG=config/ci/config-amd64.mk BUILD=build/ws242/k3 build/ws242/k3/vmunix` rc 0・warning 0、**vmunix は p008 で実機で確かめた k2 と byte で同じ**（`cmp`）。identify 24 boot choices・sm75 287・gsp 393 SKIPPED 0・qmd 733 が PASS、`git diff --check` 出力なし。

直さなかった判断:

- family の record（`identify.c`）は ANSI C の位置の初期化子のままにした。欄の意味は `nvrtx-private.h` の `struct nvrtx_profile` の comment にある。行ごとの comment を足すのは 4 表 × 31 欄の書き換えで、読み手の得は小さい。
- `node.c` の owner の口の `session = owner;` に段落の comment が無いのは、同じ file の既存の 5 関数と同じ形（file の中で揃える）。
- 試験だけの boot word `nvrtx.fail=`（p008 で足した）: coding-style §12 は試験だけの環境変数の切り替えを production の code に置かないとする。`nvrtx.fail=` は boot line の明示の語で、既定の動きは変えず、`nvrtx.stop=`・`nvrtx.disp.test=1`・`nvrtx.kill=0` と同じ種類の調べの口（実機でしか通せない止めの道を確かめる唯一の方法）なので残した。外すなら Q1 の判断。
- `sm75-schedule.c` の Collabora の表示（p002 §3.1 の保留）: 今の `compiler/sm75/schedule.c`・`isa.h` の先頭は Zlib の header だけで、Collabora の notice と出典の一覧は既に無い（block ごとの「Values referenced:」が schedule.c に 11・isa.h に 37）。この Phase で直す物は無い。
- 開発の管理情報（WS・Phase の番号、plan/ への参照）: 読んだ範囲の source に無い。
