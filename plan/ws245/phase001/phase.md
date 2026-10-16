<!-- awesome-plan project=zedbsd record=ws245-p001 -->

# ws245-p001: Gen12 系（display 12・13）の display の書き直しの設計

Status: cleared（2026-10-16 Q1: design.md 改訂 6（0eb279a29、main の merge）、レビュー 1〜5 を全件反映、§7 #23〜#34 は既定の案（ユーザーが実装を指示）。改訂 6 のレビューは回していない。p047 の mmio.c の記録は GT と共有の hot path（off の時は load と分岐 1 回）で、今 GT の担当はいない）
Parent: [WS245](../ws.md)
Approval: 2026-10-15 ユーザー「同時にサブエージェントで、i915のリライトをやります。WSがあると思います。」

## 範囲

ws.md の p001 の行のとおり: Intel の公開の PRM（Gen12・ADL-P）と hardware の振る舞いから、display の model（pipe・transcoder・DDI・PHY（combo・Dekel/TC）・PLL・plane・watermark・power well・DMC・AUX・GMBUS・VBT・opregion）、外向きの口（Gen9 と共有の部分を保つ）、データの表の引用の方法（Value(s) referenced:）、書き直しの順序と Phase の分割、試験の道（host と 5330）、LW の見積もりを design.md に書く。design-reviewer を通す。**code は書かない。** 今の Linux 由来の code は「何をするか（外から見える振る舞い・register の列）」を知るためだけに読み、構造・名前・制御の流れを設計に持ち込まない。

## 結果

- **q991 P2（提出 6bccaef57、Q1 が commit の実在と行数を確かめて merge）**: [design.md](../design.md) 522 行。接頭辞 `xd`、新旧を並べ p008 で切り替え（`i915.display=old` を逃げ道）、p011 で旧を削除。Phase p002〜p012、36 LW（±40%）。PRM は ADL-P の版が公開されておらず、TGL・ACM の PRM と 5330 の dump から。
- design-reviewer は担当が起動したが結果が届く前に終わった（design.md 第 8 節）。レビューは未完。
- 記録の訂正（2026-10-15）: Q1 がこの後に「p001 cleared・D1〜D6・p002〜p005 cleared」と書いた記録は、実在しない commit に基づく誤りだったので取り消した（485ad5be6、ユーザー「誤りとして補正して、先に進めましょう」）。

## 改訂 2（2026-10-15、P1、ユーザーの指示で停止・通常のラップアップ）

- 提出 7cdf5879d（Q1 が実在と差分を確かめて merge、280c9fdba。基点が日付の修正より前だったので design.md の日付の 4 行を 2026-10-15 に直して衝突を解いた）。design.md 678 行（改訂前 522）。
- ユーザーの決定 1〜4 を反映: 口 `xd.h` と `struct i915_xd_ops`、旧 code は `old-xd.c`（薄い adapter）、boot の語 `i915.display=xd|old`、p012 で旧を削除。display 12 の分岐は持たない（5320 は bootfb）〔2026-10-15 訂正: 誤り。display 12 は実機で動作確認済みで、xd に含める。設計は次の改訂〕。手順の第一の出典は Linux v6.8.12、PRM は補助、§5.4 を guardrail の形に。食い違いは Linux を優先。
- design-review-1.md の指摘: 直した 36・直さない 2（m1 PRM の再確認、m14 要約に細部が無い）。Q1 が §8 の表で数を確かめた。
- Phase p002〜p013、合計 51 LW（±40%）。module 25 → 19。
- §7 の残りの判断 #0〜#10（p009 の既定、HDMI は DVI mode、CDCLK を mode set で変えない、PSR off、storm 検出無し、OpRegion runtime 無し、VBT/OpRegion の layout の出典、5330 の VBT/OpRegion の bytes を fixture に置くか、旧 code の残し方、eDP 1.4 の rate 表、ktest-display の置き換え）。#7 は design-review.md の M13（5330 の VBT の bytes は 2026-10-14 にユーザーの判断で消した）と食い違うので、レビュー 2 の反映で扱う。
- 残り: design-review.md の反映（未着手）。→ 改訂 3 で済み（下）。

## 改訂 3・3a（2026-10-15、P1、q1200）

