<!-- awesome-plan project=zedbsd record=ws101-p012 -->

# ws101-p012: L3 素の 5330（S13 の本番の形、回復の手順、G1 の素の機械の分）

Phase ID: `ws101-p012`
Parent: [WS101](../ws.md)
Status: planned（2026-10-01 に phase.md を作った。範囲は ws.md の表の行のまま。依存の表の「p017」は、ユーザーの「今のまま」で p017 が uncleared のまま止まったので、main が見直す: 下の「依存」）
Phase disposition: normal
Queue: なし

## 範囲（ws.md の表から）

素の 5330: G1 の受け入れの残り（vkcs 21 step を素の機械で）、G3 の時間、GPU が固まったときの回復の手順。ユーザーの確認。
「今のまま」（2026-09-30 ユーザー）の後は、G3 の時間は「測って記録する」だけで、倍率の目標（3 倍・10 倍）は判定に使わない。

## 依存

- 表の依存は p017。p017 は uncleared（最適化を止めた）なので、この Phase の A（S13）は p015 の後で始められる。依存の書き換えは main の判断（[guide.md](../guide.md) 3.2 の提案 A・C）。
- 素の 5330 の起動はユーザー（[plan/tools/hw5330/README.md](../../tools/hw5330/README.md)、作成中）。
- demo の image は main の build（`plan/ws075/demo/build-demo-image.sh`。Noct の build を含むので subagent は作らない）。

## 手順（2026-10-01 追記）

### A. S13 を素の 5330 で（G4。提案 C を採るなら ws101-p019 へ移す）

1. main に demo の image の在りかを聞く（例 `build/demo-lcdN/hdd-image.img`）。2026-10-01 の main の checkout には S13 の入った demo の image が無い
   （`build/demo-lcd9` は gpudemo と accel の前）。無ければ main に作り直しを頼む。image の `/bin/noct` が accel 付きか確かめる:
   ```
   grep -c -a libGLESv2 build/demo-lcdN/bin/noct    # 1 以上
   ls build/demo-lcdN/rootfs/usr/share/gpudemo/      # mix.nct と s13.sh
   ```
   （`build/demo-lcdN` は main の答えに置き換える。`rootfs/bin/noct` と `bin/noct` が違う時がある: p017 の追記の注。image に入るのは `rootfs/` の物）
2. passthrough で事前に通す（lock は script が取る。他の実機の試験と同時にしない）:
   ```
   plan/ws101/tests/demo/s13-hw.sh build/demo-lcdN/hdd-image.img build/ws101-p012/s13-pt
   ```
   `build/ws101-p012/s13-pt/shots/*-s13-live.png` を読み、「The results are the same on the CPU and the GPU.」を確かめる。
   （passthrough 用の image は `passthrough` の語付きで build した物。素の機械の image とは VBT が違う: `build-demo-image.sh` の先頭。passthrough の image が無ければこの段は飛ばして「未実施」と書く）
3. ユーザーに依頼（素の 5330）: USB に書いて起動 → kei の自動 login → Kei の button → App Home → Terminal → `sh /usr/share/gpudemo/s13.sh` を 3 回。各回の画面の写真。
   ssh できれば `noct --gpu-list` の出力も。
4. 結果を下の「確認」の表に書く（素の 5330 / passthrough を分ける）。写真は `plan/ws101/phase012/` に置く（PNG/JPEG）。

### B. 回復の手順（台本の形）

1. 素の 5330 で、S13 の途中に Terminal で Ctrl+C を打ち、同じ Terminal で s13.sh をもう一度走らせて通るか（ユーザーの操作、写真）。
2. 下の「回復の手順」の節を、確かめた物だけ「確認済み」として書く。GPU を故意に固めない。

### C. G1 の素の機械の分（vkcs）

1. 素の機械で試験の kernel（`plan/ws031/tests/vkloop-hw.sh` の image）を起こす方法を [plan/tools/hw5330/README.md](../../tools/hw5330/README.md) と WS075・WS084 の記録で調べる。
2. 方法が無い、またはユーザーの時間が要るなら、「未実施（方法: …）」と書いて main に報告する。C は A・B の clearance を止めない形に分けることを main に提案する。

### D. G3 の時間（記録だけ）

A の写真の「CPU: N ms」「GPU: N ms」を記録する。内訳が要る時だけ [guide.md](../guide.md) 6 章の「時間の内訳」を頼む。

## 回復の手順（デモの台本の案。B で確かめた物に印を付ける）

| 段 | 操作 | 確認 |
| --- | --- | --- |
| 1 | Terminal で Ctrl+C、`sh /usr/share/gpudemo/s13.sh` を再実行 | 未実施 |
| 2 | Terminal を閉じ、App Home から新しい Terminal | 未実施 |
| 3 | logout → greeter → kei で login | 未実施 |
| 4 | 電源 button の長押しで止め、USB から再起動（約 N 秒） | 未実施 |

## 完了の条件

- A: 素の 5330 で s13.sh が 3 回続けて「The results are the same on the CPU and the GPU.」と「(6 kernels ran on the GPU)」を出す（写真）。
- B: 回復の手順の 1 が素の 5330 で確かめられ、表に書かれている。
- C: vkcs 21/21 が素の機械で PASS、または main の判断で別の Phase へ移された（理由と方法の記録付き）。
- D: CPU と GPU の時間が素の 5330 の数として記録されている（倍率は判定に使わない）。
- 記録: ws.md の表と Resume point、Past Log は main。

## 確認

未実施。
