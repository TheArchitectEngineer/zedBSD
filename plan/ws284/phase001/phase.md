<!-- awesome-plan project=zedbsd record=ws284-p001 -->

# ws284-p001 YOLOX-Nano の推論の設計

- 親: [ws284](../ws.md)、担当 P1（2026-10-23、worktree `agent/p1-yolo`）
- Status: in-progress（設計の第 2 版を書いた〈2026-10-23 P1、worktree `agent/p1-yolo2`〉。Q1 の判定とユーザーの D10・D11 の待ち）
- 依頼の元: 2026-10-23 ユーザーの WS282〜WS285 の目標（[ws.md](../ws.md) の出典）。model はユーザーが YOLOX-Nano（Apache-2.0）を選んだ（Q1 の依頼文）。

## 目的と受け入れの条件

[design.md](../design.md) に次を書く。code は書かない。

1. 公式の source（YOLOX、`exps/default/yolox_nano.py`、depthwise、width 0.25、depth 0.33）の network の正確な graph（層・形・op の数）、BN の畳み、SiLU、Focus、SPP、upsample、concat、head の decode と NMS（design §1、付録 A）。
2. 重みの元（URL・license）、変換の script（host で 1 回）、zedBSD の形式（fp32・任意で fp16、BN を畳む、層の表と SHA-256 の入った header）、置き場所（zedBSD-firmware の directory）と license の監査の覚え（§2）。
3. `libyolox` の API、op ごとの shader と SPIR-V の作り方、memory の計画、CPU の参照、試験の計画（§3・§4）。
4. Pi 5 の速さの見込みと fallback（§5）。
5. Phase と LW、ユーザーの判断（§6・§7）。

## 記録（2026-10-23 P1）

- 資料: YOLOX の main `6ddff4824372906469a7fae2dc3206c7aa4bbaee` の model の file を scratchpad に取得して読んだ（commit しない）。release `0.1.1rc0` の `yolox_nano.pth`（7,694,953 byte、SHA-256 `cd28f55f…ff7b`）と `yolox_nano.onnx` を取得し SHA-256 を記録。torch を使わず制限した unpickler で checkpoint の tensor の名前と形を読み、構造（§1）と全て一致する事を確かめた。
- [graph.py](graph.py)（source を写した graph）で付録 A の表と数を出した: 139 op（conv 113: 3×3 dense 1・depthwise 30・1×1 82、concat 13、add 7、maxpool 3、upsample 2、Focus 1）、416 で 522.6 M MAC、BN を畳んだ parameter 904,463、活性化の最大 4.62 MiB（concat を別名にして、fp32）。
- 要点:
  - BN の eps は 1e-3（`yolox_nano.py` の `init_yolo`）。checkpoint に入っていないので変換で明示する。
  - 入力は BGR・0〜255・114 の pad の letterbox（左上）。stem の重みを並べ替えて RGB で受ける。
  - 重みの file「ZNET」は PyTorch の形のまま（GPU の配置は library が load で決める）で、firmware の repository の `yolox-nano/` に LICENSE（Apache-2.0 の原文）と NOTICE を添えて置く。
  - library は C4 の配置、7 つの shader（preprocess・stem・conv1x1・dwconv3x3・maxpool5・upsample2x・decode）、122 dispatch、Vulkan 1.0 の core だけ（shared memory・atomic・subgroup・16 bit の拡張を使わない）。shader は木の慣習どおり regenerate.py で shaders.h を作って commit。
  - CPU の参照は NCHW の素直な C で、照合の基準と compute の無い所の fallback。
  - Pi 5 の見込み: 416 で 60〜150 ms、320 で 35〜90 ms。A76 の CPU のよく作った実装の方が速い可能性（R1）。
- Phase の案（design §6）: p002 重み（4〜6 LW）、p003 骨格と CPU の参照（6〜8）、p004 Vulkan の backend（8〜10）、p005 package と試験の program（3〜4）、p006 Pi 5 と調整（6〜10、WS283 に依存）、p007 規約と license の監査（2〜3）。計 29〜41 LW。p002〜p004 は host だけで進む。
- ユーザーの判断（design §7）: D1 重みの置き場所、D2 package の名前・menu・同意、D3 library の名前・場所、D4 重みの license の解釈と COCO で学習した重みの配布、D5 試験の data の置き場所、D6 既定の閾値、D7 GPU の入力は VkBuffer だけ、D8 fp32 の file だけ、D9 CPU の最適化は範囲の外。
- WS283 への伝達（design §3.7）: shader が使う SPIR-V の機能の一覧と、1 submit の 122 dispatch の固定の時間（CSD の job ごとの L2T の clean）の問い。
- HAL の API: 不要。toolchain: 変えない（host の glslc・spirv-val を使う既存の慣習）。
- 未実施: PyTorch での実行（host に torch が無い、p002 の T1）、Pi 5 の観測、design-reviewer の review。

## 第 2 版（2026-10-23 P1）

- 入力: [review](review-20261023.md)（F1〜F22）と、ユーザーの判断（2026-10-23）「decisionは推しでいいです。重みはダウンロードしてビルドする形にしましょう。それならソースツリーにライセンスを混入させないはずです。」→ D1〜D9 は推し、ただし D1 は「build が公式の .pth を取得・確かめ・変換、変換した重みはどの repository にも置かない」。
- [graph.py](graph.py): F13 の数え方を直し（入力の画像を両方の数で op 0 まで生かす）、`gpu` の mode を足した（融合と分割の後の dispatch、arena、DRAM の量）。`python3 -I graph.py 416 gpu 8` → 融合 119 dispatch、8 M MAC で分けて 149、実行する MAC 544.0 M、arena 5.28 MiB、DRAM 94 MB（下限）〜625 MB（上限）。320 は 129 dispatch・3.12 MiB・58〜380 MB。
- 見込み: 計 35〜49 LW（第 1 版 29〜41）。p002 5〜7、p003 7〜9、p004 10〜13、p005 3〜4、p006 6〜10、p007 2〜3、新しい p008（V3D 7.1 の host の compile の確かめ）2〜3。Pi 5 は 416 で 40〜100 ms（WS283 p008 の後）、最初は 170〜380 ms。
- ユーザーに確かめたい事: D10（image の build で license の yes/no を出すか。推し: 出さない）、D11（COCO の license 7・8 の試験の画像を tree に入れてよいか。推し: 入れる）。

