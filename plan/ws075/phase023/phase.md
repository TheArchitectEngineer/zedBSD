<!-- awesome-plan project=zedbsd record=ws075p023 -->

# ws075-p023: 性能: どれが compositor を重くしているかを測る（分岐の中の ALU・draw ごとの停止・すりガラス）

Phase ID: `ws075-p023`
Parent: [WS075](../ws.md)
Status: cleared（2026-09-30。分岐の中の ALU を囲いごとに IF で飛ぶ。実機の passthrough の 10 app で compositor の 1 run 9.3〜9.7 → 6.7〜6.8 ms、flip の率 7.7 → 9.5/s、画面は前後で画素まで同じ（時刻の文字と animation を除く）、vkx・vke1・vke2・vkc PASS、boot test PASS、X terminal の後の停止は起きない）
Phase disposition: normal
承認: 2026-09-29 main の判断（p022 の後。p023 の最初の段で (a)・(b) を測り大きい方を実装。ユーザーの見立てで (c) すりガラスを切った場合を追加）。

## 計測の方法

実機の 5330 の VFIO passthrough の QEMU、demo の passthrough の image、kei の session。`plan/ws075/tests/hdmi/measure-apps.sh`
（lock の下で desktop だけの物差し → App Home の 10 app を開く → 同じ物差し → session ごとの engine の時間（`engine-gdb.sh`）→ 撮影）を
各 image で 2 回。表は `plan/ws075/tests/hdmi/summarize.py` の出力。「1 run」は最も engine を使う session（compositor）の 1 回の実行の時間。

試験の build の変更は tree に入れていない（patch と shader は `plan/ws075/phase023/exp/`）:

| 記号 | 変更 | patch |
| --- | --- | --- |
| base | main（80314042）の tree、p021 まで | — |
| (a) | compositor の panel.frag を「全ての draw が image の道」の版に差し替え（kernel が module を見て置換。分岐の中の ALU と sample を除いた上限の見積もり、絵は崩れる） | `exp-a.patch`・`panel-image-only.frag` |
| (b) | draw ごとの PIPE_CONTROL 4 つ（context setup の flush と invalidate、primitive の前の CS stall、後の RT・depth・DC の flush）を出さない（STATE_BASE_ADDRESS は残す） | `exp-b.patch` |
| (c1) | すりガラスのぼかしだけ切る: compositor の backdrop（窓の下の scene の再描画と blur の pass）を出さない（glass は blur 済みの壁紙のまま） | `exp-c1.patch`（userland/desktop/wayland/backdrop.c） |
| (c2) | glass を全部切る: (c1) に加え、panel.frag の glass の分岐を単色（panel の色）に（kernel が module を置換） | `exp-c1.patch` + `exp-c2-shader.patch`・`panel-glass-solid.frag` |

## 結果（2026-09-29〜30）

| image・回 | 10 app: flip の率 | 10 app: latency 中央値（範囲） | compositor: 1 run・占有・run/s | engine 全体 | desktop だけ |
| --- | --- | --- | --- | --- | --- |
| base 1 | 8.3/s | 65.2 ms（15.1〜115.5） | 8.81 ms・49.5%・56.1 | 58.2% | 58.1/s・16.0 ms |
| base 2 | 8.6/s | 47.9 ms（15.3〜182.0） | 7.94 ms・49.5%・62.4 | 57.7% | 58.4/s・31.8 ms |
| (a) 1 | **12.4/s** | 48.5 ms（15.0〜115.2） | **4.86 ms**・41.7%・85.9 | 51.8% | 58.3/s・16.1 ms |
| (a) 2 | 無効（xterm を開いた後に画面の更新が止まった: PLANE_SURFLIVE が 0、実験の shader の版でのみ、原因は未調査） | — | — | — | 58.3/s・15.9 ms |
| (b) 1 | 8.1/s | 65.2 ms（14.9〜115.2） | 8.48 ms・49.8%・58.8 | 58.8% | 58.3/s・15.9 ms |
| (b) 2 | 8.2/s | 48.3 ms（15.2〜82.3） | 8.76 ms・50.3%・57.4 | 58.7% | 58.5/s・15.9 ms |
| (c1) 1 | 10.6/s | 32.7 ms（15.1〜98.6） | 9.19 ms・48.8%・53.1 | 55.7% | 58.3/s・15.9 ms |
| (c1) 2 | 10.8/s | 65.3 ms（15.1〜115.5） | 9.49 ms・49.6%・52.3 | 56.5% | 58.6/s・16.0 ms |
| (c2) 1 | 10.4/s | 49.3 ms（15.1〜114.5） | 8.94 ms・48.6%・54.4 | 56.3% | 58.3/s・15.8 ms |
| (c2) 2 | 比較できない（Gears の窓が出ず、Gears の animation が無い: 33.3/s・32.4 ms・2.66 ms/run） | — | — | — | 57.9/s・15.9 ms |

