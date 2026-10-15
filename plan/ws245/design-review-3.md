<!-- awesome-plan project=zedbsd record=ws245-p001-design-review-3 -->

# WS245 p001 の設計（design.md 改訂 3）の敵対的レビュー（3 回目）

2026-10-15 design-reviewer（P1 の worktree `/home/awe/zedBSD-worktrees/p1`、d87fe9599）。対象: [design.md](design.md)（改訂 3、875 行）。design.md と他の file は直していない。
根拠は `file:行`。無印の `design.md:行` は対象の文書、`src/` 以下は今の code（`display/` は `src/drivers/gpu/i915/display/`）、
`L:` は Linux v6.8.12 の `build/ws229-p001/linux/linux-6.8.12/drivers/gpu/drm/i915/`（読むだけ。値と分岐の照合だけに使った）。
PRM の text は `/home/awe/zedBSD-worktrees/p2/plan/ws245/temp/*.txt`（読むだけ）。build・host 試験・QEMU・実機は走らせていない。

分類: **blocker** = このまま p002 以降に進むと今動いている物が壊れるか、選んだ方式が成り立たない。**major** = 直さないと実装か受け入れで詰まる、または規則に反する。**minor** = 記述の誤り・小さな欠落。

件数: blocker 2、major 17、minor 16。

## blocker

### B1. 1 → 2 画面の DBUF/MBUS の「plane の再 program だけの遷移」は Linux に無い方式で、own の file の灯し直しの契約とも矛盾する

- design.md:320-323（「Linux の `intel_dbuf_pre_plane_update`/`post_plane_update` と同じ順 … resident の PLANE_BUF_CFG/WM を vblank に同期して書き …今の "lit again for two pipes" と同じ結果を plane の再 program だけで出す」）、:440（§3.3 の「2 画面」）、:537（契約の SPARED は「resident を 2 pipe の割り当てで灯し直した」）、:682（p008 (c) の「1 → 2 → 1 の遷移の register の列」）、:778（危険）。
- 事実:
  1. Linux v6.8.12 は MBUS の join が変わる時、全 pipe を modeset する（`L:display/skl_watermark.c:2614-2622`「TODO: Implement vblank synchronized MBUS joining changes」→ `intel_modeset_all_pipes_late(state, "MBUS joining change")`）。`intel_dbuf_mbus_update` にも同じ TODO（`skl_watermark.c:3493-3494`）。ADL-P の 1 画面（join）→ 2 画面（unjoin）は、Linux では resident の pipe を止めて灯し直す遷移で、「plane の再 program だけ」の手順は Linux に無い。決定 4（Linux を優先）の下で出典が無い。
  2. MBUS の join の変更では DBUF_CTL の DBUF_MIN_TRACKER_STATE_SERVICE も書き換える（join 3、非 join 1: `skl_watermark.c:3496-3512`）。設計の遷移の手順（:322）にも P2 の DBUF 行（:108）にも無い。
  3. 今の own の file は「灯し直し」を前提に組まれている: head の frame が `drv_i915_head_needs_relight`（`display/head.c:517-541`）で灯し直しを求め、present.c が window を抜けて `window.relight` で resident を `run_pipes` 込みで再度 run する（`display/present.c:2041-2050`、`:745-768`）。head の DBUF は `also_active_pipes`（`head.c:862`）で resident の pipe を数えて計算する。§4.2 は「own の file の構造は変えず、呼び出し側の行だけを直す」（design.md:513）なので、xd が live の遷移を持つと own の relight の道と二重になり、どちらが DBUF を書くか決まらない。
- 起こる事: p008 の host 試験 (c) は oracle（Linux の列）が無いまま自作の列を固定する。5330 で今動いている 2 画面（HDMI・USB-C、m3 の dump）を xd で灯す p010 で、join を解く瞬間の underrun か、relight と live 遷移の二重の書き込みで resident が乱れる（推測を含む: 実機の挙動は未確認）。
- 修正: live の遷移を設計から外し、Linux と今の own の file と同じ「resident を `run_pipes` で灯し直す」（= Linux の全 pipe の modeset に相当）だけにする。§2.10 の方針、§3.3 の「2 画面」、§4.2 の SPARED の説明、p008 (c)、§7 の危険を直し、MBUS_CTL の join/hashing と DBUF_MIN_TRACKER_STATE_SERVICE は resident の enable の列の中で書く（`skl_watermark.c:3496-3512` を Procedure/Values referenced）。

### B2. own の `output.c` の出力の規則が 5330 の配線に固定されていて、「5330 を特別扱いしない」多機種化が own の file の付け替えだけでは成り立たない

- design.md:513（own の file は呼び出し側の行だけを直す）、:373（GOP の出力の分類は今の `output.c:347-372` の規則を保つ。「対応しない出力（RKL・ADL-S の TC の port を含めて xd が駆動できない物）」）、:112（P2: RKL・ADL-S のデスクトップは GOP が HDMI/DP を灯す。「分類の規則は同じ」）、:686（p012: RKL・ADL-S の GOP の分類と takeover の列）、ws.md:31-35（5330 を特別扱いしない）。
- 事実: 今の own の `output.c` は port の番号を 5330 の配線で決め打ちしている: eDP = port A、HDMI は DDI B だけ（`display/output.c:584-589`「only DDI B's path (pipe B, DVI mode) is lit」）、DP は TC1〜TC4 だけ（`output.c:65-68` の `I915_OUTPUT_PORT_TC_COUNT 4`、`:251-258` の GOP の分類）。ADL-S には port B が無く外部は全部 TC1〜4（combo）、RKL の HDMI/DP は B・TC1・TC2（combo）、TGL の TC は TC1〜6（`L:display/intel_display_device.c:614-615, 634-635, 643-644`）。
- 起こる事: (1) RKL・ADL-S のデスクトップ（eDP 無し、GOP が TC1 等の HDMI/DP を灯す）は `output.c:360-364` で「not one this driver lights」になり、xd があっても display を使わない。p012 の host 試験は xd だけを流すので、この行き止まりを検出しない。(2) TGL の TC5/6、combo の DP、TC の HDMI も own の規則で拒まれる。(3) §2.14（:373）は RKL・ADL-S の TC を「駆動できない物」と書き、P2（:112）・p012（:686）と食い違う。
- 修正: own の `output.c` の port の規則（DDI B だけ、TC 4 つ、pipe B 固定）を platform の記述（§3.1 の port の一覧、`xd_platform` の「この port は駆動できるか」: :404）に置き換える仕事を Phase に入れる（p010 か p012。own の file の規則の変更として明記）。それをしないなら、RKL・ADL-S は「bootfb のまま」と §1.1・§7 #18・p012 に書き、ユーザーの「全部サポート」との差を §7 に判断として出す。§2.14 の「対応しない出力」の文を P2・p012 と揃える。

