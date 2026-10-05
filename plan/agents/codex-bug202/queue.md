# Codex BUG-202 Queue lane

Queue: q779（2026-10-06、[共有Queue](../../queue.md)・[履歴](../../history/queue-q779.md)へ反映）
Status: finished
Executor: Codex / branch codex/fix-bug202-boot-worker / worktree /tmp/zedbsd-bug202-codex / base acbb7e4a

承認: 現在のuser「実機で起動中（カーネル起動直後）にカーネルがフリーズします。ACPIやモダンスリープのサポートを追加したのがデグレードの直接の原因です。セキュリティはチェックせず、機能性だけチェックして直してください。テストは実機で私が行います。あなたはビルドが通るところまででいいです。」

| Attempt | Phase | 範囲と終了条件 | 状態 |
| --- | --- | --- | --- |
| q779-i01 | [ws073-p055](../../ws073/phase055/phase.md) / BUG-202 | 写真のidle thread sleepsの呼出しを既存ELFとsourceで確定し、起動時のsleep可能なdevice初期化を通常threadへ移す。機能の読み合わせとamd64 kernel build warning/error 0まで。実行試験はuserの実機UATに委ねる | cleared |

依存: 現在の診断guardとuat-0506cのELF（既存）。今回の写真に一致するLPSSのD3→D0待ちを含むbootstrapのscopeだけ。source所有はsrc/kern/entry.c・main.c・include/kern/kernel.h、BUG-202とそのindex、ws073-p055。HAL API変更なし。有限枠は原因確定から修正・buildの1 attempt（最大3h）。2026-10-06 user「マージしてください。」でmainへの統合と共有記録の更新を実施。GitHub公開は設定どおり保留。

## 結果と引き継ぎ

q779-i01 cleared（user指定の実装とbuild）。source WIP `801393ad`、amd64 warning/error 0、include/vmunix check PASS。実行試験は未実施でBUG-202はtracking。手順と証拠はws073-p055とBUG-202末尾。mainの履歴整理後、今回の2コミットだけをmain 010ae1c0上へ載せ直した（source e093bebe、記録 c313e3ef）。q779の衝突が無いことを確認し、mainへの統合と共有Queue・Master・Past Logへの投影を実施。統合時のbuild結果は履歴を参照。GitHub公開は保留。WS全体の完了や実機不具合の解消は主張しない。
