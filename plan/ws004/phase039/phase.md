# WS004 Phase 039: evidence-driven WLAN commonization review

Last updated: 2026-09-02

Phase ID: `ws004-p039`

Status: planned; not started; follows completed `ws004-p038`; not queued

Parent: [WS004 hardware expansion](../ws.md)

Tests: [WS004 test index](../tests/README.md)

## Objective

P039 remains unstarted until the retargeted AX211 p038 normal path is complete.
The q061 AX211/CNVio2 observation is intake evidence, not a second working
driver and not permission to begin commonization.

Review the independently working RTL8822BU and Intel AX211 implementations
side by side, identify only common behavior proven by both implementations,
and move a worthwhile stable subset into the existing generic WLAN framework.
This is late abstraction, not a mandate to eliminate duplication. A reviewed
conclusion that no additional extraction is justified is a valid Phase result.

Both drivers must already pass their useful normal paths before this review.
Do not create a proposed common layer as a prerequisite for the Intel driver.

## Frozen design principles

- The stable public WLAN UAPI and its semantic contract are the primary
  module boundary. Do not change them for naming symmetry or implementation
  convenience.
- Each driver remains independently replaceable and may retain entirely
  different firmware, transport, DMA, interrupt, ring, descriptor, reset,
  calibration, regulatory, and hardware-security structures.
- Code duplication is acceptable. Extract code only when both production
  implementations demonstrate the same ownership, lifetime, error, ordering,
  and performance contract—not merely similar syntax.
- Prefer shared conformance tests over shared implementation when they enforce
  the public interface without restricting driver internals.
- Do not introduce a common RTL88 layer, a generic Intel layer, or a combined
  Intel/Realtek hardware framework in this Phase.

## Dependencies

- `ws004-p030`: RTL8822BU normal behavior and the automatic lifecycle matrix
  are complete under its declared profile; p008 retains final physical
  repeatability ownership.
- `ws004-p038`: the independent Intel AX211 scan/WPA2/CCMP/useful-IP path is
  complete and its duplicated/private boundaries are visible.
- Passing focused and physical normal-path evidence for both exact devices.
- The project-wide interface-based modularity and late-abstraction preference
  in the master plan.

## Review procedure

1. Build a symbol/file/ownership inventory for both drivers and the existing
   p027/p029 common core. Classify every candidate as:
   - already correctly common;
   - same public behavior but intentionally independent implementation;
   - genuinely identical internal mechanism and extraction candidate; or
   - device-specific and prohibited from generic code.
2. For each extraction candidate, compare allocation owner, locking context,
   callback lifetime, cancellation, error mapping, timeout, byte order, DMA
   assumptions, secret handling, and teardown. Any mismatch keeps it private.
3. State the benefit in concrete terms: one invariant, parser, state
   transition, or test oracle maintained once. Line-count reduction alone is
   not sufficient.
4. Move at most one coherent boundary at a time beneath the existing public
   UAPI. Keep driver-local adapters so Intel and Realtek state layouts remain
   free to diverge.
5. Run both drivers' complete focused suites after each extraction. Revert the
   extraction if it adds cross-driver conditionals, exposes private hardware
   state, weakens error reporting, or makes one implementation depend on an
   unused feature of the other.

## Public-interface policy

Ordinary refactoring must not modify the public WLAN ioctl structures,
operation meanings, device names, status/error contract, or userland command
surface. Internal driver-operation tables may gain a private adapter only when
both implementations need the same semantic and all existing consumers are
updated atomically.

If the comparison exposes a genuinely missing public capability, document the
exact consumer, operation, lifetime, compatibility effect, and why a private
adapter is insufficient. Mark p039 `uncleared` and request a human interface
decision before editing the public header. Do not bundle that decision into a
mechanical cleanup.

## Verification contract (`HW-T39`)