## major

### M1. Type-C 流れ A（ADL-P）の connect/disconnect の電源と順序が Linux と違い、今の code にある ADL-P の WA が抜けている

- design.md:282（TCCOLD の block = AUX USBC の well、TBT は AUX TBT の well）、:286（connect: 「port の power（DDI IO）を取る → TCCOLD を block → ready → 所有」）、:288（disconnect: 所有を返す → TCCOLD の block を外す）、:684（p010 (b) は「流れ A: 所有 → ready → AUX USBC の well → lane」で :286 と順が違う）。
- 事実:
  1. 「port の power」は `tc_port_power_domain` = `POWER_DOMAIN_PORT_DDI_LANES_TCn`（`L:display/intel_tc.c:246-252`）で、DDI IO の well ではない。ADL-P では LANES_TC1〜4 は PW_2 の domain（`L:display/intel_display_power_map.c:1247-1280`）。DDI_BUF_CTL の所有 bit の rmw は port の power の下でしか効かない（`intel_tc.c:822-834` の `assert_tc_port_power_enabled`）。5330 の 1 画面の eDP は PW_2 off（`plan/ws031/display-ref/regs-selected.txt:68`、PWR_WELL_CTL2 0xc03）。
  2. Linux の流れ A の connect は「port の power → 所有 → ready → TCCOLD の block → lane の確認 → port の power を返す」（`intel_tc.c:864-909`）。TCCOLD の block（legacy AUX の domain = AUX_USBCn）は PW_2・DC_off・AUX USBC の well を含む（`intel_display_power_map.c:1247-1270` の XELPD_DC_OFF_PORT_POWER_DOMAINS に AUX_USBC1〜4）。TBT の TCCOLD の domain は TC_COLD_OFF（`intel_tc.c:757-766`）で、ADL-P ではどの well にも属さない（TC_COLD_OFF を持つのは `tgl_pwdoms_tc_cold_off` だけ: `intel_display_power_map.c:880-893`）。
  3. disconnect は「port の power → TCCOLD の unblock → 所有を返す → port の power を返す」（`intel_tc.c:911-934`）で、:288 の順と逆。
  4. ADL-P の DP-alt/legacy は DDI_BUF_CTL の enable の前に DKL_PCS_DW5 の CORE_SOFTRESET を lane 0/1 で消す（`L:display/intel_ddi.c:3496-3504`、`:3592-3594` の `adlp_tbt_to_dp_alt_switch_wa`）。今の code もしている（`display/ddi.c:2716`）。設計の §2.3・§2.8・P2 に無い。
- 起こる事: 5330 の 1 画面で USB-C を挿すと、xd-tc が DDI IO の well だけで DDI_BUF_CTL の bit 6 を書き、PW_2 が落ちているので所有が取れず connect が abort する（今は動いている USB-C DP の退行）。TCCOLD の block を AUX USBC の well だけで持つと、TC が CONNECTED のまま DC off を返した時に DC5/6 に入り得る（§2.5 の「作業の間だけ DC off」と組み合わさる）。passthrough では USB-C を試せない（design.md:706）ので、p011 の既定の切り替えの直前のユーザーの確かめまで見つからない。
- 修正: §2.8 の connect/disconnect を流れごとに Linux の順で書き直す（A: LANES の domain の well（13 は PW_2）→ 所有 → ready → TCCOLD の block（PW_2・DC off・AUX USBC）→ lane → LANES を返す。disconnect は unblock → 所有を返す）。TBT の TCCOLD の block は ADL-P では「well 無し」。`adlp_tbt_to_dp_alt_switch_wa` を §2.3 の 7 と P2 に足す。p010 (b) の期待値を同じ順にする。

### M2. §2.5 の「どの well が要るか」の手書きの対応が Linux の電源の所属と合わず、host 試験はそれを検出できない

