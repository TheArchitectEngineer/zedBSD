<!-- awesome-plan project=zedbsd record=ws288-p038 -->

# ws288-p038 TU の filter の pipeline（p025 の前倒し）

- 親: [WS288](../ws.md)。p025（timing の 1 段目）の範囲のうち TU の filter だけを先に行う。
- 状態: in-progress（2026-10-25 14:05 Q1、q1578 P2）
- 依存: p036 の d94076c（T1-791 で試験中）の上。

## 決定（2026-10-25 14:05 ユーザー）

Q1 の説明（ffilt は 1 cycle に選択・ETC の展開・unpack・重み・FMA を全部行う状態機械で、配線後の最悪の 39 ns・約 110 段。累算 A = fma(w, x, A) の依存は 16 個の累算器と順番の入れ替えで解ける）の後、「ではTUのfilterはパイプラインにしてください。」

## 範囲（amaterrace-gpu）

1. `atg_tu_ffilt`: 前半（slot の選択・ETC/EAC の展開・atg_fmt_unpack・重み）を段に分け、FMA を複数段の pipeline にする。累算の依存は、同じ累算器への足し算が FMA の latency 以上離れる順番（例 texel → lane → 成分）で避け、足りない時（mask の lane が少ない、成分が少ない）だけ止める。結果は今と bit で同じ（tools/tumodel.py の FFilter）。
2. `atg_tu_filter`（8 bit の形式）も同じ観点で見て、TU の最悪の経路にかかる所を段に分ける。
3. 新しい算術は p037 の規則（rtl/common の am_ の module、am_mul の LAT）で書く。
4. 目標: TU の中の最悪の経路を 5 ns（200 MHz）に近づける。OOC で TU の単体の WNS と段数、LUT・FF・DSP・BRAM の前後を測る。処理量（1 quad の cycle 数）の前後も記録。

## 受け入れ

- TB atg_tu_ffilt・atg_tu_filter・atg_tu・atg_top_tex が PASS（bit 一致）、lint clean。make sim 全部と Verilator は T に。
- TU の単体の OOC の WNS・段数・面積の前後の表。met しない経路は p025 への申し送りの表に。

## 記録

- 2026-10-25 14:05 Q1: 新設、P2 に（新しい起動、p2-p036 の d94076c から branch p2-p038）。