- Preserve a before/after map of every moved symbol and its ownership.
- Run all p027/p029 generic WLAN, WPA2 codec/engine, CCMP L2, RTL8822B core/
  security/USB, and Intel AX211 transport/security fixtures.
- Prove unchanged ABI layout and ioctl values with compile-time and binary
  conformance checks.
- Run configured amd64 builds, ordinary repository build, and retained
  IDE/xHCI USB-root regressions.
- On the final candidate, rerun one bounded normal-path smoke for each exact
  device using runtime-only credentials: scan, authorized WPA2/CCMP, useful IP
  transfer, disconnect, and down. These are regression checks, not expanded
  throughput or final repeatability campaigns.
- Retain no credential or network identity in evidence.

## Completion conditions

- The review inventory states why each candidate is extracted or remains
  duplicated/device-specific.
- Every extraction is backed by identical demonstrated contracts in both
  drivers and leaves their hardware implementations independently replaceable.
- The public WLAN UAPI is unchanged unless a separately approved significant
  interface decision was made.
- Both complete focused suites and both bounded normal-path smoke checks remain
  passing after the final refactor.
- If no extraction meets the threshold, the documented no-change review and
  passing comparison gates complete the Phase; creating a framework is not a
  completion requirement.

## Reconsideration boundary

Stop an extraction if it requires hardware-family conditionals in generic
code, makes one driver own another's lifetime, changes public UAPI without an
approved decision, or cannot preserve both passing normal paths. Keep the
duplicated implementations and record the rejected abstraction rather than
forcing superficial reuse.

## 2026-10-13 review result (P2, q963): no extraction

Queue: q963 (P2, 2026-10-13). Authority: the user's 2026-10-13 instruction to finish the beta-2 implementation without new features, and Q1's assignment ("a no-extraction conclusion is acceptable; record the reason"). No source was changed.

Prerequisites as found: RTL8822BU normal path complete (p028-p030, p041-p044, p046 physical). AX211 p038 is still formally uncleared (one shared final direct-boot result), but the exact-device VFIO normal path passed (q066) and the 5330 UAT uses AX211 Wi-Fi (BUG-145, BUG-157, BUG-212 records). The comparison below therefore uses both working implementations as they stand in main (2026-10-13).

### Inventory (procedure step 1)

Both drivers plug in only through `struct wlan_radio_ops` (`include/kern/net/wlan.h`): `rtl8822bu_radio_ops` (usb-rtl8822bu.c:625, 13 SoftMAC operations) and `ax211_radio_ops` (intel-ax211.c:1700, the same 13 plus `restart`). `wlan_wpa2_ops` and `wlan_fullmac_ops` are core-internal adapters, not driver tables.

| Topic | Classification | Evidence |
| --- | --- | --- |
| Beacon/probe IE parsing for selection | Already correctly common (`wlan_frame_parse_bss`) | RTL has none. AX211's `ax211_bss_ies_decode` (intel-ax211-bss.c:466) also reads TIM/DTIM and WMM for its firmware association, which `wlan_bss_record` does not carry. It is device-specific use, not a duplicate of the selection parser |
| Auth/assoc/probe frame building, RSN IE | Already common (wlan-wpa2-codec.c, wlan.c:4263) | RTL builds a deauth frame privately for its hardware path; AX211 builds a firmware scan-offload probe template (`ax211_scan_probe_encode`) with firmware rate encoding: device-specific |
| BSS cache and selection | Already common (`cache_insert_locked`, `station_select_bss_locked`) | AX211's private 64-entry cache mirrors the core's eviction rule on purpose (intel-ax211-bss.c:316-318) to feed firmware association. It only looks up the core-selected BSS |
| Data header, LLC/SNAP, EAPOL, PN assignment, replay check | Already common (wlan-l2.c, wlan.c) | Drivers only classify or measure lengths for DMA |
| CCMP header decode on RX / header check on TX | Same public behavior, intentionally independent implementation (see below) | RTL usb-rtl8822bu.c:4529-4572 and 4182-4204; AX211 intel-ax211-rx.c:471-529 and intel-ax211-tx.c:497; core static `ccmp_header_parse`/`ccmp_header_store` (wlan-l2.c:365/348) |
| Channel-to-frequency and channel validity | Same arithmetic, different policy | Core has two static copies (wlan.c:2968, wlan-frame.c:421); AX211 `ax211_pci_channel_frequency` (intel-ax211.c:4139) with NVM/MCC regulatory; RTL a channel table plus JP/ETSI board policy (usb-rtl8822bu.c:571-587, rtl8822b.c:1294-1306) |
| Sequence numbers, rate choice, key slots, deadlines/time units | Device-specific | Hardware descriptor bits (RTL EN_HWSEQ, CAM slots, tick-to-ms), firmware-assigned sequence numbers and rate formats (AX211), separate key state modules |

