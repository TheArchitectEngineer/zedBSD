<!-- awesome-plan project=zedbsd record=ws244-p007 -->

# ws244-p007 compiler・render の Mesa・Linux の言及

Status: cleared（2026-10-19 Q1 判定: intel/ の header 6 file と compiler・render の Mesa の言及を直した。code の行は全 file で前と差 0（Q1 も差分が comment だけと確かめた）、既定の vmunix の hash 同じ、build 3 組 warning 0（P1）。a501d77a5・05d88e8e0・e0b196607、merge c790ae9d7）（旧: in-progress（2026-10-19 q1260-i02 P1、範囲は済み。cleared は Q1））

- 親: [WS244](../ws.md)。判断 1（2026-10-18 ユーザー）: PRM に載る command・opcode・field の名前（3DSTATE_VS、MI_*、EU の opcode）は使ってよい。Mesa・Linux にだけある独自の名前は zedBSD の名前に変える。動作は変えない。

## 規模（2026-10-19 P1 が数えた、`Mesa|Linux|brw_|anv_|SHA-256|Intel の Copyright` の行数）

| file | 行 | 言及 |
| --- | --- | --- |
| intel/genxml.h | 883 | 35（先頭に Intel の MIT の表示と Mesa の SHA-256 の一覧） |
| intel/genxml-gen9.h | 200 | 8（同上） |
| intel/genxml-video.h | 309 | 4（同上） |
| intel/eu-encoding-gen12.h | 428 | 62（先頭に Intel の Copyright 3 行） |
| intel/eu-encoding-gen9.h | 249 | 24（同上） |
| intel/null-state-gen9.inc | 1005 | 4（先頭に MIT、表は Linux gen9_renderstate.c の値） |
| compiler/compile.c | 5477 | 58（「Mesa does X (brw_*.cpp)」） |
| compiler/eu.c | 2751 | 46 |
| compiler/compile-geometry.inc・compile-compute.inc | 882+786 | 19+15 |
| render/state.c・heap.h・transport.*・memory.c・video-mfx.* ほか | | 各 2〜7（Mesa 25.0.7 の値の言及） |

## 再開の手順

1. intel/ の header（genxml*.h・eu-encoding-*.h・null-state-gen9.inc）: p003 と同じ形。先頭の Intel の MIT の表示・「Source: Mesa … SHA-256」の一覧・「transcribed」の行を外し、zedBSD の表示（Zlib）と役割の説明だけにする。block（comment で区切られた #define の群）ごとに `Values referenced: Mesa <版> src/intel/genxml/gen120.xml (<instruction 名>)` を付ける（先頭の一覧に、どの file・版から何を取ったかが書いてあるので、それで block の出典を決める。Mesa の tarball は WS244 の build/ に無いので、照合する時は Q1 に取得の許可を求める。Debian の mesa 25.0.7 の source は手元にあるかを先に探す）。null-state-gen9.inc は `Values referenced: Linux v6.19 drivers/gpu/drm/i915/gt/gen9_renderstate.c (gen9_null_state_batch)`（build/ws244-linux619 で照合できる）。Mesa にだけある macro の名前（brw_・anv_ など）があれば `I915_*` に改名し、使う file を追従。
2. compiler/compile.c・eu.c・*.inc: 「Mesa does X (brw_*.cpp)」の言及を、手順の写しか確かめながら、自前の説明と `Procedure referenced: Mesa <版> <path> (<関数名>)` に書き換える。関数の分け方が Mesa と 1 対 1 の所があれば、p004〜p006 と同じく前の版との比べ合わせ（compile の出力の EU の命令の列の前後比較、host で compile できるかを先に確かめる）で書き直す。
3. render/*: 値の言及を `Values referenced:` に。Venus の protocol の名前（vk*MESA）は protocol の名前なので残す（inventory §4）。
4. 確かめ: build 4 組（`sh` で amd64 の既定・I915_TESTS=y・I915_TEST_SET=vke2・execution、warning 0）。compiler の host 試験があれば（tests/render、WS039 の道具）流す。

## 結果（2026-10-19 q1260-i02 P1、a501d77a5・05d88e8e0）

- 照合の元: Mesa 25.0.7 の tarball（worktree の build/mesa-tools/mesa-25.0.7.tar.xz を build/tmp/p007-mesa に展開）と Linux v6.19（build/ws244-linux619）。引用元に書いた file・関数・表の名前は全部この tree で在ることを grep で確かめた。
- intel/ の 5 header と null-state-gen9.inc: 先頭の Intel の MIT の表示・「transcribed」・Mesa main の commit・SHA-256 の一覧・「Mesa の generator の notice」の説明を外し、`SPDX-License-Identifier: Zlib` と役割の説明だけにした。define の群ごとに `Values referenced: Mesa 25.0.7 src/intel/genxml/<file> (<命令・struct の名前>)`（eu-encoding は brw_eu_inst.h・brw_eu.c・brw_eu_defines.h・brw_eu.h・brw_reg_type.c ほか）を付け、本文の中の出典の括弧は外した。macro の名前は元から zedBSD の物（GEN12_*・GEN9_*・EU_*）で、改名は無い。null-state-gen9.inc は Linux v6.19 gen9_renderstate.c の gen9_null_state_batch と値が全部一致（960 語、diff で確かめた）、`Values referenced: Linux v6.19 …(gen9_null_state_batch)`。表の中の Linux の comment（reloc・cmds end・state start/end）は自前の文に変えた。
- compiler/compile.c・eu.c・compile-compute.inc・compile-geometry.inc: 「as Mesa does (brw_*.cpp)」「Mesa's X()」の言及を自前の説明にし、block の末尾に `Procedure referenced: Mesa 25.0.7 src/intel/compiler/<file> (<関数>)`（値なら Values referenced）。lowering の関数は IR の命令ごとの自前の分け方で、Mesa の関数と 1 対 1 に写した所は見当たらなかったので、書き直し（前の版との比べ合わせ）は要らないと判断した。
- render/*（state.c・compute.c・video-mfx.c・image.c・heap.h・state.h・draw.c・batch.c・blit.c・blit.h・fence.c・pipeline-prepare.c）: 「as anv does」「isl fills」の言及を同じ形に。SHA-256 の行を外した。Venus の protocol の名前は無変更。
- code は不変: 各 file の comment 以外の行を前の版と diff して差が無いことを確かめた。amd64 の既定の vmunix の sha256 の先頭が変更の前後で同じ（a949bb449e3144ac）。
- build（`sh build/tmp/kbuild.sh p007b`）: 既定・I915_TESTS=y I915_TEST_SET=vke2・execution の 3 組 rc=0、warning 0。I915_TESTS=y の既定の set は今回は流していない（comment だけの変更で、既定の vmunix が同じ hash のため）。
- 機械的に出典を探す確かめ: `python3 -I build/tmp/p007-tools/check.py 'Mesa|\banv|brw_|Linux|sha256|SHA-256|\bisl\b|isl_|gentool|genX|blorp' render/* compiler/* intel/genxml*.h intel/eu-*.h intel/null-state-gen9.inc` が referenced の段落の外で 0 件。

## 残り

- 無し（範囲は済み）。
