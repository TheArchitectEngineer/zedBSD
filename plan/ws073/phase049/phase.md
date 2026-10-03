<!-- awesome-plan project=zedbsd record=ws073-p049 -->
# ws073-p049: BUG-033 — guest の clang の速さを今の状態で測り、残る原因があれば直す

Status: in-progress（q651-i01、P1 generation12、2026-10-04。測定の道具を作り、測定を T1 に依頼）
Disposition: normal
Parent: [WS073](../ws.md)
Bug: [BUG-033](../../bugs/BUG-033.md)
Queue: q651（2026-10-04 user「P1、P2はBUG-143, 053, 052, 162, 103, 129, 033, 157, 139を修正します。直す順序は任せます。」）

## 範囲

BUG-033 は ws046-p007 で主因（libc の allocator の鎖の走査、buffer cache の hash）を直し、`xmlparse.c` の compile が 513 秒 → 6.5〜6.9 秒（512 MiB）・
5.05 秒（2 GiB、host の 4.9 倍）になった。user（2026-09-28・10-03）: 「解決している見込みで、残りは計測して閉じるだけ」。今の main で測り、十分なら
測定の結果で Q1 に close を提案し、残る原因があれば直す。

## 道具（2026-10-04）

- [bug033-kit.sh](../tests/bug033-kit.sh): expat 2.8.5（package の記録と同じ sha256 を確かめる）の `lib/` の source と host の configure の
  `expat_config.h` を `build/p1-q651/bug033/kit.tar` にまとめ、host での同じ compile の時間を出す（project の clang、zedBSD の target）。
- [bug033-clang.sh](../tests/bug033-clang.sh)（kit の中、guest で走らせる）: `clang --version` と `xmlparse.c`・`xmltok.c`・`xmlrole.c` の `-O2 -c` を
  3 回ずつ、最後に 3 つを同時に。1 行ずつ `BUG033 what= real=`（POSIX `time -p`）。

## host の基準（2026-10-04、この host、`bug033-kit.sh`）

| compile | 秒 |
| --- | --- |
| xmlparse.c | 1.05〜1.36 |
| xmltok.c | 0.91〜1.05 |
| xmlrole.c | 0.12 |

## 検証

- 道具の dry run: host の clang と sh で `bug033-clang.sh` が全ての行を出す（`xmlparse-3 real=1.10` など）。
- QEMU（T1 に依頼、未実施）: clang の入った image（`config/ci/config-amd64.mk`）、guest 512 MiB と 2 GiB の 2 回。合否の目安: user の期待
  「host の数倍以内」。09-24 の値（512 MiB 6.5〜6.9 秒、2 GiB 5.05 秒）より悪くなっていないこと。

## 残り

- T1 の結果で、数倍以内なら close を Q1 に提案、遠ければ gdbstub で PC を採って原因を探す（ws046-p007 と同じ方法）。


## P1 generation12 のラップアップ（2026-10-04、ユーザーの指示で P1 を終了）

T1 に 512 MiB・2 GiB の測定（試験の依頼 4、kit は build/p1-q651/bug033/kit.tar）を依頼済み、結果は Q1 が受ける。再開: host の xmlparse 1.05〜1.36 秒と比べ、数倍以内なら close を提案、遠ければ gdbstub で PC を採る。