### Extraction candidates (procedure steps 2-3)

1. CCMP header encode/parse. This is the only 802.11 mechanism that both drivers re-implement privately (48-bit little-endian PN from bytes 0, 1, 4-7, key index `>> 6`, PN 0 rejected). The contracts are not identical:
   - Header offset: RTL assumes a fixed 24-byte header. AX211 computes the header length (QoS, 4-address).
   - Validity: RTL also requires the reserved bits (`& 0x3f == 0x20`). AX211 checks only the ExtIV bit.
   - Error reporting: RTL returns errno values (EACCES, EILSEQ, EINVAL). AX211 returns its delivery enum or STALE.
   - Context: RTL decodes under a spinlock snapshot of adapter state with descriptor fields (`encryption_type`, `icv_error`) mixed in. AX211 decodes lock-free under its caller's `lifecycle_lock` mutex with firmware status bits mixed in.

   By the procedure ("any mismatch keeps it private"), it stays duplicated. The core already re-verifies the header and requires the driver-reported PN to match (wlan-l2.c:226-231), so the invariant that matters (PN and replay) is already maintained once in the core. A shared helper would remove about 40 lines per driver. It would not move an invariant, and line-count reduction alone is not sufficient (step 3).
2. Channel-to-frequency. The formula is one line per band. The regulatory and channel-validity policy that surrounds it is device- and board-specific (RTL board policy, AX211 NVM/MCC). The only invariant that could be shared is the arithmetic itself. Making the core's static copy exported would be a mechanical cleanup inside the core (two core copies plus bcm43455), not driver commonization. It is not worth a regression campaign on both exact devices now.

Conclusion: **no extraction**. Both drivers stay independently replaceable, and the public WLAN UAPI is untouched. Because there is no extraction, HW-T39's post-refactor suites and the two bounded normal-path smokes are not required (completion condition: "a documented no-change review and passing comparison gates complete the Phase"). The comparison gates are the drivers' existing passing focused suites and physical results recorded in p028-p030, p038 and p041-p046. They were not rerun because no code changed.

### Observations for later (not part of this Phase's change)

- Core hardware detail: `wlan_l2_parse_data` expects an RTL-style MIC trailer to still be attached (wlan-l2.c:253-258). AX211 pads 8 zero bytes to satisfy it (intel-ax211-rx.c:284-288, 333-335). This is a core assumption that leaks one device's layout. A later core cleanup could make the trailer optional per received frame.
- RTL's fixed 24-byte header offset in its CCMP RX decode and TX check assumes non-QoS, three-address data frames. It has passed the physical normal paths. If QoS data ever reaches that path, the decode would read the wrong bytes. It is recorded here, not changed: no failure is observed, and changing it is outside a no-extraction review.
- The core keeps two identical static `channel_frequency` copies (wlan.c, wlan-frame.c), and 21 files define private `le16`/`le32` helpers. These are core or kernel-wide tidy-ups, not WLAN driver commonization.

Status: proposed cleared with the no-extraction result (Q1 judges). Evidence: the read-only inventory above (2026-10-13), with file:line references in main as of 55712017f.