- design.md:218-220（pipe/DDI/AUX/VGA → well の規則。「DDI n → DDI IO の well + 外部の port なら PW_2（12 は PW_2 と PW_3）」「AUX ch → AUX の well」）、:216-217（POWER_DOMAIN の集合は持たない）、:680（p006 の試験）。
- 事実（`L:display/intel_display_power_map.c`）: ADL-P の DDI A・B は PW_1 の中（:1282-1287）で、DDI B（5330 の HDMI）に PW_2 は要らない。ADL-P の AUX USBC/TBT は PW_2 の domain（:1263-1270）。TGL の AUX USBC/TBT は PW_3（したがって PW_2）と TC_cold_off の両方の domain（:833-844、:880-893）。TGL の port B（combo）の lane は PW_2/PW_3 に属さない（:819-844 に LANES_B が無い）。RKL の TC1/TC2（combo）の lane と AUX USBC1/2 は PW_3（:1014-1025）。ADL-S の TC1〜4（combo）の lane は TGL の PW_3（`adls_power_wells` = tgl main: :998-1002）。
- 起こる事: 規則どおりに作ると、ADL-P で DDI B に不要な PW_2 を上げ（電力と DC の阻害）、AUX USBC に PW_2 を上げずに AUX が timeout し、TGL の TC の AUX で PW_3 と TC_cold_off を欠く。表 W1 の「機能」の列は自分で書く（:220）ので、host 試験の oracle も同じ誤りを持ち、ずれを検出しない。
- 修正: 「機能 → well の集合」を hardware の所属のデータとして Linux の `*_pwdoms_*` から写像の表にし（Values referenced を付ける。software の domain の抽象を持ち込むのではなく、どの register 群がどの well にあるかの事実として）、W1 の列と §2.5 の規則をその表から導く。host 試験はこの表と 5330 の `dbg-i915_power_domain_info.txt` で照合する。

### M3. DMC の載せ方で、pipe の well を上げずに pipe DMC の program を書く。main の MMIO の範囲も Linux と違う

- design.md:451（DMC の worker は DC off の refcount を 1 つ持つだけ）、:349（「main は 0x80000–0x8FFFF」）、:169（resume の載せ直し）。
- 事実: Linux は DMC の init から載せ終わるまで `POWER_DOMAIN_INIT`（全 well の domain に入っている: 例 `intel_display_power_map.c:1242-1245`、`:1278-1280`）を持つ（`L:display/intel_dmc.c:894-905`、`:980`、resume は `:1097`）。`intel_dmc_load_program` は全 dmc id（main と pipe A〜D）の program を書く（`intel_dmc.c:510-525`）。ADL-P の pipe DMC の base は 0x5F000（`intel_dmc_regs.h:24-29`）。header v3 の main の MMIO の範囲は 0x8F000–0x8FFFF（`intel_dmc_regs.h:75-76`、`intel_dmc.c:629-631`）で、0x80000–0x8FFFF は header v1 の範囲（`intel_dmc.c:626-628`）。
- 起こる事（推測を含む: pipe DMC の program storage が pipe の well の中にあるかは PRM で確かめていない）: 5330 の起動は 1 画面で PW_2/PW_B が off なので、pipe B の DMC の program と MMIO pair が書かれず、後で HDMI を pipe B に灯して PIPEDMC_CONTROL(B) を立てると、空の pipe DMC が走る。MMIO の範囲の検査が緩いと、main の pair に 0x80000–0x8EFFF の語を持つ壊れた package を受け入れる。
- 修正: §3.4 (d) と §2.12 に「載せる間は package に firmware のある pipe の well（13 は PW_A/PW_2/PW_B…）を上げ、DC off を持つ」と書き、resume の (10) も同じにする。§2.12 (4) の範囲を v3 main = 0x8F000–0x8FFFF、v1 = 0x80000–0x8FFFF に直す。p006 (c) に「載せる間の well」の検査を足す。

### M4. watermark の latency の調整を誤読している（+3/+2 µs は level 0 が 0 の時だけ）

- design.md:317（「display 12 以降は全 level に 3 µs、11 は 2 µs を足す（`adjust_wm_latency` の read latency）、level 0 が 0 なら WaWmMemoryReadLatency」）、:109（P2 の「latency の読みの加算 3 µs」）、:625（T6）。
- 事実: read latency は WaWmMemoryReadLatency そのもので、`wm[0] == 0` の時だけ全 level に足す（`L:display/skl_watermark.c:3315-3326`）。その前に「0 の level より上を全部 0 にして level の数を減らす」（:3305-3313）、後に 16 GB DIMM の WA で level 0 に +1（`dram_info.wm_lv_0_adjust_needed`: :3328-3335）。今の code も同じ（`display/takeover.c:1230-1264`）。
- 起こる事: 5330 で level 0 の latency が 0 でなければ、xd は全 level に 3 µs 多く見積もり、PLANE_WM の語が oracle（PLANE_WM0 0x80004010: design.md:682 (b)）とずれる。0 の level の切り捨てと DIMM の WA が無いと、DIMM 次第で underrun する。
- 修正: §2.10 と P2・T6 を Linux の 3 段（切り捨て → level 0 が 0 なら read latency → DIMM の WA）に直し、`xd-dbuf` の入力に DRAM の情報（DIMM の WA の要否）を足す（§3.6 の `init_opregion` の DRAM の読み）。

### M5. CDCLK の crawl の条件が違い、p006 の試験の期待値が Linux と食い違う

- design.md:253（「ADL-P は crawl」）、:680（p006 (e)「妥当でない値を最小にする列（13 は crawl、12 は relock）」）。
- 事実: crawl は「今の PLL が動いていて値が分かり（vco > 0 かつ unknown でない）、新しい vco > 0」の時だけ（`L:display/intel_cdclk.c:1914-1918`）。sanitize は vco を ~0（unknown）にする（`intel_cdclk.c:2098-2105`）ので、init の「妥当でない」→ 最小の設定は ADL-P でも relock（`icl_cdclk_pll_update`）。uninit（vco 0）と resume の init（PLL off）も relock。
- 起こる事: p006 の host 試験が「不明な PLL を crawl で書き換える」列を正しいとして固定し、xd-clock がそれに合わせて作られる。実機で PLL の状態が不明な時に crawl すると lock しない恐れがある（推測）。
- 修正: §2.6 の set の手順に Linux の条件を書き、p006 (e) を「妥当でない／uninit／PLL off からは 12・13 とも relock、動いている PLL の ratio の変更だけ 13 は crawl」に直す。

### M6. §3.1 の platform の記述（`struct i915_xd_platform` × 5）では表せない差がある（「kind で分岐しない」方針が成り立たない）

