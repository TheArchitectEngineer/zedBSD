<!-- awesome-plan project=zedbsd record=ws290-p005 -->

# ws290-p005 session・VM

- 親: [WS290](../ws.md)。設計: [design.md](../design.md) §18 の p005（版 4.5、ghci.md cb937e6）。
- 状態: cleared（2026-10-28 P3 q1644-i01: 場面 PASS（driver 587）、GHCI 有りの amd64・riscv64 の build の warning 0、design-reviewer の code の review の H・M を反映）
- 依存: p004

## 範囲

session・VM（chunk・勘定・map・unmap・close・再生、flat の恒等の VA）、memory（host、4 GiB の上限）、resource・blob・map・read・write、share・import。local は ENOTSUP。+ `SHARE | ALLOCATION_SHARE`

詳細は design の該当の節（§4〜§16）と §14 の p005 の場面。

## 受け入れ

- p005 の場面 PASS、build の warning 0
- 試験は host 試験と amd64・riscv64 の build（warning 0）。実機は p014 だけ。
- 見積り: 26 LW。

## 仕様の仮の扱い（ghci-review の回答で変われば直す）

無い（design §19: d9252e4 で仕様の残りは無い）。

## 記録

- 2026-10-28 P3（q1638-i01）: design §18 から立てた。
- 2026-10-28 P3（q1644-i01）: 実装。
  - code: `vm.c`（新: session の番号・SESSION_OPEN/CLOSE・VM_MAP/UNMAP・arena と chunk・CP_HEAP の勘定・回収・回復の犯人・submit を失った session・数え直し・再生・SESSION_ACTIVE の期待値・DEAD の報告）、`ghci.c`（resource・blob・read・write・map、`GPU_CAP_RESOURCE | BLOB | TRANSFER | MAPPING | SHARE | ALLOCATION_SHARE`）、`share.c`（書き直し: export・release・import、SCANOUT の import は ENOTSUP）、`memory.c`（hold・unhold、LOST で GPU に渡した物は quarantine）、`cp.c`（submit の持ち主、回復の配置の待ち）、`recover.c`（2 の犯人、4 の submit を失った session、6〜8・10）。実装の形は design 版 4.9（§5.5 の「実装の形」）。
  - 試験: `plan/ws290/tests/ghci-host-test.sh` PASS（words 141・ref 56・model 254・driver 560、1.1 の名前の grep）。p005 の場面: sessions（15 個と 16 個目 EBUSY、close の間は番号を保つ、勘定）、mappings（PROT 0・1 の両方で model の表・chunk の持ち主・L0 の勘定、unmap の後の物理、他の session が取った chunk の勘定 T12、close の返し方）、flat（packet 無し、VA = bus、unmap は最後の fence、reset は再生しない）、heap（上限を超える open・map は ENOMEM で MMU_OOM 無し）、close（VM_UNMAP → SESSION_CLOSE → FENCE）、replay（4 KiB の ring より大きい再生 120 map、再生の途中の PERM_FAULT を最後の確かめで見つけ次の周で犯人だけ DEAD）、culprit（PROT の client の fault で reset、犯人と SESSION_ACTIVE で拾う 2 つ目が DEAD、確かめていない open は犯人にしない、嵐に数えない、書かれた fence でも犯人の ticket は FAILED、DEAD の close は packet 無し・勘定を二重に返さない、番号を再利用しない）、lost work（submit を失った session だけ DEAD、VM の操作だけの session は再生、MMU_OOM は犯人無し）、retry（map・open・unmap・close が門で回復を待ちやり直し、重複無し）、resources（storage・blob・read・write・view・export・import・SCANOUT 拒否）。
  - build: GHCI 有りの amd64（build/ws290-p005-amd64）・VF2（build/ws290-p005-vf2）、GHCI 無しの config/ci/config-amd64.mk・config-visionfive2.mk（build/ws290-p005-*-ci）、全部 exit 0・warning 0。
  - commit: 55f636f74（code・試験・design 4.9）、700cf6875（vmunix.mk の GHCI の行）。
  - 未実施: 実機（p014）。local memory は ENOTSUP（p011）。VkDeviceMemory の blob（blob_id ≠ 0）は ENOTSUP（p007）。
- 2026-10-28 P3（q1644-i01）: design-reviewer（Opus 5.5）の code の review（55f636f74・700cf6875 を対象）: H 1・M 6・L 9。判定は「H1 を直し M4 と M5 の一部を試験に足せば approve 可」。対応:
  - H1（犯人の ticket が `ghci_cp_wait` で OK になる）: reset の記録 `ghci_reset_record` に、犯人の submit のうち fence が書かれた物の seq（32 個まで。超えれば世代の終わりをその最初の前に下げる）を持ち、`ticket_state` が FAILED と答える。`ghci_vm_ticket` を消した。試験は `ghci_cp_wait` = EIO。
  - M1（遡って FAILED）: H1 の形で消えた（fault の時に completed になっていた物は submit_log から既に外れ OK のまま）。
  - M2（SESSION_ACTIVE の (b) が装置の fault を嵐から隠す）: (b) は最初の fault が session に帰せる時だけ。試験: PROT で装置の fault と同時に session が ACTIVE から落ちても誰も DEAD にならず嵐に数える。
  - M3（flat の map が門と LOST を見ない）: flat も門を通る（packet は置かない）。
  - M4（MMU_OOM の試験が PROT でない）: PROT の profile で MMU_OOM が session を ACTIVE から落としても犯人にしない場面を足した。
  - M5: (a) LOST で map 済みの物理が quarantine に入り detach で返る、(b) ring の空きの待ちが回復で EAGAIN になった map が新しい arena を返し回復の後に 1 回だけ map する（chunk の漏れ無し）、(g) 1 つの allocation を 2 つの session に map（holds 3）を足した。(c) 再生の途中の MMU_OOM・(d) PROT 0 の上限・(e) DEAD の後の chunk の再利用・(f)・(h) は足していない（(c)(f) は PERM_FAULT と装置の reset の場面で同じ経路を通る）。
  - M6（DEAD の近道は reset の成功が前提）: close に注記。p007 の isolate は DEAD と別の印で物理を device の hold に移すこと（ws.md の再開点に p007 の制約として書く）。L2（LOST の close が submit_log の持ち主を解放）も p007 の core の reset で log を先に空にする制約。
  - L1（二重 close の空回り）: CLOSING は抜ける。L3（再生の失敗でも replayed）: 成功の時だけ。L4（holds の下溢れ）: 0 なら log して返さない。L5（import が mappable を広げる）: export の時の flags を引き継ぐ。L8（出典を file の先頭に）: block に移した。L6（resource id の回り込み）・L7（報告の ABA）・L9（大きな再生の時間）は低い危険として残す。
  - 確かめた事: `plan/ws290/tests/ghci-host-test.sh` PASS（words 141・ref 56・model 254・driver 587）。GHCI 有りの amd64・VF2 の vmunix を build し直し exit 0・warning 0（build/ws290-p005-amd64・-vf2）。GHCI 無しの CI の 2 つは直しの前に warning 0（GHCI の file を含まない）。
