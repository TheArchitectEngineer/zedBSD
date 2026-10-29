<!-- awesome-plan project=zedbsd record=ws314-p003 -->

# ws314-p003 設計: post・`kl_process`・`kl_launch`・`kl_instance` の event・libbrowser の network の thread

- 親: [WS314](../ws.md)。Queue: q1690 / q1690-i01（担当 P2、2026-10-30）。
- Status: cleared（2026-10-30。ユーザーの判断 A〜D は Q1 経由で問い合わせ中。答えで design.md の名前・境界を直す）
- Disposition: normal

## 目的・受け入れ

- ws.md の決定 1〜7 と「承認された案」を具体の API・thread・platform の当てにする（[design.md](../design.md)）。どれも fd を公開しない。
  platform の層の実装の当て（zedBSD・Linux・FreeBSD、Windows に移す時の写像）を書く。design-reviewer を 1 本走らせて指摘を反映する。
- ユーザーの判断が要る点は推しと理由を添えて Q1 に送る。

## 成果

- [design.md](../design.md): §1 棚卸し（実測）、§2 platform の層の置き場（libkeiland の中の `libkeiland/platform/`。`libkeiland-backend-*` は
  compositor だけの静的 library なので置けない → compositor・backend・sessiond の所有の追加は不要）、§3 `kl_app_post`・`kl_window_post`、
  §4 `kl_process_*`（level の出力 event、行と CUT、`waitid(WNOWAIT)`、thread の表）、§5 `kl_launch`、§6 `kl_instance_bind`、
  §7 libbrowser の `browser-net` の thread と wake の callback、§8 platform の表、§9 ユーザーの判断、§10 review の反映、§12 Phase の細目。
- design-reviewer（2026-10-30）: 23 件（high 6・medium 12・low 5）。全てを反映（design.md §10 の表）。

## ユーザーの判断（Q1 へ送った、返事待ち）

- A. 権限を上げる起動を API に含めるか（推し: 含めない）。
- B. `kl_window_post` の意味の変更（推し: 同じ名前で data を足し、他の thread から）。
- C. 名前（推し: design.md §9）。
- D. 「UNIX 依存の削除」の境界（推し: 公開の API と app ↔ desktop・他の program のやりとりから外す。app の中の network の層・pthread・file の I/O は
  その app の platform の部分として残す）。

## 残り・提案

- p005 が大きい（review 23）: (a) kl_process の核と Installer・Terminal・Phone、(b) `kl_launch` と Files・Settings・PDF Viewer・Terminal の新しい窓、
  (c) Preview の API から fd・pid を外す、の 3 つに分ける案。番号は Q1 が決める。