- design.md:388-396（記述の field と「× 5」「kind で分岐する所は無い」）、:592（offset の違う register は field か別名の define）。
- 事実（同じ display の版の中の差）:
  1. subplatform: TGL-UY の swing の表（`tgl_uy_combo_phy_trans_dp_hbr2`）と stepping の表（`L:intel_step.c:66-78`）、RPL-U の CDCLK の表（480 MHz を含む `rplu_cdclk_table`: `L:display/intel_cdclk.c:1334-1356`）、ADL-N・RPL-P・RPL-S の stepping の表（`intel_step.c:130-142`）、ICL の port F は ID 次第（`L:display/intel_display_device.c:963-964`）。1 つの platform に 1 行では選べない。
  2. abox の集合: MBUS の credit は TGL・ADL-S が abox 0・1・2、RKL・ICL が 0（`L:display/intel_display_power.c:1146-1173`、`intel_display_device.c:565, 627`）、BW_BUDDY は abox_mask（TGL・ADL-S は 1・2 だけで 0 は書かない、ADL-P は 0・1: `intel_display_power.c:1597-1639`、`intel_display_device.c:648`）。記述に abox の field が無い。
  3. DPLL の CFGCR0/1 の offset は ICL・TGL（ADL-P）・RKL・ADL-S で別（`L:display/intel_dpll_mgr.c:3667-3701`、`L:i915_reg.h:6078-6169`）、ADL-S の DPLL2/3 の enable は 0x46018/0x46030（`i915_reg.h:6039-6040`）。§5.1 の「offset の違う register」の列挙（:592）に無い。
  4. VBT の AUX channel の写像は 4 表（`L:display/intel_bios.c:3508-3570`）。記述は `vbt_dvo_map`・`vbt_ddc_map` だけ。
  5. RKL・ADL-S の HTI（HDPORT_STATE で PHY を除く: `L:display/intel_hti.c:12-29`、`intel_ddi.c:4871-4876`）。
  6. RKL は CMP（PCH_CNP）とも組む（`L:soc/intel_pch.c:95-103`）。その時の HPD の pin（`intel_ddi.c:4699-4709` の `rkl_hpd_pin` は PCH で分岐）、south の HPD の表（hpd_spt）、DDC の表（cnp: `intel_bios.c:2243-2245`）は ICP 系と別。記述の `pch_kind` は ICP/TGP/ADP だけ（:394）。
- 起こる事: 実装の時に記述の field が足りず、`kind` や ID での分岐が手順の中に戻る（方針の破れ）か、RPL-U・TGL-UY・RKL+CMP で誤った表を使う。
- 修正: 記述を「platform × subplatform（× PCH）」の行にし（行の数を決め直す）、abox の集合、PLL の register の offset の組、AUX の写像、HTI の有無、PCH ごとの south の表（CNP を含める）を field に足す。RKL+CMP を支えないならそう書いて §7 に出す。

### M7. 表 V2（DVO port → DDI）が TGL・ICL で誤っている

- design.md:360（「共通（TGL・ICL）: A/B/C → DDI A/B/C、D/E → DDI D/E、F/G/H/I → TC1..4」）、:678（p004 の合成 VBT の「TGL の DVO/DDC の値」）。
- 事実: Linux の共通の表は DVO の A〜I を `enum port` の A〜I に写し（`L:display/intel_bios.c:2321-2331`）、`PORT_TC1 = PORT_D`（`L:display/intel_display_limits.h:89`）。TGL では DVO D〜I → TC1〜TC6、ICL では port C〜F（DVO C〜F）が TC1〜4（MG）。「D/E → DDI D/E」は TGL・ICL のどちらにも無い。
- 起こる事: p004 の合成 VBT の fixture が誤った写像で作られ、試験は通るが、5320 の USB-C（TC）の child が見つからない（TC の port を新しく支える p010 の前提が崩れる）。
- 修正: V2 を platform ごとに Linux の 4 表どおりに書き直す（TGL: A/B/C → A/B/C、D〜I → TC1〜6。ICL: C〜F → TC1〜4）。M6 の 4 の AUX の写像の表（V4）も足す。

### M8. §4.2 の run の契約が present.c の再試行の規則に足りない

- design.md:536-537（`_screen_light` だけが EAGAIN、`_screen_run` は SERVED/UNPLUGGED/SPARED/FAILED）、:544（`resident_run_rep` は p013 で消える）。
- 事実: present.c は run の戻りの EAGAIN で link を下げて再 run する（`display/present.c:2040-2042`）。two-pipe の run が失敗した時、「run が display に何かを渡したか」を `display->resident_run_rep.display_acquired` で読み（`present.c:2081`）、渡していなければ 1 pipe で即時に再試行する。spared は present.c 自身が計算する値で（`present.c:2053-2083`）、「resident を 2 pipe で灯し直した」は `window.relight` の道（`present.c:2044-2050`、head の `drv_i915_head_needs_relight`）で、設計の SPARED の説明（:537）は両者を混同している。retained の照会（`present.c:2058-2060`）も要る。
- 起こる事: p009 の xd で present.c の「呼び出し行だけの付け替え」が成り立たず、2 画面の失敗の時に resident を 1 pipe で灯し直さないか、display を誤って失敗にする（今の ws113-p011 の規則の退行）。
- 修正: `_screen_run` の戻りに EAGAIN と「display を取ったか（acquired）」を足し、`run_result.reason` から SPARED を外して relight（run_pipes を伴う再 run）と spared（present.c の判断）を分けて書く。p009 の host 試験に present.c の 3 つの分岐（EAGAIN、relight、未取得の 2 pipe の失敗）を入れる。