| 指摘 | 変えた事 | design の節 |
| --- | --- | --- |
| F1 spec constant | 使わない。違いは build 時の variant（`regenerate.py` の `-D`、15 の SPIR-V）か push constant。memory の access が変わる違い（残差）は必ず variant | §3.6 の表、§3.7 |
| F2 command buffer と VkBuffer | 固定の graph の CB（library の buffer だけ、位置は push constant）と、submit ごとに記録し直す入力の段の CB を 1 回の submit に。idle の時だけ descriptor を更新。offset は切り下げ＋残りを push constant。staging は大きさが越えた時だけ作り直す。試験は validation layer 付きで error 0 | §3.6「command buffer」、§3.5、§4 の冒頭 |
| F3 1 dispatch の長さ | `max_dispatch_macs`（既定 8 M MAC、効率 10% で約 1.5 ms）で画素の tile・行の範囲に分割。416 で 149 dispatch | §3.6「dispatch の分割」、§3.2、§5 |
| F4 stem の配置 | Focus＋3×3 を元の画像の 6×6・stride 2・pad 2 の conv に（重みの式）。CPU の参照は Focus を明示 | §3.3 の 1、§3.4 |
| F5 前処理 | float64 の r と `(int)(w*r)`、写像・clamp・11 bit の係数・丸めの式、係数の表は host が double で作る（GPU と CPU は bit で一致）、cv2 とは 0〜255 で ±1、415 の帯 | §1.7、§4 T5 |
| F6 比べる量と許容差 | T1 は `decode_in_inference=False` で ONNX と比べる。T2・T4 は decode の前の 85 値を atol＋rtol で。T3 も atol＋rtol。T0 (d) に ONNX の initializer の照合 | §4 T0〜T4・T7 |
| F7 debug の runner | 同じ融合の計画で arena を再利用しない。存在しない tensor は `YOLOX_ERROR_FUSED`。config の `flags` に `YOLOX_RUNNER_DEBUG` | §3.5、§3.2 |
| F8 Pi 5 の見積もり | 1.15 GHz（55.2 G MAC/s）。DRAM の下限 94 MB・上限 625 MB を graph.py で計算。lane＝画素、出力の組＝`WorkgroupId.x`（一番速く回す、重みは ldunifa）。register の表（24／56）で conv1x1 を O×P ∈ {1,2}² に作り直し、第 1 版の 4×4 をやめた。詰めた後と最初を分けて見積もった | §5、§3.6「invocation と dispatch の対応」、§3.7 |
| F9 WS283 への依存 | p006 の依存を WS283 p004・p006・p007 の cleared に（速さは p008 の後）。新しい p008 で 15 の SPIR-V を host で bcm2711 7.1 の後段にかける | §6、§3.7 |
| F10 重みの置き場所 | ユーザーの判断で、公式の .pth と LICENSE を取得（mirror → upstream）して build の中で変換、ZNET の SHA-256 を固定。firmware の repository の約束（byte が同じ、upstream に戻れる）にそのまま合う。同意の問いは D10 | §2.2、§2.4、§7 |
| F11 ZNET の load | kernel・stride・groups・maxpool の値、生成の一意、入力を書く op が無い、64 bit の積、予約が 0、要素の境界、fp16 の bias、入力の大きさの上限と device の limit、負の試験 T2b | §2.3、§4 T2b |
| F12 非同期の API | queue の排他（`queue_lock`）、wait の stage、queue family と所有権、自分の device では `NOT_SHARED`、DEVICE_LOST、count と capacity、`struct_size` | §3.2 の約束 |
| F13 memory の数 | 5.28 MiB（入力と stem の出力）。別名は最大を減らさず、写しの dispatch と約 15 MB の帯域を無くす | §3.5、graph.py |
| F14 dispatch の数 | 融合した 119（reg＋obj、decode 3）、分割の後 149 | §3.6 の表 |
| F15 残差の順 | `out = SiLU(acc + b) + residual` | §1.5、§3.3 の 2 |
| F16 barrier | 連続する全ての dispatch の間に global の barrier（SHADER_WRITE → READ｜WRITE） | §3.6「barrier」 |
| F17 venv | psutil・tabulate・packaging・onnxruntime を足し、版の pin と `pip freeze` の記録 | §4 の venv |
| F18 試験の無い所 | 320 の参照（T1・T2・T4）、format・row_bytes（T5）、非同期・共有の API（T9）、lavapipe の固定（`VK_DRIVER_FILES` と device の確かめ） | §4 |
| F19 NMS | 降順、同点は anchor・class の小さい順、class ごとに厳密、試験は同点と境を除く | §1.6、§4 T2 |
| F20 置き場所 | converter は `userland/firmware/yolox-nano/`、試験の data は p007 で tests/ へ移す | §2.2、§4、§6 p007 |
| F21 license の表示 | NOTICE に COCO の 1 行 | §2.5 |
| F22 camera の分布 | WS285 への伝達 | §9 |

## 再開の条件

Q1 の判定（第 2 版）とユーザーの D10・D11 の判断。その後 p002 を Queue に入れる。
