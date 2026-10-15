# WS244 棚卸し（途中、2026-10-16 P7）

生成: `python3 -I plan/ws244/tests/inventory.py src/drivers/gpu/i915`（`--functions` で関数ごとの C/S の行）。
分類は heuristic（script 先頭の注記）。C = comment が Linux・Mesa の関数・file を出典に名指す、または derived を宣言した file で Linux の型名を使う。S = 出典の名指しは無いが Linux の型名を使う。O = どちらも無い。
**未完**: derived の file の O の関数（例 vbt.c 106、takeover.c 98、watermark.c 71、clock.c 76）は、file 全体が Linux の text からの書き換えと宣言されているので、実際は派生と見るべき物が多い。手で確かめていない。データの block（register の define・表）の出典の対応も未整理。

## file の群（file の先頭の宣言から読んだ物）

| 群 | file | 出典の宣言 | 見立て |
| --- | --- | --- | --- |
| Gen12（ADL-P、5330）の display の「Linux 環境」 | display/{ddi,dp,dp-sink,dp-internal.h,pipe,plane,clock,watermark,vbt,vbt.h,panel,panel-backlight,power,dmc,takeover,takeover-internal.h,hotplug,hotplug-internal.h,modeset,modeset-internal.h,internal.h,opregion,opregion-internal.h,aux,gmbus,hdmi,hdmi-mode,edid,edid-read,color,vblank,phy,state,diagnostics,dp-ext,dp-ext-kern,tc,dkl-phy}.c/.h | 「Derived from the Linux kernel v6.8.12」・「the Linux text rewritten in this tree's style」。Linux の型（intel_crtc_state・drm_i915_private・intel_encoder など）と関数の分け方をそのまま持つ | 写した code・comment・構造。書き直しの本体 |
| Gen9 の display | display/gen9-{mode,modeset,link,ddc,outputs,backlight,flip,node,io,display}.c | 「The code is this file's own」、順序と表は Linux 6.8.12 から | 自前（順序・表はデータとして引用元を付け直す。表の行は要確認） |
| display の自前 | display/{present,scanout,bootfb,capture,head,head-rules,output,control,dc9,interrupts,display,backlight}.c | 出典の宣言なし（display.c・interrupts.c に Linux の関数名の言及が少し） | 自前、言及の確認だけ |
| intel/ の header・表 | intel/*.h・*.inc（mreg・trans・dp・plane・vbt-defs・genxml・eu-encoding-*・fourcc・pci-ids・mocs・forcewake・null-state-gen9 など） | Linux・Mesa の MIT 表示つき、多くは「excerpted/rewritten, values unchanged」 | 大半はデータ。ただし struct の配置（vbt-defs.h の VBT block、ref.h・connector.h・wm.h の Linux の struct）は構造の写しを含む |
| GT・core | gt-power.c、workarounds.c、verify-workarounds.c、tlb.c、ggtt.c、device-info.c、ppgtt.c、memory.c、worker.c | 「Follows Linux intel_rc6_init()」「Linux wa_write_or()」など関数ごとの言及 | 関数の流れを Linux に合わせた物（gt-power 21・workarounds 23・verify 7 の関数）。値は引用で残せる |
| compiler・render（Mesa） | compiler/{eu.c,compile.c,compile-geometry.inc,compile-compute.inc}、render/{state,image,compute}.c | 「placement logic here is own」、Mesa の関数（brw_*）を規約・手順の出典として言及 | 主に自前の code に Mesa の規約（register・message の配置）を引用。eu.c の encode の field は intel/eu-encoding-*.h のデータ。要確認 |
| tests | tests/display/{aux,edp-ktest,host-lcd-test,...} | Linux の関数名の言及 | 本体の書き直しに合わせて直す |

## file ごとの要約（生成、関数の数と C/S の行数）

```
file                                     lines  derived  funcs  C    S  O    C_lines  S_lines  tables  tables_cited  defines  origin_lines
context.h                                135    -        0      0    0  0    0        0        0       0             2        1
defaults.c                               694    -        13     0    0  13   0        0        2       1             7        0
device-info.c                            944    -        15     2    0  13   219      0        2       0             66       3
device-info.h                            216    -        0      0    0  0    0        0        0       0             20       6
device.c                                 2530   y        49     0    0  49   0        0        2       0             20       3
dma.h                                    367    -        7      0    0  7    0        0        0       0             4        2
ggtt.c                                   1011   y        23     0    0  23   0        0        0       0             2        2
gt-power.c                               1416   -        32     21   0  11   898      0        1       1             7        27
gt-power.h                               234    -        0      0    0  0    0        0        0       0             12       6
irq.c                                    1234   -        32     0    0  32   0        0        0       0             15       0
irq.h                                    257    -        0      0    0  0    0        0        0       0             1        1
memory.c                                 741    -        15     2    0  13   70       0        0       0             0        2
memory.h                                 299    -        0      0    0  0    0        0        0       0             14       1
power.c                                  425    -        8      0    0  8    0        0        0       0             18       1
power.h                                  38     -        0      0    0  0    0        0        0       0             1        1
ppgtt.c                                  1119   -        29     1    0  28   17       0        0       0             3        3
ppgtt.h                                  194    -        0      0    0  0    0        0        0       0             17       1
request.h                                102    -        0      0    0  0    0        0        0       0             1        0
reset.c                                  919    -        18     0    0  18   0        0        0       0             23       1
reset.h                                  49     -        0      0    0  0    0        0        0       0             2        1
submit.h                                 115    -        0      0    0  0    0        0        0       0             1        0
tlb.c                                    369    y        3      0    0  3    0        0        2       0             16       6
tlb.h                                    62     -        0      0    0  0    0        0        0       0             1        1
verify-workarounds.c                     797    y        12     7    0  5    479      0        2       2             3        9
verify-workarounds.h                     131    -        0      0    0  0    0        0        0       0             1        1
workarounds.c                            1528   y        34     23   0  11   844      0        3       1             1        31
workarounds.h                            228    -        0      0    0  0    0        0        0       0             4        4
worker.c                                 2215   -        34     1    0  33   39       0        0       0             15       2
compiler/compile-compute.inc             775    y        12     4    0  8    414      0        0       0             5        14
compiler/compile-geometry.inc            882    y        12     3    0  9    201      0        0       0             25       19
compiler/compile.c                       5467   y        87     26   0  61   1215     0        0       0             66       59
compiler/eu.c                            2747   y        70     25   0  45   747      0        1       0             18       46
display/aux.c                            957    y        21     10   0  11   471      0        0       0             8        13
display/clock.c                          4076   y        110    34   0  76   1474     0        1       0             23       20
display/color.c                          351    y        12     11   0  1    224      0        0       0             0        14
display/color.h                          32     -        0      0    0  0    0        0        0       0             1        1
display/ddi.c                            5002   y        111    107  0  4    4209     0        1       1             0        119
display/ddi.h                            52     -        0      0    0  0    0        0        0       0             1        1
display/diagnostics.c                    2681   y        77     13   0  64   557      0        7       0             37       23
display/diagnostics.h                    147    y        0      0    0  0    0        0        0       0             1        4
display/display.c                        3711   -        72     4    2  66   127      125      0       0             62       5
display/display.h                        147    -        0      0    0  0    0        0        0       0             1        3
display/dkl-phy.c                        248    y        6      0    0  6    0        0        0       0             10       2
display/dmc.c                            1235   y        32     1    0  31   13       0        0       0             31       3
display/dp-ext-kern.c                    1000   -        25     7    1  17   234      90       0       0             4        11
display/dp-ext-kern.h                    64     -        0      0    0  0    0        0        0       0             1        1
display/dp-ext.c                         1551   -        34     23   0  11   869      0        2       1             70       34
display/dp-ext.h                         242    y        0      0    0  0    0        0        0       0             8        8
display/dp-internal.h                    1508   y        32     31   0  1    405      0        0       0             73       93
display/dp-sink.c                        3113   y        89     39   0  50   1353     0        0       0             16       69
display/dp-sink.h                        51     -        0      0    0  0    0        0        0       0             1        3
display/dp.c                             3287   y        92     92   0  0    2623     0        0       0             0        110
display/dp.h                             50     -        0      0    0  0    0        0        0       0             1        1
display/edid-read.c                      366    y        7      4    0  3    187      0        1       0             4        10
display/edid.c                           583    y        9      7    0  2    355      0        0       0             13       14
display/edid.h                           38     -        0      0    0  0    0        0        0       0             1        1
display/gen9-backlight.c                 500    y        12     0    0  12   0        0        0       0             2        2
display/gen9-ddc.c                       578    -        13     0    0  13   0        0        0       0             11       1
display/gen9-link.c                      515    -        12     0    0  12   0        0        0       0             11       1
display/gen9-mode.c                      1378   -        29     0    0  29   0        0        14      1             19       2
display/gen9-mode.h                      206    -        0      0    0  0    0        0        0       0             14       1
display/gen9-modeset.c                   680    -        20     0    0  20   0        0        0       0             10       1
display/gen9-outputs.c                   675    -        15     0    0  15   0        0        1       0             6        1
display/gen9-outputs.h                   138    -        0      0    0  0    0        0        0       0             5        1
display/gmbus.c                          1029   y        23     9    0  14   342      0        0       0             3        12
display/gmbus.h                          24     -        0      0    0  0    0        0        0       0             1        1
display/hdmi-mode.c                      285    y        6      4    0  2    159      0        0       0             0        8
display/hdmi-mode.h                      33     -        0      0    0  0    0        0        0       0             1        1
display/hdmi.c                           538    y        15     5    0  10   202      0        0       0             2        6
display/hotplug-internal.h               1474   y        31     30   0  1    338      0        0       0             85       88
display/hotplug.c                        3823   y        99     42   0  57   1993     0        0       0             17       50
display/hotplug.h                        93     y        0      0    0  0    0        0        0       0             1        4
display/internal.h                       3961   y        3      0    0  3    0        0        0       0             174      45
display/interrupts.c                     1347   -        34     2    0  32   39       0        0       0             72       3
display/modeset-internal.h               3717   y        63     54   0  9    780      0        0       0             325      130
display/modeset.c                        4410   y        88     12   0  76   946      0        0       0             29       32
display/modeset.h                        148    y        0      0    0  0    0        0        0       0             1        3
display/opregion-internal.h              665    y        12     12   0  0    135      0        0       0             64       36
display/opregion.c                       2595   y        79     34   0  45   1052     0        0       0             17       39
display/opregion.h                       73     -        0      0    0  0    0        0        0       0             1        3
display/panel-backlight.c                1346   y        40     37   0  3    983      0        0       0             0        50
display/panel-backlight.h                51     -        0      0    0  0    0        0        0       0             1        2
display/panel.c                          2112   y        56     40   0  16   1461     0        0       0             6        25
display/phy.c                            1030   y        36     13   0  23   333      0        1       0             26       4
display/pipe.c                           3142   y        74     74   0  0    2558     0        0       0             16       92
display/pipe.h                           96     -        0      0    0  0    0        0        0       0             1        2
display/plane.c                          1632   y        44     43   0  1    1267     0        0       0             0        52
display/plane.h                          36     -        0      0    0  0    0        0        0       0             1        1
display/power.c                          3447   y        90     16   0  74   559      0        2       1             86       9
display/present.c                        2110   -        38     1    0  37   16       0        0       0             6        1
display/present.h                        112    -        0      0    0  0    0        0        0       0             1        1
display/state.c                          1767   y        39     11   0  28   510      0        2       2             5        22
display/state.h                          80     y        0      0    0  0    0        0        0       0             1        3
display/takeover-internal.h              767    y        8      7    0  1    69       0        0       0             103      37
display/takeover.c                       6672   y        135    37   0  98   1875     0        0       0             158      25
display/takeover.h                       102    y        0      0    0  0    0        0        0       0             1        1
display/tc.c                             1590   y        47     2    0  45   55       0        0       0             35       5
display/tc.h                             218    y        0      0    0  0    0        0        0       0             5        1
display/vblank.c                         850    y        27     19   0  8    449      0        0       0             0        24
display/vblank.h                         69     -        0      0    0  0    0        0        0       0             1        1
display/vbt-parse.h                      76     -        0      0    0  0    0        0        0       0             1        5
display/vbt.c                            4978   y        131    25   0  106  1146     0        0       0             17       35
display/vbt.h                            791    y        16     8    0  8    91       0        0       0             102      40
display/watermark-internal.h             244    y        4      4    0  0    39       0        0       0             26       12
display/watermark.c                      4491   y        107    36   0  71   1967     0        0       0             20       22
display/watermark.h                      84     -        0      0    0  0    0        0        0       0             1        3
intel/bits.h                             29     -        0      0    0  0    0        0        0       0             6        3
intel/clock.h                            244    y        0      0    0  0    0        0        6       4             1        4
intel/commands.h                         264    y        0      0    0  0    0        0        0       0             77       7
intel/connector.h                        112    y        0      0    0  0    0        0        0       0             4        7
intel/ddi.h                              111    y        0      0    0  0    0        0        0       0             1        2
intel/display-regs-gen9-modeset.h        366    -        0      0    0  0    0        0        0       0             218      1
intel/display-regs-gen9.h                233    -        0      0    0  0    0        0        0       0             107      1
intel/dkl.h                              233    y        0      0    0  0    0        0        0       0             104      6
intel/dp.h                               2202   y        3      1    0  2    6        0        0       0             619      28
intel/engine-table-gen9.inc              58     -        0      0    0  0    0        0        0       0             0        3
intel/engine-table.inc                   59     -        0      0    0  0    0        0        0       0             0        3
intel/eu-encoding-gen12.h                428    y        0      0    0  0    0        0        0       0             191      58
intel/eu-encoding-gen9.h                 249    y        0      0    0  0    0        0        0       0             129      19
intel/fixed.h                            162    y        0      0    0  0    0        0        0       0             2        2
intel/forcewake-ranges-gen9.inc          78     -        0      0    0  0    0        0        0       0             0        1
intel/forcewake-ranges.inc               82     y        0      0    0  0    0        0        0       0             0        1
intel/fourcc.h                           1584   -        1      0    0  1    0        0        0       0             286      1
intel/genxml-gen9.h                      200    y        0      0    0  0    0        0        0       0             42       6
intel/genxml-video.h                     309    y        0      0    0  0    0        0        0       0             113      2
intel/genxml.h                           883    y        0      0    0  0    0        0        0       0             358      34
intel/gt-power.h                         130    y        0      0    0  0    0        0        0       0             47       4
intel/gt-regs.h                          719    y        0      0    0  0    0        0        0       0             252      8
intel/hotplug-types.h                    79     y        0      0    0  0    0        0        0       0             1        2
intel/hotplug.h                          206    y        0      0    0  0    0        0        0       0             45       19
intel/lrc-offsets.h                      357    y        0      0    0  0    0        0        4       4             7        4
intel/mcr-ranges.inc                     48     -        0      0    0  0    0        0        0       0             0        2
intel/mocs-table-gen9.inc                51     -        0      0    0  0    0        0        0       0             0        2
intel/mocs-table.inc                     108    -        0      0    0  0    0        0        0       0             0        3
intel/mocs.h                             148    y        0      0    0  0    0        0        0       0             49       8
intel/mreg.h                             655    y        0      0    0  0    0        0        0       0             384      43
intel/null-state-gen9.inc                1005   y        0      0    0  0    0        0        0       0             0        2
intel/opregion.h                         350    y        0      0    0  0    0        0        0       0             106      10
intel/pci-ids.h                          213    -        0      0    0  0    0        0        0       0             12       4
intel/phy.h                              412    y        0      0    0  0    0        0        16      2             1        9
intel/plane.h                            522    y        0      0    0  0    0        0        0       0             286      13
intel/power-set.h                        41     y        0      0    0  0    0        0        0       0             2        2
intel/power.h                            138    y        0      0    0  0    0        0        0       0             4        7
intel/psr.h                              69     y        0      0    0  0    0        0        0       0             22       2
intel/ref-inlines.h                      76     y        3      0    0  3    0        0        0       0             1        2
intel/ref.h                              547    y        0      0    0  0    0        0        0       0             92       13
intel/trans.h                            465    y        0      0    0  0    0        0        0       0             329      7
intel/vbt-defs.h                         1289   y        0      0    0  0    0        0        14      0             166      8
intel/vbt.h                              521    y        0      0    0  0    0        0        0       0             25       8
intel/wm-dbuf.h                          307    y        0      0    0  0    0        0        2       0             1        7
intel/wm.h                               123    y        0      0    0  0    0        0        0       0             1        10
intel/workarounds.h                      184    y        0      0    0  0    0        0        0       0             108      4
render/blit.h                            41     y        0      0    0  0    0        0        0       0             1        1
render/compute.c                         703    y        10     1    0  9    22       0        0       0             8        2
render/draw.c                            1277   -        22     0    0  22   0        0        0       0             4        1
render/heap.h                            197    y        0      0    0  0    0        0        0       0             51       3
render/image.c                           1212   -        27     1    0  26   168      0        0       0             9        7
render/state.c                           4055   y        65     8    0  57   537      0        10      1             28       15
render/state.h                           235    y        0      0    0  0    0        0        0       0             1        1
render/transport.c                       362    y        8      0    0  8    0        0        0       0             9        0
render/transport.h                       37     y        0      0    0  0    0        0        0       0             1        0
tests/contracts/mmio_contract_test.c     355    -        8      0    0  8    0        0        0       0             0        0
tests/contracts/pci_contract_test.c      317    -        7      0    0  7    0        0        0       0             0        1
tests/display/aux.c                      889    -        15     3    0  12   176      0        1       1             21       27
tests/display/dp-fake-hw.c               1030   -        31     1    0  30   34       0        0       0             22       1
tests/display/dp-fixture-latitude5330.h  121    -        0      0    0  0    0        0        4       0             1        1
tests/display/edp-ktest.c                1134   y        26     4    0  22   153      0        0       0             6        18
tests/display/edp-ktest.h                58     y        0      0    0  0    0        0        0       0             1        1
tests/display/edp-sync-ktest.c           849    -        19     0    0  19   0        0        0       0             2        1
tests/display/hdmi-output.c              1047   -        13     1    2  10   43       81       0       0             12       2
tests/display/host-dp-test.c             797    -        16     0    1  15   0        40       0       0             0        1
tests/display/host-gen9-mode-test.c      356    -        9      0    0  9    0        0        1       0             0        1
tests/display/host-lcd-modeset-test.c    1825   -        25     0    0  25   0        0        1       0             2        11
tests/display/host-lcd-test.c            592    -        8      3    0  5    185      0        1       1             1        29
tests/display/hpd-ktest.c                835    -        16     0    0  16   0        0        0       0             19       2
tests/display/hpd-model.c                307    -        6      1    0  5    28       0        0       0             10       3
tests/display/hpd-model.h                62     -        0      0    0  0    0        0        0       0             1        1
tests/display/lcd-fake-hw.c              1402   -        44     3    2  39   75       49       1       0             33       3
tests/display/lcd-fake-hw.h              184    y        0      0    0  0    0        0        0       0             4        2
tests/display/lcd-modeset-ktest.c        673    -        10     0    0  10   0        0        1       0             5        3
tests/display/lcd-run.c                  1205   -        22     1    0  21   12       0        1       0             18       2
tests/display/opregion-ktest.c           1239   y        23     0    0  23   0        0        0       0             38       2
tests/execution/eu-test.c                2572   y        50     0    0  50   0        0        5       1             27       2
tests/execution/ktest-display-probe.c    3078   -        73     0    3  70   0        155      0       0             16       0
tests/execution/ktest-display.c          3452   -        74     0    3  71   0        143      1       0             48       0
tests/execution/ktest-gt.c               4584   -        81     0    1  80   0        78       4       0             29       1
tests/execution/ktest-sync.c             1575   -        43     0    1  42   0        56       0       0             15       0
tests/fixtures/draw-fixture.c            1889   y        43     1    0  42   11       0        5       2             25       7
tests/fixtures/draw-fixture.h            132    y        0      0    0  0    0        0        0       0             28       1
tests/fixtures/tex-fixture-fhd-gen.inc   114    y        0      0    0  0    0        0        5       2             32       6
tests/fixtures/tex-fixture-gen.inc       99     y        0      0    0  0    0        0        4       1             26       5
tests/fixtures/vkref-generated.inc       106    y        0      0    0  0    0        0        8       0             16       3
```
