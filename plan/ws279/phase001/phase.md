<!-- awesome-plan project=zedbsd record=ws279-p001 -->

# ws279-p001 Mesa と Linux の V3D 7.1・HVS6 の初期化と submit の手順の調べ

- 親: [ws279](../ws.md)、Queue: q1544 / q1544-i01（P2、2026-10-21）
- Status: in-progress（設計の第 1 版を書いた。Q1 とユーザーの判定待ち。Fable の review は使わない、2026-10-21 ユーザー「Pi5の対応は、Fableによるレビューは不要です。Opus 5.5で対応しましょう。」）
- 依頼の元: 2026-10-21 ユーザー「GPUは別途Mesaの初期化手順を正確にまとめてから実装しましょう。」

## 目的と受け入れの条件

Mesa（v3dv・common・cle・qpu・compiler）と Linux（v3d・vc4・pmdomain・firmware、Raspberry Pi の tree）を調べ、[design.md](../design.md) に次を書く。code は書かない。

1. V3D 7.1 の識別、clock・電源・reset、SMS、MMU、cache、割り込み（design §1）。
2. bin・render の job（4.2 との packet の差）、TFU、CSD、完了の待ち、hang と reset（§2）。
3. QPU の命令・register・signal・op・payload・schedule の決まりの 7.1 の差と、compiler に要る変更（§3）。
4. HVS6 の plane・dlist・UPM・scanout・vblank、firmware の framebuffer からの引き継ぎ、zero copy の条件（§4）。
5. Pi 4 の driver との共有・分離の表、Phase の分け方と見込み（LW）、試験の方法（§5〜§7）。
出典（file と行・commit）を書き、文は写さない。調べた事と未確認の事を分ける（§8）。

## 記録（2026-10-21 P2）

- 資料（design §0）: Linux v6.19（05f7e89ab973）の v3d・vc4・bcm2835-power・clk-raspberrypi、mainline の bcm2712.dtsi、Raspberry Pi の rpi-6.12.y（取得時の先頭 58b28eeef87a）の bcm2712-iommu.c・vc4_firmware_kms.c・raspberrypi-firmware.h・v3d_drv.c、Mesa 25.3.6（06f9e28304d5）の src/broadcom、firmware の Pi 5 の dtb 2 つ（C1・D0、`dtc` で戻して V3D・HVS の node が同じ事を確かめた）、zedBSD の Pi 4 の driver（main 2026fac56）。Linux と Mesa の tree は WS141 の作業の tree（`plan/ws141/temp/`、commit しない）、Raspberry Pi の file と mainline の dtsi は scratchpad に取得（SHA-256 は design §0）。
- 結論の要点:
  - 電源は PM + 0x304 の bit 6（ASB 無し）。7.1 は SMS の idle と reset が要る。IRQ は hub（SPI 250）と core（SPI 249）の 2 本。
  - MMU の形・CL の register・cache の操作は 4.2 と同じ。TFU（0x700〜、IOC が新しい）・CSD（0x930〜、CFG7）・CSDDONE の bit・GMPV の場所が違う。
  - CL の packet: Binning と Rendering Mode に tile の大きさを明示、Color の packet が RT ごとの Part1〜3（tile buffer の stride と base を driver が計算）、clear・cfg bits・blend・clipper（1/64）・shader state record・texture state・TMU cfg が変わる。
  - D0（ユーザーの 2 GB 版）は V3D 7.1.10: draw index の形の shader state record、CSD の batch の数は −1 しない、R/B swap の bit あり。HVS は D0 の register の配置（channel の block が 0x100 から 0x40 ずつ）。
  - QPU: accumulator 無し、add と mul が別々の 2 つの読み口、small immediate は 4 か所のどれか 1 つ、signal と op の表が別、SFU は add の op（latency 2）、ldunif・ldvary は rf0 に暗黙に書く、fragment の W は rf3、thrsw/thrend の決まりが違う → scheduler・register の割り当て・encoder は 7.1 用を新しく作る。
  - HVS6: 40 bit の address、plane ごとに UPM、dlist の memory は窓の 0x4000 から（先頭 32 word は firmware）、LPTRS で切り替え DL で取り込みを確かめる、vblank は pixelvalve の IRQ。IOMMU4 は 40 GiB より下を素通しにする設定が Linux の既定（firmware の設定は未確認）。
- Phase の案（design §6）: p002 骨格（4〜6 LW）、p003 job の submit・TFU・CSD・reset（6〜8）、p004 QPU 7.1 の compiler（10〜14、host だけで並べられる）、p005 描画の経路（8〜10）、p006 HVS6（8〜10、V3D と並べられる）、p007 Keiland の login（4〜6、WS278 p004 の USB keyboard に依存）、p008 速さ（4〜6）、p009 規約と license の監査（3〜4）。合計 47〜64 LW。
- ユーザーの判断（design §9）: D1 配置（推し: bcm2711 の directory に版の file を足す）、D2 display の道（推し: HVS6 を直接、firmware KMS ではなく）、D3 まず D0 だけで受け入れ、D4 Pi 5 の video の hardware decode は範囲の外。
- HAL の API（hal.h）の変更: 不要の見込み（GPU の driver は kernel の driver の中、firmware の mailbox は既存の口）。
- 未実施: 実機・QEMU の観測は無い（design §8 の U1〜U10 は実機で確かめる）。Fable の design-reviewer は使わない（ユーザーの指示）。

## 再開の条件

ユーザーの D1〜D4 の判断と、WS278 p002 の実機の起動の結果（Pi 5 で kernel が動く事）。その後に p002 を Queue に入れる。p004（compiler）は host だけで進められるので、WS278 の実機の結果を待たずに始められる。
