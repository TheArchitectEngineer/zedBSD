<!-- awesome-plan project=zedbsd record=ws290-p009 -->

# ws290-p009 記録と実行

- 親: [WS290](../ws.md)。設計: [design.md](../design.md) §18 の p009（版 4.5、ghci.md cb937e6）。
- 状態: planned
- 依存: p008

## 範囲

記録と実行: vk-pass・vk-record・vk-execute、draw・indirect・dispatch・barrier・dynamic state・転送・query・event・clear

詳細は design の該当の節（§4〜§16）と §14 の p009 の場面。

## 受け入れ

- p009 の場面 PASS、amd64・riscv64 の build の warning 0
- 試験は host 試験と amd64・riscv64 の build（warning 0）。実機は p014 だけ。
- 見積り: 36 LW。

## 仕様の仮の扱い（ghci-review の回答で変われば直す）

design §19: N78（属性の register の累積の base_i と header の 4 × loc + comp の食い違い）は ghci.md の累積で作る。p006 の compiler の取り決め（design §11.4 の「p006 で決めた取り決め（版 4.10）」: STORAGE の image は linear・set 3 の配置・texel buffer は slot と entry・属性の表は常に渡す・VS を先に compile・dispatch と draw の間の table の置き直し・c24〜c31 に LOAD_CONST しない・fragment の無い pipeline の FS）を守る。

## 記録

- 2026-10-28 P3（q1638-i01）: design §18 から立てた。
- 2026-10-28 P3（q1644-i01）: p008 の review から p009 に渡す物: submit が参照する pipeline・set・view・buffer を fence まで hold する、code の heap（EXEC）と data の heap（EXEC 無し）を分ける（design 版 4.12 §11.4）、dynamic offset は pipeline layout の set ごとの base から作る、`subpass_of` の不正な render pass は断る、`ghci_vk_layout_tables` の ENOENT は draw を飛ばして log。