画面: 各 run の最後（X terminal まで開いた後）を並べた図 `build/ws075-shots/ws075-p023-variants.png`（上段 base 1・2、(a) 1・2、(b) 1、
下段 (b) 2、(c1) 1・2、(c2) 1・2）。(a) は絵が崩れる（全ての panel を画像として描く）。(c2) 2 は Gears が無い。
各 run の撮影は `build/ws075-p023/m-*/shots/`。

## 読み

- (a) 分岐の中の ALU（と使わない sample）を除いた上限: compositor の 1 run が約 8.4 → 4.9 ms（約 -45%）、flip の率 8.5 → 12.4/s。
  一番効く。ただし image の道だけの上限の見積もりで、実装（分岐ごとに飛ぶ）ではこれより小さい。1 回は画面が止まった（実験の版だけ）。
- (b) draw ごとの停止と flush: 変わらない（8.48・8.76 ms）。今の compositor では効かない。
- (c) すりガラス: ぼかし（backdrop）を切っても、glass を全部切っても、compositor の 1 run は変わらない（9.2〜9.5・8.9 ms）。
  flip の率は約 8.5 → 10.6/s に上がる（run の数が減る: 56〜62 → 52〜54 run/s）。GPU の時間の大半は glass の効果ではなく panel.frag
  そのもの（全ての分岐を全 pixel で）と見る（推測）。latency の中央値は run ごとのばらつき（32〜66 ms）が大きく、差を言えない。
- よって (a) → p023 のまま「分岐の中の ALU を飛ぶ」を実装するのが次（main の判断どおり）。(b) は ws.md の候補に残す。
  すりガラスをやめるかは効果が小さい（flip の率 +25% 程度、1 run は同じ）という材料をユーザーへ。

## 実装（2026-09-30）

ユーザーの判断（2026-09-30、main 経由）: 「分岐の中の計算を飛ばす、にします」。すりガラスは残す。

受け入れ条件（main の指示）: 実機の passthrough の 10 app で compositor の 1 run が縮む（前後それぞれ 2 回以上）・画面が前後で画素まで
同じ（時計などを除く）・guard/run.sh と vk の host 試験と test-hw.sh（vkx・vke1・vke2・vkc）が PASS・QEMU の boot test・latency の中央値の記録
（WS099 の C6 の材料）・(a) の 2 回目の X terminal の後の画面の停止が起きないこと（起きれば原因の調査）。

今までの compiler は構造化された分岐を if 変換し、全ての block を全ての channel で predicate の下に実行していた（store・phi・edge は
predicate で選ぶ）。p021 で分岐の中の texture の send だけを IF で飛ぶようにした。p023 は block の ALU もまとめて飛ぶ。

