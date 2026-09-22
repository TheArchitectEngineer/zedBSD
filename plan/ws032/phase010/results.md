# ws032-p010 結果: レビュー（2026-09-23、cleared）

q315-i10。WS 終了時点で、回帰・契約検査・ライセンス・制限の整理を行った。

## 1. 回帰

同一イメージを複製して 4 並列で走らせ、6 本すべて exit 0。

| 試験 | 見るもの | 結果 |
| --- | --- | --- |
| `tests/sh-target.sh` | `/bin/sh` の回帰 | PASS |
| `tests/clang-target.sh` | 実機でのコンパイル・リンク・実行と終了状態 | PASS |
| `tests/openssh-target.sh` | 鍵生成・公開鍵ログイン・`scp`・`sftp`・疑似端末 | PASS |
| `tests/sshd-service-target.sh` | init が起動する `sshd` へのログイン | PASS |
| `plan/ws033/tests/networking-target.sh` | ネットワーク | PASS |
| `tests/lldb-target.sh` | デバッガ（停止・変数・ウォッチポイント・step・終了） | PASS |

加えて `tests/signal-stack-target.c` を実機で 10/10 PASS。シグナルハンドラと
スレッドが呼び出し規約どおりのスタックで始まること、そこから十六バイトを動かす
命令を含む libc 関数を呼べることを見る。

## 2. 契約検査

- 動的 ELF の契約（PIE・soname・DT_NEEDED・RPATH 不使用）は OpenSSL と OpenSSH の
  ビルド中に `tools/build/check-dynamic-elf.py` が検査する。
- 取得物は `external.mk` が SHA-256 とアーカイブ member（単一根・絶対パス無し・`..`
  無し・通常ファイルのみ）を検査してから展開する。
- カーネルは `make` の `amd64 vmunix check: PASS` を通している。

## 3. ライセンス

[provenance.md §4](../provenance.md)。GPL 文言を含む 5 件はいずれも例外条項または
多重ライセンスで寛容側を選べることを確認済み。イメージには 4 件の通知を置く
（[p009](../phase009/results.md)）。

## 4. 制限（完了にしないもの）

- [BUG-026](../../bugs/BUG-026.md): lld の出力が通常ファイルへ全ゼロで届く。
  `--mmap-output-file` で回避したまま原因未特定。本WSで実機デバッガが入ったので、
  記録した再開手順（`InMemoryBuffer::commit()` の直前を観測する）が実行できる。
- [BUG-027](../../bugs/BUG-027.md): ファイル裏付けページのフォルトが遅い。LLVM 由来の
  プログラムの起動が 10〜30 秒。lldb は packet-timeout 60 秒で回避している。
- [BUG-028](../../bugs/BUG-028.md): 閉じた loopback ポートへの `connect()` が返らない。
- 範囲外のまま: ネイティブビルド、配布形式・依存解決・アップグレード機構、
  リポジトリサーバ、clang セルフホスト、OpenSSL の完全な test suite。

## 5. このレビューでしていないこと

各Phaseは実装前に `plan/coding-style.md` と Guardrail の全文を読む境界で実行したが、
WS 終了時点で改めて全文と実装を一行ずつ突き合わせる監査はしていない。上流
（LLVM・OpenSSL・OpenSSH）のソースはこちらの規約の対象外で、当てたパッチだけが対象。
独立した第三者レビューは受けていない。
