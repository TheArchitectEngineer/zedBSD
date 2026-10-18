<!-- awesome-plan project=zedbsd record=ws244-p007 -->

# ws244-p007 compiler・render の Mesa・Linux の言及

Status: planned（2026-10-19 q1260 P1 が規模を測り、再開の手順を書いた。未着手）

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