- IR（`compiler/ir.h`）: `I915_IR_SKIP_BEGIN`（src[0] は block の predicate）と `I915_IR_SKIP_END`。
- parser（`compiler/spirv.c`）: loop の header でなく predicate が常に真でない block を SKIP_BEGIN・SKIP_END で囲む（block の label で開き、
  merge・branch・switch・return・kill・unreachable の前で閉じる）。囲いの中で作った定数（ID_CONSTANT の cache・zero・one・true）は
  閉じた後に作り直す（loop を閉じた後と同じ。飛んだ時は register に値が無いため）。
- 安全の証明（`compiler/compile.c` の `i915_compile_skips`）: 囲いごとに、飛んだ時に値が壊れても見えないことを証明できたものだけを飛ぶ。
  Boolean の atom（最大 8 個）の真理値表（256 通り）で「値が garbage でありうる channel の組」を追う。囲いで作った値は全ての組で
  garbage。SELECT は選ばれた側の garbage だけ、AND・OR は他方が偽・真の組だけを受け継ぐ。garbage が P（囲いの predicate）の組の中に
  収まった値は、P の channel では実行されて正しいので clean。見える garbage が output・storage への store・kill・loop の move・loop の
  終わり・次の囲いの predicate に届けば、その囲いは飛ばない。囲いの中に loop・loop の move・output の store があるもの、P の下に無い
  kill・storage の store があるものも飛ばない。
- 生成（`i915_compile_skip_begin`・`_end`）: 「どれかの channel が P なら全 channel が入る」IF。`mov(1) f0.0<UW> 0`（NoMask、新しい
  `drv_i915_eu_flag_clear`）→ `cmp.nz f0.0 P, 0` → `mov(1) tmp.UD ← f0.0`（新しい `drv_i915_eu_flag_store`、Mesa の brw_asm と同じ bytes を
  確認）→ `cmp.nz f0.0 tmp<0,1,0>UD, 0` → `IF`。最初の clear は必要: cmp は動いている channel の bit しか書かず、動いていない channel
  （primitive の外の pixel、SIMD8 の bit 8〜15）の bit は前の値（thread の開始では不定）のまま残るので、clear が無いと「どれか」が
  常に真になる（Mesa の any の vote も同じ理由で flag を先に埋める、`brw_lower_subgroup_ops.cpp`）。入る時は全 channel が
  入るので、微分と implicit LOD の sample は今までと同じ値。p021 の texture の guard もこの IF に替えた（囲いの中の texture は囲いの IF に
  任せて guard しない）。ENDIF の位置と loop の中の JIP の付け替えは p021 の仕組みを使い、IF・ENDIF の数は `COMPILE_MAX_GUARDS` を共有。
- 短い囲いは IF を付けない（`COMPILE_SKIP_MIN_INSTRUCTIONS` 6、IR の命令の数）: IF は EU の命令 6 つ（clear・cmp 2・flag の store・
  IF・ENDIF）と jump がかかり、else-if の鎖の次の比較（IR 2 命令）や merge の select（4 命令）では飛ぶ得より入る損が大きい。証明
  （`skip_ok`）は全ての囲いに行い、閾値は生成の段だけで見る（lower の試験の poison の検査は全ての囲いに残る）。panel.frag の IF は
  17 → 7、命令は 485 → 425。
- spill: spill の store は定義の直後にしか出ない（読むたびに fill）ので、囲いの外で作った値の slot が囲いの中で書かれることは無い。

host の試験:

- `plan/ws031/tests/i915-vk-lower-test.c`: `compile.c` を取り込み、証明された囲いを predicate が 0 の時に飛び、その値を poison（0 と
  0xFFFFFFFF の 2 通り）にして走らせ、出力・storage・kill が飛ばない実行と一致することを全ての場面で比べる（証明 389148・飛んだ 694448、
  結果は同じ）。
- `plan/ws031/tests/i915-vk-compile-test.c`: EU の model に IF・ENDIF（active の mask の push・pop、残りが 0 なら JIP へ）と flag の
  store の MOV を加えた。thread の開始の flag を 0xFFFF（hardware では不定）にし、flag の store で active の外の bit が 0 であることを
  assert する（flag の clear の無い版はこの assert で落ちる、下の経緯）。閾値の後で IF 11168 回、うち 512 回を飛んだ、結果は同じ。
