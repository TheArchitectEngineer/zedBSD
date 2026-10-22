<!-- awesome-plan project=zedbsd record=ws284-p001 -->

# ws284-p001 YOLOX-Nano の推論の設計

- 親: [ws284](../ws.md)、担当 P1（2026-10-23、worktree `agent/p1-yolo`）
- Status: in-progress（設計の第 1 版を書いた。Q1 の design-reviewer とユーザーの判断 D1〜D9 の待ち）
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

## 再開の条件

Q1 の design-reviewer の結果とユーザーの D1〜D9 の判断。その後 p002 を Queue に入れる。