### M9. RKL・ADL-S（p002）と ICL（p014）の PCI ID を `device.c` に足すと、display 以外（GT・render）も動き出す。設計はその扱いを持たない

- design.md:397（`i915_product_listed` に RKL・ADL-S/RPL-S を足す）、:695（「ID を足すだけで旧 code の道には入れない（bootfb のまま）」）、:688（p014: ICL の ID を `device.c` に）。
- 事実: `device.c` の表は platform の種類（`I915_PLATFORM_GEN12`、graphics 12、display 12/13）を決め、GT の起動を含む driver 全体の道を選ぶ（`src/drivers/gpu/i915/device.c:767-785`）。RKL・ADL-S を display 12 として受けると、今の旧 code は TGL の well の表（`display/power.c:2242-2255`、RKL に無い PW_2 を含む）で動く。ICL は graphics 11 で、今の GT の code に Gen11 の道は無い（`device.c` の表に Gen11 の種類が無い）。
- 起こる事: p002 の時点で、RKL・ADL-S の機械が「何もしない（efifb/bootfb）」から「GT を起こし、旧 display の TGL の道で RKL に無い well を叩く」へ変わり得る（「bootfb のまま」にする仕組みが書かれていない）。p014 の ICL は GT の道が無く、ID を足すだけでは起動の道が決まらない。WS245 の範囲（display）を越える。
- 修正: 「display だけを持つ種類」（GT は起こさない、旧 display の道に入らない）を `device.c` に足す方法を p002 に書き、RKL・ADL-S・ICL の GT を起こすかは §7 の判断としてユーザーに出す（他の WS の範囲）。

### M10. Phase の依存が module の呼び出しと逆になっている所がある

- design.md:672（「p004・p005・p006 は p002 だけに依存し互いに独立」）、:679-681（p005・p006・p007 の範囲と試験）、:409-412（§3.2 の「呼ぶ物」）。
- 事実: §3.2 では xd-aux が xd-power（AUX の well、DC off）を呼ぶ（:407）が、xd-power は p006。xd-power の init は xd-ddi（combo PHY の init、p007）と xd-dbuf（slice・MBUS・BW_BUDDY、p008）を呼ぶ（:409）が、p006 (a) は「init の手順の register の列が §2.2 の順」を試験する。xd-clock は TC PLL の語の書き込みに xd-tc の Dekel の window を呼ぶ（:410）が、p006 (d) は m3 の TC PLL 2 の語を試験し、xd-tc は p010。xd-ddi の swing は xd-tc の表 T2 を呼ぶ（:411）が p007、xd-tc は p010。
- 起こる事: p005〜p007 の host 試験が後の Phase の module 無しでは書けず、stub を作って後で作り直すか、Phase の境界で詰まる。
- 修正: Dekel の window と表 T2（xd-tc の register の部分）を p006 か p007 に前倒しし、状態機械・HPD は p010 に残す。p006 (a) の init の全体の列は p008 の後（p008 か p009）の試験に移し、p006 は well・DMC・clock の単体にする。p005 は p006 に依存させるか、xd-power の口だけを p002 で決める。

### M11. p009 の opt-in の規則「GOP の出力が eDP でなければ display を使わない」は、T1 の passthrough（firmware が何も灯さない）と衝突する

- design.md:659（p009 の規則）、:683（p009 の T1: eDP の resident run、lcdb）、:706（passthrough の制約）。
- 事実: passthrough の guest には firmware の画面（GOP の点けた pipe）が無い（`plan/tools/hw5330/README.md:44`）。今の own の `output.c` は「firmware が何も灯していない」（`I915_GOP_NONE`）を内蔵 panel として扱う（`display/output.c:333-336`）。§2.14 の分類（:373）に NONE の行が無い。
- 起こる事: p009 の規則を字のとおり実装すると、passthrough の T1（`i915.display=xd`）は GOP が eDP でないので display を使わず、lcdb が観測できない。p009 の受け入れの T1 が成り立たない。
- 修正: p009 の規則を「GOP の出力が eDP か無し（NONE）なら xd で eDP を灯す、それ以外は GOP の絵を残す」とし、§2.14 の分類の表に NONE → eDP を足す。p009 の host 試験 (d) に NONE の場合を足す。

### M12. 既定の切り替え（p011）の条件が TGL を外し、未確認の platform も既定で xd に入る（退行の窓）

- design.md:685（p011:「既定の切り替えは p009・p010 の 5330 の実機の確かめ（ユーザー）が済んでから」）、:753（§7 #0 は「5330・5320 で確かめてから」）、:686（p012 は p011 の後）、:684（5320 の流れ B は「動かなくても p010 は cleared」）。
- 事実: TGL（5320）は今 eDP・backlight・suspend が動く platform（ws.md:27、design.md:572）。p011 の行は 5320 を条件に入れていない。RKL・ADL-S の host の網羅（p012）と ICL（p014）は p011 の後なので、p011 で既定を xd にした時点で、host 試験も実機も通っていない platform が xd の道に入る。
- 起こる事: 5320 で xd の TGL の道（流れ B の pcode の TCCOLD など、実機の実績が無い物）が既定で走り、今動く eDP の起動が壊れても p011 の受け入れでは止まらない。RKL・ADL-S・ICL の機械は「bootfb で表示」から「xd の takeover で黒画面」に変わり得る。
- 修正: p011 の条件を §7 #0 と同じ「5330・5320 の確かめ」に揃える。platform の記述に「実機で確かめたか」の flag を持たせ、確かめていない platform（RKL・ADL-S・ICL）は既定を old（＝今の bootfb）のまま `i915.display=xd` の opt-in にする（§7 に判断として出す）。TGL の流れ B は、5320 で確かめるまで既定では TC の connect をしない（hotplug の通知だけ）などの逃げ道を書く。