- compositor の panel.frag（host で compile）: 全ての囲いが証明されて飛ぶ（拒否 0）。

## 検証（2026-09-30）

| 確認 | 結果 |
| --- | --- |
| `sh plan/ws031/tests/run-vk-host-tests.sh <name>`（spirv・lower・eu・compile・pipe・resdispatch、各 plain と ASan/UBSan） | PASS |
| 同 cmd・res・sync・cmdbuf | FAIL（cmd は link の未定義 `drv_i915_gfx_object_create` など、res は `opcode 78 routed to the res module` の assert）。p023 の前の tree（HEAD 5dfe65e0 の複写）でも同じく FAIL なので p023 と無関係の既存の失敗（未調査、main へ報告） |
| `BRW_TOOLS=/home/awe/p014-c/mesa/build-asm/src/intel/compiler sh plan/ws031/tests/run-vk-gentool-test.sh` | PASS |
| `sh plan/ws075/tests/guard/run.sh`（brw_asm の照合・scoreboard の検査を含む） | PASS |
| `python3 plan/tools/style-check.py`（変更した file、前の tree との差分） | compiler の source は新しい指摘 0。試験の EU model に 3 件（WHILE の既存の処理と同じ形の nested declaration・call-in-condition・blank-after-brace）を残した |

| `plan/ws075/tests/test-hw.sh`（最終の code c55586b5、`I915_HOST=solaris10-man`、`BUILD=build/ws075-p023/<場面>`） | vkx 9/9・vke1 6/6・vke2 17/17・vkc 9/9 PASS（`build/ws075-p023/hw-*/run.log` の verdict の行） |
| QEMU の boot test（GPU なし、`OUTPUT=build/ws075-p023/boot-test plan/tools/boot-test.sh` に最終の demo の image の複写） | PASS（`build/ws075-p023/boot-test/login.png`） |

### 実機の passthrough（5330、QEMU の VFIO passthrough。素の 5330 ではない）

image は `plan/ws075/demo/build-demo-image.sh build/ws075-p008/pt passthrough ZEDBSD_GRAPHICAL_BOOT=n
"ZEDBSD_BOOT_EXTRA_LINES=display=edp login=graphical"` で、compiler の file だけを変えた 4 つ（`build/ws075-p023/*.img`、kernel は `vmunix-*`）:
pre（compiler を 5dfe65e0 = p023 の前に戻す）、post（be3f7089、flag の clear が無い）、fix（1fc4d352、clear を足した）、
tune（c55586b5、短い囲いに IF を付けない、最終）。各 run は `plan/ws075/tests/hdmi/measure-apps.sh`、表は `summarize.py`。
pre・post・fix・tune を交互に（pre 1 → post 1 → pre 2 → post 2 → fix 1 → pre 3 → fix 2 → tune 1 → tune 2）。

| run | 10 app: flip の率 | 10 app: latency 中央値（範囲） | compositor: 占有・1 run・run/s | engine 全体 | desktop だけ |
| --- | --- | --- | --- | --- | --- |
| pre 1 | 7.7/s | 65.6 ms（15.9〜148.6） | 50.3%・9.32 ms・54.0 | 59.2% | 58.4/s・32.7 ms |
| pre 2 | 7.8/s | 81.5 ms（15.7〜131.8） | 50.9%・9.65 ms・52.7 | 59.5% | 58.5/s・32.5 ms |
| pre 3 | 7.7/s | 82.8 ms（14.9〜136.3） | 50.0%・9.43 ms・53.0 | 57.7% | 58.5/s・32.4 ms |
| post 1 | 4.9/s | 115.3 ms（15.7〜164.6） | 62.7%・18.34 ms・34.2 | 69.2% | 58.4/s・32.6 ms |
| post 2 | 4.9/s | 98.8 ms（64.8〜131.9） | 62.2%・18.40 ms・33.8 | 69.2% | 58.4/s・32.7 ms |
| fix 1 | 8.9/s | 48.4 ms（15.3〜81.5） | 48.0%・7.79 ms・61.6 | 57.0% | 58.4/s・15.9 ms |
| fix 2 | 9.0/s | 65.6 ms（15.5〜132.5） | 48.1%・7.55 ms・63.7 | 57.0% | 58.1/s・32.3 ms |
| **tune 1** | **9.6/s** | 65.7 ms（15.6〜98.2） | 46.1%・**6.81 ms**・67.8 | 54.4% | 58.5/s・15.9 ms |
| **tune 2** | **9.5/s** | 48.9 ms（2.4〜114.9） | 44.8%・**6.71 ms**・66.8 | 53.0% | 58.2/s・15.7 ms |

