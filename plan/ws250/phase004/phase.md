<!-- awesome-plan project=zedbsd record=ws250-p004 -->

# ws250-p004: 実機の scenario の script

Status: in-progress（2026-10-29 P5、q1676-i01。script を書いた・実機で未実行。cleared にしない）
Parent: [WS250](../ws.md)
Approval: p002 と同じ。

## 範囲

[design.md](../design.md) 6.3 の S0〜S6 を流す `plan/ws250/tests/rtx-hang-check.sh OUT VMUNIX`（`plan/tools/gpu-perf/measure.sh` の段を使う、GPU_PERF_MACHINE=rtx）。

## 受け入れ

`sh -n`。実機で流すのは Q1 の割当で。

## 実装（2026-10-29 P5）

- `plan/ws250/tests/rtx-hang-check.sh OUT VMUNIX [--probe]`: `measure.sh deploy --kernel` → S0（mview scene-5000 12 s と vkhang --loops）→ S2（mview の
  中で vkhang、その後 vkhang --loops と keiland-shot）→ S3（vkhang 5 回と対照）→ S4（vkhang 2 つと mview）→ S5（vkhang を 0.5 s で kill -9、終わるまでの秒）→
  終わりの shot →（`--probe` の時だけ）S1（`measure.sh cfg nvrtx.kill 0` で再起動、mview の中で vkhang を 10 s、GSP の channel error の行、その後 line を外す）
  → `measure.sh linux`。各 scenario の log は OUT/sN.log、要約は OUT/summary.txt。mview と vkhang は machine の conf の USB_MVIEW・USB_VKHANG
  （plan/ws039/tests/vkhang-build.sh で作る）。
- 確かめた事: `sh -n` だけ。未実施: 実機での実行（Q1 の割当で、p005 の後）。S1 は GR が詰まったままになりうる（GSP が RC しなければ）ので最後に置いた。