### M13. combo DPLL の手順に PRM の WA（DPLL_SSC）だけを根拠にした書き込みがある

- design.md:243（「DPLL_SSC（TGL の WA: CFGCR0 の SSC bit は効かず DPLL_SSC の sscen）」）、:60・:764（§0.2・§7 #11: 判断までは PRM の値は照合だけ、実装に入れる値は Linux から）。
- 事実: Linux v6.8.12 の combo PLL の enable（`L:display/intel_dpll_mgr.c:3862-3882`、`icl_dpll_write` :3667-3701）は DPLL_SSC を書かない（参照は non-SSC だけ: :3977-3981 の `icl_update_dpll_ref_clks`）。DPLL_SSC の WA は PRM の Vol 14（`p2/plan/ws245/temp/tgl-vol14-workarounds.txt:328`、`:1831-1832`）だけ。今の `display/clock.c` も DPLL_SSC を書かない（grep で該当無し）。
- 起こる事: 決定 4（Linux を優先）と §7 #11 の既定（PRM は照合だけ）に反し、今の 5330・5320 に無い register の書き込みが入る。SSC を使わない xd では意味も無い。
- 修正: §2.6 の手順から DPLL_SSC を消し、「SSC は使わない（Linux と今の code と同じ）」と書く。

### M14. T1 の passthrough の scenario を「xd の口に付け替えて残す」作業の大きさが見積もりに無い

- design.md:707（lcdb・hdmib・dual・dual_share・hdmi_hpd・hdmi_edid・aux を xd の口に付け替え。付け替えは p009（lcdb）と p010（他））、:728（§6.4）、:683-684（p009 7 LW・p010 8 LW）。
- 事実: これらの scenario の source は Linux 由来の header を直に include する（`src/drivers/gpu/i915/tests/display/lcd-run.c:29-36`: clock.h・diagnostics.h・modeset.h・panel-backlight.h・vbt-parse.h・watermark.h、`hdmi-output.c:27-35`: hdmi.h・hotplug.h・state.h など）。行数は lcd-run.c 1205、hdmi-output.c 1047、aux.c 889、hdmi-hotplug.c 453、lcd-flip.c 380（計 3974）。
- 起こる事: 「付け替え」は scenario の観測の部分の書き直しで、p009・p010 の LW に入っていない。旧 code（p010 まで既定）と xd の両方で同じ scenario を流す必要がある期間の扱い（二重化か xd だけか）も決まっていない。
- 修正: scenario の書き直しを独立の作業として LW を見積もり（p009・p010 に足すか、別の Phase）、旧と xd の両方で流す期間の扱いを §6.3 に書く。

### M15. Gen9（display 9、今動いている）への退行の窓が p002・p003 にあり、確かめの道が無い

- design.md:676（p002: `internal.h` の own の型の分離、`gen9-outputs.c`・`gen9-node.c`・`bootfb.c`・`device.c` への移設。受け入れは host 試験と Venus の QEMU だけ）、:503-505（§4.1 の Gen9 との共有）、:710-716（実機の一覧は 5330・5320 だけ）。
- 事実: display 9 は「実機で動作確認済み」の機能（ws.md:27、:33-34）。p002 は Gen9 の node が使う型と関数（`gen9-node.c` の `drv_i915_delayed_*`、`gen9-outputs.c` の `drv_i915_adjust_wm_latency`）を動かす。
- 起こる事: Gen9 の機械（SKL〜CML）の display が p002 で壊れても、host 試験と QEMU（i915 無し）では見つからない。
- 修正: p002・p003（と p013）の実機の一覧に Gen9 の機械（ユーザーの手元の Gen9 の機械。どれかは未確認）の起動・display を足す。

### M16. p014（display 11）の範囲に ICL の事実の抜けがある

- design.md:688（p014 の範囲）、:104（P2 の ICL の TC）、:230（「ICL は MG の同じ意味の register」の uc_health）、:345（DMC v1 は「1 entry、main だけ」）、:88（ICL の port C〜F）、:689（p015 は規約の見直しだけ）。
- 事実:
  1. ICL の combo の AUX の well は PORT_CL_DW12 の AUX lane enable と、eDP でない AUX A/B の Display WA #1178（ICL_AUX_ANAOVRD1）を伴う（`L:display/intel_display_power_well.c:413-436`）。
  2. ICL の display の WA（Wa_1409120013 の DPFC chicken、Wa_14010594013 の CHICKEN_DCPR_1 ICL_DELAY_PMRSP: `L:display/intel_display_wa.c:11-19`）が P2 の ICL の init（:102）に無い。
  3. TC の AUX の uc_health の待ちは display 12 以降だけで、ICL には無い（`intel_display_power_well.c:535-545`）。:230 の「ICL は MG の同じ意味の register」は Linux に無い。
  4. HDMI の disable で display 11 は TRANS_CLK_SEL を DDI_BUF_CTL の前に消す（`L:display/intel_ddi.c:3058-3064`）。§2.4 は 12 の順だけ。
  5. DMC の package v1 も stepping の entry を最大 20 持つ（`L:display/intel_dmc.c:140`）。「1 entry」ではない。
  6. ICL の port F は ID 次第（`intel_display_device.c:963-964`）。
  7. ICL の GT（graphics 11）の扱いは M9。
  8. p014 は p013（出典の監査）の後に新しい code を足すが、p015 は規約の見直しだけで、p014 の file の出典の監査（block ごとの referenced、file の先頭に出典が無い事）が再び走らない。
- 修正: p014 の範囲に 1〜6 を足し、3 の誤りを消す。p015 の受け入れに「p014 の file の出典の監査（§5.4 の grep）」を足す。

### M17. 記録の整合: ws.md が改訂 3 に追従していない。ユーザーの発言と code の事実の食い違いを聞かずに解釈している