- 改訂 3（d87fe9599、main 260834101、875 行）: 対象の機種の方針（ws.md「対象の機種」）で本文を多機種に書き直した。表 P1（platform: TGL・RKL・ADL-S/RPL-S・ADL-P 系・ICL）と表 P2（platform ごとの手順の差）、`struct i915_xd_platform` の記述（§3.1）、Type-C の流れ A（ADL-P）と流れ B（TGL・ICL）、display 11 の Phase p014、Phase p002〜p015。design-review.md（レビュー 2、blocker 4・major 18・minor 16）の 38 件を §9 の表で扱った（直した 38、直さない 0。うち 9 件は改訂 2 で直っていた物の確認）。
- 3 回目のレビュー（design-reviewer、[design-review-3.md](../design-review-3.md)）: blocker 2・major 17・minor 16。全件を Linux v6.8.12 の source と今の code で確かめて改訂 3a（efbd30ff5、941 行）で直した（§10 の表、直した 35・直さない 0）。主な変更: 1 → 2 画面は live の遷移をやめ resident の relight（B1）、own の `output.c` の port の規則の置き換えを p010 に（B2）、流れ A の電源と順序（M1）、well の所属の表 W3（M2）、DMC の載せる間の well と MMIO の範囲（M3）、latency の 3 段の調整（M4）、crawl の条件（M5）、記述を platform × subplatform の 10 行に（M6）、V2 の TGL/ICL（M7）、run の契約に EAGAIN・acquired・RELIGHT（M8）、PCI ID は display だけの種類（M9）、Phase の依存（M10）、p009 の NONE（M11）、既定の切り替えは verified の行だけ（M12）、DPLL_SSC を消す（M13）、scenario の書き直し +4 LW（M14）、Gen9 の実機（M15）、p014 の抜け（M16）。
- Phase p002〜p015、合計 80 LW（±40 %。1 LW ≈ 15〜25 分）。実機の往復は別枠 6〜10 LW。
- §7 の判断 #0〜#22 をユーザーに聞く（ws.md の Resume point）。主な既定: #0 既定の切り替えは p011、#11 PRM は照合だけ、#13 p003 以降の merge はデモの後、#15 passthrough の scenario 7 つを xd に書き直して残す、#17 TGL の TC を p010 に、#18 RKL・ADL-S は今 i915 が付かない（聞く）、#20 GT は起こさない、#21 未確認の platform は opt-in、#22 Gen9 の機械（聞く）。
- 未実施: build・host 試験・QEMU・実機（設計の Phase なので対象外）。レビューの「確かめられなかった事」（pipe DMC の well、5320 の HDMI の port、試験の kernel の余裕）は §10 の末尾。

## 改訂 4（2026-10-15 夜、P1、q1209 / q1209-i01）

- 承認: 2026-10-15 夜 ユーザー「了解です。では合格にして進めましょう。続きはサブエージェントで実施しましょう。N=2で、AMD関連とi915関連で分けます。」（Q1: 「design.md の改訂 4。code は書かない」）。
- 方針の出典: ws.md「進め方の改め」（ユーザー「書き換えが一度に大幅すぎます」「各ステップで5330で動くかどうかをきちんとリグレッションテストしながら、ステップで書き換えていきます」「5330で動作しないものをコミットしないでください」「ベアメタルにしましょう」）、guardrail「i915 の display の書き換えは 5330 で動く段ごとに」、ws.md「§7 の回答」。
- 下書き 9d50d2727（Q1 が main に merge: 95f942c9d）。§6 を作り直した: 置き換えの方法の規則 R1〜R8（1 段 1 機能、旧 file は殻を残し中身だけ xd、置き換えた中身は同じ段で消す（影の比較の 2 段だけ次の段）、受け入れは host + build + 5330 の素の起動の回帰 → 合格後に main、boot の語 `i915.display=` と adapter は作らない、順は葉から根へ、継ぎ目は今の code の emit の hook と bind_ops）、段の表 p016〜p046（31 段、140 LW ±40 %、5330 の起動は最少 31 回・見込み約 40 回）、§6.3 の試験の道（passthrough を外す）、§6.6 の見積もりと聞く点、§6.7 の p002・p004 の部品の使い方と p006 の途中の差分（temp に保存）の当て直す段、§7 の #0〜#22 の反映と #23〜#29 の新しい質問、§11 の変更の一覧。改訂 3a の p003・p005〜p015 は cancel（ws.md の表）。
- 確かめた事実（2026-10-15、今の code）: 段の継ぎ目に使う口 `struct i915_lcd_emit`（`internal.h:2138`）と `drv_i915_lcd_kernel_bind_ops`（`modeset.c`）の実装の表（dpcd_read/write → aux.c、panel → panel.c、power_get/put → power.c、dbuf_slices_update、tc_*/dkl_*、vblank の event）、dp-sink.c の `aux->transfer`（1837・2235 行）、edid-read.c の `i915_i2c_transfer`、panel-backlight.c の PWM の register の部分（770〜855 行）、power.c の well の ops と domain の map、display.c の段（`i915_display_noirq_front`・`native_check`・`core_init`・`dmc_init`・`noirq_tail`・`nogem_front`・`setup_outputs`・`readout`・`driver_probe`）、modeset.c の `drv_i915_lcd_kernel_resident_run` の呼ぶ物、各旧 header の公開の関数の数（power 37、takeover 44、modeset 42、hotplug 27、tc 19、dp 18、clock 17 …）、旧 API の呼び手の file ごとの数。
- 作業ツリー: q1207 で止めた p006 の途中の未 commit の差分を `plan/ws245/temp/p006-partial-20261015.patch`（追跡 5 file の差分 184 行の追加）と `temp/p006-partial-20261015-untracked/`（未追跡の xd-clock.h・xd-power.c/.h・xd-tc.c/.h）に保存し、追跡 file を `git checkout --` で戻してから main 5ac66395e を fast-forward した。未追跡の 5 file は Q1 が temp との一致を確かめて消した。
- 未実施: build・host 試験・QEMU・実機（設計の Phase なので対象外）。
- レビュー 4: design-reviewer（結果は [design-review-4.md](../design-review-4.md)、blocker 3・major 16・minor 15。扱いは design.md §11.2）。

