<!-- awesome-plan project=zedbsd record=ws050-p005 -->

# ws050-p005: i915 との連携（HPD・pin・向きの二つの出所）

Phase ID: `ws050-p005`
Parent: [WS050](../ws.md)
Status: in-progress（2026-10-07 P2: 範囲の ACK、調べだけ。UAT の ws181-p006 を先にするため code は未着手で区切った）
Phase disposition: normal
Queue: q834 の続き（P2、Q1 の ACK 2026-10-07「範囲 1〜5 で ACK、weak の口は coding-style に合う形で」）

## 範囲（ws.md の表、design.md §13）

`typec_display_report`、HPD・pin・向きの UCSI と i915 の二つの出所の統合、TC の port と connector の対応付け。

## 注（2026-10-07、ws050-p002 の仕様との照合から、設計の見直しが要る）

- **UCSI の GET_CONNECTOR_STATUS に DP の HPD・pin の field は無い**（3.1 Table 6-43。1.2 にも無い）。design.md §13 の「UCSI 2.0 以上で PPM が返すなら」
  の経路は、3.x の **GET_ATTENTION_VDO（0x16、DP の Attention VDO = HPD の状態）** と **GET_CAM_CS（0x18、今の mode の Configuration/Status = pin の割り当て）**
  で設計し直す。この 2 つが 2.0・2.1 にあるかは unconfirmed（2.x の文書が手に入らなかった）。
- 向きは 3.1 の GET_CONNECTOR_STATUS の bit 86（p002 で実装済み、2.0 で同じかは unconfirmed）。
- GET_ALTERNATE_MODES は 3.1 でも 1 回 2 つまで（2.x の大きな MESSAGE IN でも同じ。p002 は 2 つずつ offset を進める）。

## 範囲（2026-10-07 Q1 の ACK）

1. typec の層に i915 の TC の port ごとの display の記録（hpd、pin、lane 数、向き、generation）、`drv_typec_display_report`・`_get`、TC の port と connector の
   対応表（`drv_typec_display_bind`、既定は未対応、決まるまで i915 の値を connector に入れない）。
2. 統合: 対応がある connector は i915 の値を採り UCSI の値を並べる（/dev/typec に `hpd=1(i915) ucsi=1`）、200 ms 以上違えば `disagree` と log 1 回。
3. UCSI: version ≥ 3.0 で DP の mode の connector に GET_CAM_CS（と GET_ATTENTION_VDO）。
4. i915 → 層: tc-kern.c で readout の後と hotplug の work の connected の step（IRQ の外、tc の lock の外）、`CONFIG_DRIVER_TYPEC` 無しでも link する weak の口。
5. host と build。

## 調べ（2026-10-07、UCSI 3.1 の文書、P2 の scratchpad の ucsi.Upv9）

- GET_ATTENTION_VDO（0x16、§6.5.21 Table 6-55〜57）: command は connector 16〜22。data: alt mode の index 0〜15（0xFF は mode に無い）、VDO の数 16〜18、
  sequence 21〜23、VDM header 24〜55、VDO 56〜87（DP の Attention の VDO = DP Status）。bmOptionalFeatures の「GET_ATTENTION_VDO supported」が 0 なら not supported。
- GET_CAM_CS（0x18、§6.5.22 Table 6-58〜60）: command は connector 16〜22、current alt mode（GET_CURRENT_CAM の配列の index）24〜31。data: index 0〜7、
  Status 8〜39（DP は DP Status の VDO）、VDO の数 40〜47、VDO[N] 48 + 32N（DP は Configuration の VDO）。
- DP の VDO（VESA DP Alt Mode Table 5-3・5-4、Linux の include/linux/usb/typec_dp.h と同じ値）: Status の bit 7 = HPD の状態、bit 8 = IRQ_HPD。
  Configuration の bit 15:8 = pin の割り当て（bit 8 A、9 B、10 C、11 D、12 E、13 F）。i915 の FIA の DFLEXPA1 の値は 3 = C、4 = D、5 = E。
- 方針: HPD と pin は GET_CAM_CS だけで取れる（Status と Configuration）。GET_ATTENTION_VDO は IRQ_HPD の事象の時だけ要る（p005 では使わず backlog の候補）。

## 再開の情報

code は未着手。上の範囲の 1 から。`typec-os.h` に時刻（ms）の口を足すなら host の 2 つの試験（ucsi-host.c・ucsi-acpi-host.c）にも実装が要る。
