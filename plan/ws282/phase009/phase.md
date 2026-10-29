<!-- awesome-plan project=zedbsd record=ws282-p009 -->

# ws282-p009 文書、camera の system event と seat、道具の登録

- Parent: [ws282](../ws.md)、Queue: q1667 / q1667-i05（P6、実装だけ・試験なし、branch agent/p6-ws282d）
- Status: in-progress（文書と camera の system event・seat の差分を実装。seat の切り替えと挿入の確認は未実施）

## 実装した事

- `docs/reference/camera.md`（新規）と `docs/reference/README.md` の索引: node と権限（0600、seat が user に渡す、`KERN_SYSTEM_EVENT_CAMERA`）、所有、record の作法、18 の request の表と誤り、poll、format の表、buffer（DEVICE と IMPORT の手順・`EXDEV`・fence の後の QUEUE）、metadata、control の id、USB の制約（`host_payload_max`・`ENOSPC`・HS の 3072・SS は無し・bulk）、suspend の制約（isochronous の stream 中は眠らない、app は蓋・sleep の前に `STREAM_OFF`）、道具（camcap・camcheck・camvk）。docs の規則どおり plan/ への link は無い。

## camera の system event と seat（`plan/ws282/phase009/camera-event.patch` を Q1 の指示で branch に適用）

- 2026-10-29 Q1「camera-event.patch を自分の branch（agent/p6-ws282d）に当てて commit してください。include/uapi/system.h は UAPI で HAL ではないので事前承認は要りません。…推し（login 画面の account には camera を渡さない）で進めてください。」→ 適用した。login 画面の account に camera を渡さない事は**推しの暫定・ユーザーの確認待ち**。

- SHA-256 `d74638b238d0e98a29d1709ecc07f7e079623163ddc8f12ca31734cb1df886f1`、基点はこの branch の先頭（`git apply --check` で当たる事を確かめた）。
- 中身（design §3.9・D4）:
  - `include/uapi/system.h`（他の WS の UAPI）: `KERN_SYSTEM_EVENT_CAMERA 0x00000100U` と `KERN_SYSTEM_EVENT_CLASSES` を `0x000001ffU` に。
  - `userland/desktop/sessiond/seat.c`（他の WS）: `SEAT_EVENT_CLASSES` に CAMERA、`/dev/cam0`〜`cam3` を session の user に 0600 で渡し、login 画面の account と restore では root の 0600（鍵・smart card と同じ扱い: 開けた者に部屋が見えるので）。
  - `src/drivers/camera/cam.c`（この WS）: 登録の後に ADD、退役で REMOVE を `kern_system_event_post`（weak、subject `camN`、detail `driver=… name=…`）。
  - `userland/tests/systemevents/main.c`（他の WS の試験の道具）: class の名前 `camera`。
- 確かめた事（patch を当てた状態で、後で戻した）: `run-cam-host-test.sh` PASS、amd64 の `vmunix`・`bin/sessiond`・`bin/systemevents` が rc 0、この 4 file に warning 0（log に出た warning は同じ build で作られた外部 package の openssl と perl の locale だけ）。
- 判断が要る点: login 画面の account に camera を渡さない（鍵と同じ）のは推しの判断。design §3.1 は「seat の user に渡す」だけ。

## 道具の登録（Q1 への依頼の案、plan/tools.md は Q1 の物）

- `camcap`（`userland/tests/camcap`）: `/dev/camN` から frame を取り JPEG/PPM に。`-l` で format の一覧、`--probe-only ms` で isoch の実地。終了 0/1/2（ENOSPC）/3。
- `camcheck`（`userland/tests/camcheck`）: PC が全画面で出す `plan/ws282/tests/pattern/pattern.html` の色・counter・drop、`--cycles n` の leak、`--second-open`。最後の行 `camcheck: PASS/FAIL`。
- `camvk`（`userland/tests/camvk`）: Vulkan の memory に取り込み GPU で gray、CPU と比較。最後の行 `camvk: PASS/FAIL`。
- image の config: `plan/ws282/tests/config.mk`（amd64）・`config-rpi4.mk`・`config-rpi5.mk`（camvk も）・`config-visionfive2.mk`。WS の完了の時に今後も使う物を `plan/tools/` に移す。

## 未実施

- seat の切り替えと camera の挿入で `/dev/cam0` の所有が変わる事の確認（実機）。