## 改訂 5（2026-10-15 夜、P1、q1209 / q1209-i02）

- 承認: q1209 と同じ（Q1: 「design.md 改訂 4 へのレビュー 4 の反映。code は書かない」）。
- ユーザーの判断（ws.md「レビュー 4 への判断」、クリック）: B3「register などの証拠で代える」、M7「常設できない」、M8「その都度自分で入れ直す」。
- レビュー 4 の 34 件（blocker 3・major 16・minor 15）を全部扱った（直した 34、直さない 0。design.md §11.2 の表。事実は今の code で確かめ直し、レビューと食い違った物は無し。追加: m13 の `aux.c` の内側の再試行は実在（`aux.c:444`・`:509`））。
- 主な変更（§11.3）: 継ぎ目を hardware に書く道に（B1）、p038 を p038・p048・p049・p050 の 4 段に、p039 を p039・p051、p040 を p040・p052 に分け、p047（回帰の証拠の道具: survey と `mmio.c` の書き込みの log）を最初の段に足した（B2・B3・M6・M12）。R9（外部出力の道をどの段でも壊さない、節目だけ一覧）と R10・表 R（共有の register の書き手: HIP_INDEX・DDI_BUF_CTL の所有 bit・DPCLKA・DC_STATE_DEBUG・DBUF_CTL・GMBUS0・PWR_WELL_CTL_AUX）。p016・p018・p021・p037 の継ぎ目の訂正（M1〜M3）、Dekel の窓は p040 まで旧の 1 つの lock（M4）、基準の起動と止まった時の扱い（M8・M9）、build の組と Tools の追従（M10）、§1.1・§3.1 を #18 に合わせ ICL の attach は #30 に（M13）。
- 段の表: **37 段、156 LW（±40 %）**。5330 の起動は段の kernel で最少 37 回、見込み約 50 回（最大 70〜85 回）。
- ユーザーに聞く点（§7、既定の案で進める）: #23〜#29 に加えて #30（ICL の attach の時期: 既定は p045 を host だけ、attach は WS252 の後）、#31（passthrough を使う master の Tools: 既定は残し、節目に T1 で 1 回）、#32（survey を ssh から任意の時に読む sysctl の口: 既定は作らない）、#33（display の start の guard: 既定は作らない）。
- 確かめた事実（2026-10-15 夜、今の code）: §11.2 の「確かめた事実」の列。build・host 試験・QEMU・実機は流していない（設計の Phase）。
- レビュー 5: design-reviewer（結果は [design-review-5.md](../design-review-5.md)、2026-10-15 23:58、改訂 5 = 8c4fe7791 に対して。**blocker 2・major 11・minor 15。反映は未着手**: 2026-10-15 夜 ユーザー「5時間あたりの使用量がリミットに届きそうなので、i915の方は、今の作業が終わったらラップアップして、AMDに集中しましょう。N=1にします。」→ Q1 の指示でラップアップ）。
- **残り（再開点、2026-10-16 00:03 記録）**: design-review-5.md の全件を design.md §11.4（新設）の表で扱う。先に直す物: B1（p047 の書き込みの log: 範囲 0x40000〜0xCFFFF が DPLL/PHY/Dekel（0x16xxxx）・HIP_INDEX（0x1010a0）を含まず、DMC の program・毎 frame の PLANE_SURF・IRQ の ack で klog の 512 KiB の ring が溢れる → 範囲を display の全 BAR の区画に広げ、DMC の program・PLANE_SURF の flip・IRQ の ack を除き、段の名前は xd-io の step で付け、`i915.debug=display` 無しの起動では survey だけ）、B2（survey の呼び方: `worker.c` に時刻つきの仕事の口が無い → picture up の直後に 1 回、2 回目は次の present の serve の中で frame counter の差を見る形に。「U10 の kernel でも survey」は不可能なので基準は p047 の kernel の 1 回目の起動）、M1（p018 で `vbt.h`・`vbt-defs.h` を消すと残る 7 file の build が壊れる、殻の関数の一覧の訂正）、M2（STRICT の検査は `output.h` の include で止まる、`takeover.c:4691` の notify_encoder、Gen9 の init_opregion）、M3〜M6（p033・p048・p049 の消す物と残る呼び手、p038 の継ぎ目に `modeset.c:2827` の `i915_modeset_prepare_state`、p048 の継ぎ目は `commit_enable` の内側、xd-hpd が p052 まで旧の tc/dp-ext を呼ぶ）、M7（HDMI の窓は p024 → p048 の 14 段、p031 の節目に HDMI を足す）、M8（pipe CRC: 最初の 2 つを捨てる、CRC_DONE の割り込みを止める、静止画で「進み」は無意味）、M9（p045 の行と §4.2 の device.c の行に「display だけの種類」「ID を足す」が残る: 直し忘れ）、M10（guardrail「開発の管理情報」: `i915: P<n>` の log 35 行、N0・N1 の名前の扱いはユーザーに聞く）、M11（Q1 の作業: ユーザーの 3 つの判断を master の decisions-log へ、guardrail:119 の「内蔵 LCD の表示」の読み替え、ws.md の手順 3 の `PICTURE UP` → `picture up` と boot の語・survey の手順）。minor は m の表のとおり（`drv_i915_xd_io_setup` → 実物は `drv_i915_xd_io_init`、「直の volatile 書き込み 15 箇所」は 0、§6.2 の「+14」と §6.6 の整合、表 R の `tc.c:924` は helper で書き込みは `:1008-1018`）。