- compositor の 1 run: pre 9.32〜9.65 ms → tune 6.71〜6.81 ms（約 -28%）。flip の率 7.7 → 9.5〜9.6/s。engine 全体 58〜60% → 53〜54%。
  実験 (a) の上限（image の道だけ、4.86 ms）には届かない: 囲いの外に残る命令（距離の計算・else-if の鎖の比較と merge の select・
  IF の前の 4 命令）が約 160。
- latency の中央値（WS099 の C6 の材料、10 window・pointer の移動から表示まで）: pre 65.6・81.5・82.8 ms、tune 65.7・48.9 ms。
  10 回の中央値は frame の刻み（16・32・48・65 ms）に乗って run ごとに大きく揺れ、tune の 2 回のうち C6 の 50 ms 以下は 1 回だけ。
  C6 を満たしたとは言えない（この物差しでは差を言うのに回数が足りない）。
- post（clear の無い版）は 18.4 ms と p021 の前（14.8 ms）より遅い: 「どれか」が常に真で、p021 の texture の guard も含め何も
  飛ばず、IF の分だけ増えた。gdbstub・monitor ではなく host で panel.frag の code を disassemble して気付いた（上の説明）。
  EU の model に同じ条件（不定の flag）を入れて host の試験で再現させ、clear で直した。
- 画面: 各 app を開いた後の撮影（`m-*/shots/a-*-live.png`）を pre 1 と fix 1・tune 1・tune 2 で比べると、上の bar（時計、y < 33）の下では
  画素まで同じ。違いは時刻に依る文字（Files の「Good afternoon/evening」、Notes の file 名の時刻）と Gears の animation だけ（目視で確認）。
  pre 1 と pre 2 の差も同じ所だけ。並べた図 `build/ws075-shots/ws075-p023-final.png`（pre 1 と tune 2 の最後）。
- X terminal の後の画面の停止（実験 (a) 2）: 9 run のどれでも起きない。X terminal を開いた後も flip は続き（tune 9.6〜9.7/s）、
  2 秒あけた 2 枚の撮影で Gears が動いている（live buffer が同じ run も中身は違う）。(a) の停止は実験の shader の版だけのもので、
  原因は未調査のまま（起きないので調べていない）。

## 未実施・残り

- 素の 5330（USB の image での起動）での計測: 未実施（passthrough だけ）。
- latency の C6（中央値 50 ms 以下）: 2 回のうち 1 回だけ。回数を増やした物差しが要る（WS099 側の判断）。
- 残る compositor の時間の候補（ws.md の候補に）: 同じ predicate の flag を select ごとに作り直している（`cmp` + `sel` の組）こと、
  距離の計算を全 mode で行うこと（shader の側）、draw ごとの停止（(b)、今は効かない）。
- host の cmd・res・sync・cmdbuf の fixture の失敗（p009 から既存、p023 と無関係）。
- 実験の段: (c2) の 2 回目は Gears が出ず比較できない（1 回だけ）。(a) の 2 回目は画面が止まり無効（1 回だけ）。
