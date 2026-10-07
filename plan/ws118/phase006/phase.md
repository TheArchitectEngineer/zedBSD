<!-- awesome-plan project=zedbsd record=ws118-p006 -->
# ws118-p006: Tiger Lake の DPLL と firmware の表示の引き継ぎ（5320 で Keiland を表示する）、世代に依らない fallback

Status: in-progress（q846-i01、P3、2026-10-07）
Disposition: normal
Parent: [WS118](../ws.md)
Queue: q846
依存: なし（実機 5320 は zedBSD で稼働中、`ssh kei@10.0.30.5`（password kei）、2026-10-07 ユーザー）
目安: 4〜8h（実機の確認はユーザーの再起動・書き込みが要る時は Q1 経由）

## 由来

2026-10-07 ユーザー:「サブエージェントP3を立てて、5320のDPLLに対応してほしいです。できれば世代やバリエーションにかかわらず表示できるようにフォールバックも実装してほしいですが、難しければいいです。」

## Q1 の調査（2026-10-07、実機の log、`tests/k5320-20261007-*.log`）

- Keiland（greeter・kei の session）は 1366x768 の output を開き、最初の frame の `acquire`/`submit` が `VK_ERROR_DEVICE_LOST`（-4）→ `KWL FAILED site=compose_draw errno=5`。sessiond が 6 回で console に戻す。
- kernel: firmware は eDP（DDI A、pipe A）を **640x480（PIPESRC 0x027f01df、panel fitter で 1366x768）** で点けている。`P5a objects: ... dplls=0(mgr=0)`（TGL の DPLL の管理が空）。
  takeover の readout `pipe 0 transcoder 0 DPLL-1 port 0, mode 1366x768 0 kHz`。停止で `pipe_off wait timed out`・`Timeout waiting for DDI BUF A to get idle`、
  その後 TRANSCONF=0x40000024（enable は落ち、状態の bit 30 が残る）、DPCLKA_CFGCR0 0x01e17800→0x01e17c00、DPLL0_ENABLE 0xcc000000→0。
  takeover は rc=0 を返すが preflight が `the display is not idle` → `resident display: not started` → 以降 `presentation fails from now on`。
- 未移植の step: `skl_scaler_get_config`（pfit の readout）など（LCD-B UNRESOLVED の列）。

## 範囲

1. TGL（display version 12）の DPLL の管理（combo PHY の DPLL0/1、必要なら TBT/TC は対象外で可）を Linux の `intel_dpll_mgr.c`（tgl_pll_mgr）に倣って埋め、readout が firmware の PLL を正しく結ぶ。
2. takeover の停止（`crtc_disable_noatomic` 相当）を TGL で正しい順にする: plane → pfit/scaler → pipe off の待ち → DDI・transcoder → clock gate → PLL。pfit の readout（scaler）を移植する。
3. 5320 で Keiland が 1366x768 で表示される（実機の証拠: SSH で log、目視はユーザー）。
4. （できれば）**fallback**: 世代・variation に依らず、modeset が使えない・失敗した時に firmware が点けた pipe をそのまま使い、plane だけを自分の buffer に向ける（PLANE_SURF/STRIDE/CTL、pipe の解像度のまま、pfit も保持）など、表示が出る経路。既定の経路が成功する機械（5330）の挙動を変えない。難しければ設計と見積もりを残して Q1 に返す。

## 受け入れ

- build（warning 0）、変えた所の host 試験（短い物）。
- 5320 実機: greeter/desktop が eDP に出る（kernel.log・greeter.log で present が進む、目視はユーザー）。
- 5330 で回帰しない（T1 に依頼。5330 の起動はユーザー）。
- fallback を実装したなら、その経路を強制する option か試験で 1 回は通す。

## 規則

- HAL の API は変えない。toolchain は変えない。rm は打たない（Q1 に path を送る）。security の判定で止まったら止まって Q1 に返す。
- 実機 5320 への image・kernel の入れ替えと再起動は、方法を決めたら Q1 に送る（ユーザーの立会いが要るなら Q1 が聞く）。