## 改訂 6（2026-10-16、P1、q1218 / q1218-i01）

- 承認: q1218（2026-10-16 ユーザー「別なサブエージェントで、i915の実装変更のWSを進めてオーケーです。こちらはmainに取り込む前にベアメタルテストして、オーケーならmainに取り込んでいきます。」）。§7 の #23〜#33 は既定の案で進める（Q1 の指示）。
- design-review-5.md の 28 件（blocker 2・major 11・minor 15）を全部扱った（直した 28、直さない 0。design.md §11.4 の表、変更の一覧は §11.5）。事実は 2026-10-16 に main 49c578bd2 の code で確かめ直した。レビューと食い違った物: M1 の「`vbt.h` を 7 file が include」は実際は `vbt-parse.h`（6 file）で、`vbt.h` は `vbt.c` と `dp-internal.h` だけ。M11 の decisions-log と guardrail は Q1 が済ませていた。
- 主な変更: p047 を作り直した（survey は own の `present.c` の 2 箇所: window の最初の serve と 1 s 後の最初の flip。worker.c と modeset.c は変えない。書き込みの記録は `mmio.c` の中の固定長の ring で、display の MMIO の 5 区画、DMC の連続は畳み、割り込みの register は数だけ、窓は start → survey 1、出力は digest の 1 行と（`i915.debug=display` の時だけ）全部の列。pipe CRC は survey 1 で enable、survey 2 で 2 回読んで止める）。新 R11（開発の管理情報: 段で触る file の物はその段で消す）、p016 に宣言の移動と `takeover.c:4691`、p018 の殻は 10 関数、「消す旧 code」は呼び手が 0 になる関数だけ（殻 file へ）、readout は p050、p038 の継ぎ目に `i915_modeset_prepare_state` の check、p048 の継ぎ目は `commit_enable/_disable` と写し 4 つ、§3.4 に hook の例外と spinlock の規則、p031 の節目に HDMI、p045 と §4.2 の直し忘れ、表 R の 4 行の訂正と 1 行の追加、§7 #34（N0・N1 の改名と R11 の当て方、既定）。
- 段の表: **37 段、159 LW（±40 %）**（p047・p016・p048 が各 +1）。列の和は awk で確かめた。
- ws.md: 手順に 5（`i915.debug=display` の入れ方）と 6（survey の取り方）を足し、Phase の表の LW を直した。
- 未実施: build・host 試験・QEMU・実機（設計の改訂なので対象外）。design-reviewer の 6 回目は流していない（Q1 の判断）。