- ws.md:6（Status は「改訂 2 … design-review.md は未反映」）、:11（Resume point は「p002〜p013、51 LW」）、:33（「今の Gen12 の code が動かしている全機種: display 12（Tiger Lake・Rocket Lake・Alder Lake-S・Raptor Lake-S/HX …）」）、design.md:71（「RKL・ADL-S・RPL-S の ID は今の code に無い」）、:771（§7 #18 は既定を「支える」）。
- 事実: AGENTS.md「記録の整合」は本体と写し（design.md の Phase の表 → ws.md の表・Resume point）を同じ commit で直す事、ユーザーの決定が既存の事実と食い違う時は記録の前に聞く事を求める。ws.md の Phase の表と Resume point は改訂 2 のまま（p002〜p013、51 LW）で、design.md は p002〜p015、76 LW。ユーザーの「すでに多数の機種で動作しており … Raptor Lake Refreshまですべてサポート」（ws.md:31）と、`device.c` に RKL・ADL-S・RPL-S の ID が無い事実（`device.c:767-785`）が食い違う（RPL-S と Refresh のデスクトップは今の code で i915 が付かない）。
- 起こる事: Q1 と担当が ws.md から古い Phase の表で Queue を組む。RKL・ADL-S の範囲（p012、M9 の GT、B2 の own の規則）が、ユーザーの意図（今の動作の維持か、新しい支持か）を確かめないまま 3 LW 以上の作業として入る。
- 修正: ws.md の Status・Resume point・Phase の表を改訂 3 に合わせる（担当の範囲なので P1 が直せる）。「RKL・ADL-S/RPL-S は今の code では i915 が付かない（ID が無い）。今動いているのは efifb/bootfb か」をユーザーに聞き、答えを §7 #18 の前提にする。

## minor

- m1. TGL の TCCOLD の要求は 3 回まで 1 ms 置きで、失敗は log だけで well は上がった扱い（`L:display/intel_display_power_well.c:1723-1762`）。design.md:232 と T8（:627）の「10 回」は誤り。失敗時の扱い（connect を止めるか）も書く。
- m2. FUSE_STATUS の待ちは Linux 1 ms（`intel_display_power_well.c:333-340`、`intel_de_wait_for_set(..., 1)`）。T8（:627）の「PG fuse 20 µs」は「PRM と Linux の大きい方」の規則（:221）と食い違う。
- m3. combo PHY の init の順: Linux は PHY_MISC の comp power down を消してから PORT_TX_DW8/PORT_PCS_DW1（display 12 以降）を書く（`L:display/intel_combo_phy.c:323-358`）。design.md:263 は逆。「済み」の判定も COMP_INIT だけでなく `icl_combo_phy_verify_state`（procmon の値・IREFGEN・CL_DW5 を含む: :215 以下）。
- m4. DP の enable の 7（design.md:188-189）: Linux は swing → lane の power の後に DP_TP_CTL の enable と DDI_BUF_CTL の enable を続けて書く（`L:display/intel_ddi.c:2678-2685`、`:3586-3602`）。DP_TP_CTL を swing の前に書く順は Linux と違う。idle pattern の DP_TP_STATUS の待ち（`intel_ddi.c:3645-3660`）も §2.3 に無い。
- m5. P2 の「TC PHY の register」の行（:105）は DP20BITMODE を消す事を 13 の列だけに書くが、Linux は display 12 以降の全部（`intel_ddi.c:1364-1365` の rmw は ADL-P の分岐の外）。§2.8（:290）の共通の記述と食い違う。
- m6. ADL-P の HW の SAGV の WM は PLANE_WM_SAGV と PLANE_WM_SAGV_TRANS の 2 つ（`L:display/skl_watermark.c:2401-2407`）。design.md:316・:318 は SAGV だけ。
- m7. VGA の well（12: PW_3、13: PW_2）の post_enable で VGA の MSR を書き直す（`L:display/intel_display_power_well.c:185-193` の `intel_vga_reset_io_mem`。今の `display/power.c:2437-2439` もしている）と、resume の VGA の再無効化（`L:i915_suspend.c:121`）が §2.5・§2.14 に無い。
- m8. PLANE_BUF_CFG の「その pipe の slice の組の先頭からの相対」（design.md:324）: Linux の基準は S1/S2 の組なら S1、S3/S4 なら S3（`L:display/skl_watermark.c:533-545` の `mbus_ddb_offset`、使う所 :653）で、TGL の A+B（A = S2）では A の start は 1024 から。「pipe の slice の組の先頭」と読むと TGL で 0 から書く誤りになる。
- m9. boot の語の追加は `src/kern/boot.c` の表と値の検査（:38-59、:295-305）だけでなく `include/kern/boot.h:132-133` の enum と `KERN_BOOT_PARAMETER_COUNT` も変える（§4.1・§6.1 は boot.c だけ）。名前の綴りの誤り（例 `i915.dispaly=`）は今の parser では record_unknown で黙って無視される（`boot.c:255-263`）ので、:661 の「不明な値の log」は値の誤りにしか効かない。p003〜p008 で `i915.display=xd` を指定した時の振る舞い（old で起動して log）も書く。
- m10. §8 の表に改訂 2 の Phase の番号が残る: B4（:791「p012 で消す」）、M10（:803「p012 に … Tools へ登録」）、M11（:804「p012 は p011」）、m10（:822「p012 で dump と比べる」）。改訂 3 では削除・登録・置き換えは p013。M18（:811）の「1 LW ≈ 1〜2 時間」も訂正後の換算（:691）と食い違う。表に「（改訂 2 の番号）」と書くか今の番号に直す。
- m11. platform の数の数え方が揃っていない: p002（:676）「表は 5 platform の行を持つ（ICL の行は p014 で足す）」、p012（:686）「5 platform の記述（ICL を除く 4 つ）」、§3.1（:396）「× 5」。
- m12. `include/drm/display/drm_dp.h`（D1 の出典: :356、:635）は MIT ではなく Keith Packard の HPND 型の許諾（同 file の先頭）。§0.2 の「全部 MIT」は i915 の file の話で、この file の扱い（guardrail は MIT・GPL を名指す）を書く。§5.2（:612）の `intel_gt_regs.h` の path は `gt/intel_gt_regs.h`。
- m13. master.md の Tools に登録された WS075 の H4 の harness は、`modeset.c`（書き直す Gen12 の file）が出す行の全文を正規表現で読む（`plan/ws075/tests/hdmi/h4-ctl.py:62-64` ↔ `display/modeset.c:3802`・`:3904`）。§4.4（:571）は接頭辞だけを保つと書き、全文は p011 で決めるので、それまでの xd でこの harness が buffer を見つけられない。この 2 行（と `present.c:1711` の flip の行）の書式を §4.4 で固定する。
- m14. `tests/display/host-kernel.c:27`・`host-native-decide-test.c:19-20` は `display/internal.h`（と `takeover.h`）を include する。§6.4（:729、:731）は「残す」だけで、p013 の `internal.h`・`takeover.h` の削除に合わせた付け替えを書いていない。
- m15. 表 W1 の列（:628: fuse の有無、VGA の有無、irq_pipe_mask、TC/TBT の AUX か、pcode の well か）は Linux の `i915_power_well_desc` の field（`has_fuses`・`has_vga`・`irq_pipe_mask`・`is_tc_tbt`・ops の種類）とほぼ 1 対 1。guardrail の「構造を写さない」の監査（p013）で疑われ得るので、列を PRM の "Functions Within Each Well" と M2 の所属の表から導いた事を記録に残す。
- m16. TC の VBT の legacy の印と live の食い違いの補正（`L:display/intel_tc.c:422-448` の `tc_port_fixup_legacy_flag`）と、TBT-alt の DP の TBT PLL（`L:display/intel_dpll_mgr.c:4032-4033`、`:3884-3900`）を xd が持つか持たないかが §2.8 に無い。

## 改訂 3 の §9 の「直した」の確かめ

| レビュー 2 | 判定 | 根拠 |
| --- | --- | --- |
| B1 ACM | 直っている | §0.1 の ACM の行（:42）、§2.6 の pipe count（:255） |
| B2 Type-C | 一部（流れ A/B は分かれた。流れ A の電源・順序・WA は M1） | `L:display/intel_tc.c:864-934` |
| B3 退行の窓 | 一部（p009・p010 は old が既定。p011 の条件と未確認の platform は M12） | design.md:685、:753 |
| B4 passthrough | 一部（道は入った。p009 の規則との衝突は M11、scenario の書き直しは M14） | `plan/tools/hw5330/README.md:44` |
| M2 DMC | 一部（header の番地は直った。pipe の well と main の範囲は M3） | `L:display/intel_dmc.c:492-532`、`intel_dmc_regs.h:72-76` |
| M5 契約 | 一部（表は入った。EAGAIN・acquired・relight と spared の混同は M8、B1） | `display/present.c:2040-2083` |
| M9 並行性 | 一部（(a)〜(d) は書かれた。(a) の DMC の worker の well は M3） | `L:display/intel_dmc.c:894-905` |
| M10 watermark | 一部（選択の規則は Linux になった。latency の調整の誤読は M4） | `L:display/skl_watermark.c:3305-3335` |
| m4（レビュー 2 の MBUS の join） | 設計が正しい（レビュー 2 の指摘が誤り: join の行は A 単独と B 単独だけ） | `L:display/skl_watermark.c:1259-1274` と `adlp_allowed_dbufs` |
| 他（M1・M3・M4・M6〜M8・M11〜M18、m1〜m3・m5〜m16） | 本文で確かめた範囲では直っている（M8 の VGA の post_enable は m7、m14 の文言の全文は m13） | — |

## 確かめられなかった事（推測を含む）

- M3 の「pipe DMC の program storage が pipe の power well の中にある」は、Linux が DMC の load の間 `POWER_DOMAIN_INIT`（全 well）を持つ事からの推測。PRM の DMC の章では確かめていない。
- M1 の「TCCOLD の block を AUX USBC の well だけで持つと DC5/6 に入り得る」は、Linux の domain の所属（AUX_USBC が DC_off の domain に入る）からの推測。5330 で確かめていない。
- B1 の「live の遷移で underrun する」は推測。Linux がその遷移を実装していない（TODO）事は確か。
- B2 の RKL・ADL-S の GOP が灯す port（TC1 等）は Linux の port の表からの推測で、実機の dump は無い。
- 5320（TGL）の HDMI が combo B か TC の legacy かは確かめていない（M7・B2 の影響の大きさに関わる）。
- kernel の大きさ: main の amd64 の vmunix の load の範囲は 13.44 MiB（`build/amd64/vmunix` を readelf で計算）で design.md:664 と合う。ただし 2026-10-05 の試験の build（`/home/awe/zedBSD-worktrees/p1/build/bug198-base-display/vmunix`）は 15.85 MiB、`bug198-base-none` は 15.57 MiB で、`vmunix.mk:217-219` の注記（「上限の 450 KiB 内」）とも合わせて、試験の組の余裕は design.md の見積もりより小さい可能性がある（config の違いは確かめていない）。
- ICL の DMC の `icl_dmc_ver1_09.bin` が header v1 か、ADL-P の v2.20 の package に pipe A/B の entry がある事は、firmware の bytes を読んで確かめていない（design.md の記述のまま）。
- 実行したのは grep・読み・`readelf -lW` だけ。build・host 試験・QEMU・実機は走らせていない。
