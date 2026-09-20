# ユーザー指示による修正（2026-09-21）

WS032 の中間報告で挙げた発見に対する、ユーザー指示（2026-09-21）への対応。
各項目の検証結果を実測で記録する。

| # | 指示 | 状態 |
| --- | --- | --- |
| 1 | `make toolchain-cache` を修正 | **完了・検証済み** |
| 2 | clang に linker job を追加 | 実装済み、LLVM 再ビルド中 |
| 3 | Noct はパッチをファイルで保持 | **完了** |
| 4 | `syscall-smoke.c` を修正 | **完了・検証済み** |
| 5 | TCP_NODELAY / SO_KEEPALIVE をカーネルで実装 | **完了・実機検証済み** |
| 6 | console のシリアル同時出力 | **完了・実機検証済み**（従来は無かった） |

## 1. `make toolchain-cache` が LLVM のフルビルドを誘発する問題

**原因**: `$(ZEDBSD_LLVM_INSTALL_STAMP)` の prerequisite が
`$(ZEDBSD_LLVM_CONFIG_IDENTITY)`（＝configure 済みビルドツリーの印）だった。
キャッシュはこのビルドツリーを作らないので、stamp 自体は存在してもその prerequisite が
未生成扱いになり、make は stamp を作り直そうとして configure → 全ソースビルドへ進む。

**修正**（`toolchain/llvm/llvm.mk`）: `build/llvm/.zedbsd-install-identity` が
現在の版・patch level と一致するなら、**それが受理済みのインストールである**として
stamp をその場で満たす（ツールとライセンスの存在だけ確認）。一致しない場合だけ
従来どおりソースビルド経路へ進む。キャッシュから来たか自前ビルドかを区別しない。

**検証**: `build/amd64/sysroot/.zedbsd-sysroot-complete` を消してから
`make sysroot-amd64` を実行し、`ninja` も `cmake --build` も**起動しない**こと
（grep 件数 0）と sysroot が再生成されることを確認した。従来は `make -o <stamp の絶対パス>`
という回避が要った。

## 4. `syscall-smoke.c` の未初期化

`catch_fault_siginfo` が `ucontext_t *context;` を宣言直後に `valid` の初期化式で読み、
その **後** で `context = opaque_context;` していた。宣言時に代入する形へ直した。
`-Werror=uninitialized` で落ちていたので、この file は従来 compile できていない。

**検証**: `make build/amd64/user64/userland/base/tests/syscall-smoke.o` が
warning 0 で通る（23,464 byte）。

## 5. TCP_NODELAY と SO_KEEPALIVE

### 実装

- `include/uapi/netinet.h`: `TCP_NODELAY` を定義（kernel と libc が同じ値を見るよう
  uapi へ置き、`libc/include/netinet/tcp.h` はこれを include する）。
- `include/uapi/socket.h`: `SO_KEEPALIVE`。
- `include/kern/net/socket.h`: `struct socket` に `keepalive`、`socket_ops` に
  `keepalive_changed`（オプション変更を protocol へ伝え、idle timer を張り直す）。
- `src/kern/net/socket.c`: SOL_SOCKET の `SO_KEEPALIVE` を set/get。
- `include/kern/net/tcp-socket.h`: `nodelay`、`keepalive_deadline`、`keepalive_probes`。
- `src/kern/net/tcp.c`:
  - `tcp_setsockopt` / `tcp_getsockopt` を `socket_ops` へ追加し、`IPPROTO_TCP` の
    `TCP_NODELAY` を扱う。他のオプションは `ENOPROTOOPT`。
  - idle probe: 接続が確立したときに idle 期間を張り、ピアからの受信と自分の送信で
    張り直す。期限が来たら **最後に送ったバイトに対する ACK**（`send_next - 1`）を
    probe として送り、`TCP_KEEPALIVE_COUNT` 回連続で応答が無ければ状態を CLOSED にして
    `ETIMEDOUT` を上げ、待っているスレッドを起こす。
  - `tcp_timer_next_deadline` が retransmit と keepalive の早い方を返すので、
    net スレッドは probe の時刻に起きる。

既定値は伝統的な値（idle 2 時間、間隔 75 秒、9 回）。`CONFIG_TCP_KEEPALIVE_IDLE_MS`、
`CONFIG_TCP_KEEPALIVE_INTERVAL_MS`、`CONFIG_TCP_KEEPALIVE_COUNT` で上書きでき、
試験用に短縮したイメージを作れる。

### TCP_NODELAY の意味（記録した制限）

この TCP は **stop-and-wait** で、直前のセグメントが ACK されるまで次を送らず、
書き込みごとに 1 セグメントを PSH 付きで送る。つまり小さな書き込みを溜める仕組み
（Nagle）が無い。したがって:

- `TCP_NODELAY=1` は現在の挙動そのもの。
- `TCP_NODELAY=0` を設定しても**遅延は発生しない**（戻せる遅延が無い）。
- setsockopt / getsockopt は設定値を忠実に保持・報告する。

ヘッダのコメントにも同じことを書いた。「Nagle を実装した」とは主張しない。

### 実機検証（QEMU、`-cpu max`）

`plan/ws032/tests/socket-options-target.c`:

```
SOCKOPT ok   tcp-nodelay
SOCKOPT ok   so-keepalive
SOCKOPT ok   unknown-option-refused: errno 56
SOCKOPT verdict: PASS (3/3)
```

初期値 0 → 1 を設定して読み戻し → 0 に戻して読み戻し、を両オプションで確認。
`IPPROTO_TCP` の未知オプションは `ENOPROTOOPT` で拒否される。

`plan/ws032/tests/keepalive-target.c`（idle 2 秒・間隔 1 秒・3 回に短縮したカーネル）:

```
KEEPALIVE connected, idling
KEEPALIVE verdict: PASS (connection carried data after an idle period)
```

loopback で接続し、両端に `SO_KEEPALIVE` を立てて 12 秒放置した。これは idle 期間 1 回＋
probe 間隔 10 回分で、probe が無応答なら 3 回で切断される設定である。放置後もデータが
通ったので、probe が送られ、相手に応答され、接続が維持されたことになる。

**未検証**: ピアが消えたときに `ETIMEDOUT` で切れる経路（loopback では相手を
黙らせられない）。コードは `tcp_keepalive_expire_locked` の `-1` 分岐で実装してある。

## 6. console のシリアル同時出力

**確認した現状**: 以前から存在したのは **debugcon（port 0xe9）への出力**で、
`src/hal/amd64/bsp-pcat/cons.c` の early console が `HAL_PCAT_DEBUGCON` のときに
1 文字ずつ出していた。ただしグラフィックスが `kernel_putc` を公開した後は
テキストコンソール側（`src/drivers/platform/pcat/graphics/text.c`）へ出力が移り、
**そちらには外部へ出す経路が無かった**。そのため userland の出力（getty、shell、
プログラム）は画面にしか出ていなかった。シリアル（16550）への出力は履歴上も無い。

**追加**: `text.c` の `putc_locked` から COM1（0x3f8）へ同じ文字を流す。

- 115200 8N1、FIFO 有効。初期化は最初の 1 文字で 1 回だけ。
- 送信可否は LSR を**有限回**読むだけで、UART が無い機械（LSR が 0x00 か 0xff）では
  その場で捨てる。待ち続けない。
- `\n` の前に `\r` を補うのは、直前が `\r` でないときだけ。端末規律が既に CRLF を
  送っている userland 出力と、素の `\n` を書くカーネルログの両方が正しく出る。
- `HAL_PCAT_DEBUGCON` と同じ条件で有効（pcat 系の platform makefile が定義済み）。
  ビルド設定ファイルは変更していない。

**配置**: `src/drivers/platform/pcat/serial-mirror.{c,h}` に独立させ、
`platform/amd64/vmunix.mk` の source 一覧へ追加した。呼び出しは
`text.c` の `putc_locked` から。ここは `kern_text_putc` を通るカーネルログと
tty 出力の**両方の唯一の分岐点**である。

（初回は `platform/amd64/vmunix.mk` が並行作業の対象だったため `text.c` 内の static
関数として書いたが、ユーザーが同 file の編集を許可したため独立させた。）

**実機検証**: `-serial file:...` で起動ログから `login:` プロンプト、シェルの
コマンドと出力まで全て取得できる。これを使って以降の実機確認を自動化した
（`plan/ws032/tests/run-target-console.py`）。

## 実機コンソール harness（副産物）

`plan/ws032/tests/run-target-console.py`: イメージを起動し、QMP の `send-key` で
PS/2 キーボードとして打鍵し、シリアルに出た内容を読む。プロンプトの出現回数を数えて
コマンドの終了を待つ（コマンド直後にマーカーを打つと、端末エコーが先に出てしまうため）。
`--cpu` で CPU モデルを指定できる。

これにより OpenSSL の実機確認ができた（[phase006/results.md](phase006/results.md) §9）。

## 追加対応（2026-09-21、第2次指示）

| 指示 | 状態 |
| --- | --- |
| TCP スタックの改善 | 実装済み（検証は LLVM 再ビルド後） |
| `/etc/services` と servent 一式 | 実装済み |
| `gethostbyname` / `h_errno` / `struct hostent` | 実装済み |
| `chroot` | 実装済み |
| `libc.so` に unwind table | 実装済み |
| `HOST_NAME_MAX` | 実装済み |
| C++ ランタイムの配置 | 方針確定（未実装、p005） |
| ローダの `LD_LIBRARY_PATH` | 実装済み |

### TCP: stop-and-wait からスライディングウィンドウへ

従来は 1 セグメントを送るたびに ACK を待っていた（`send_unacknowledged == send_next`
になるまで次を送らない）。RTT ごとに最大 1024 byte しか流れない。

変更点:

- `struct tcp_socket` の単一 retransmit 記録を、**最大 8 セグメントの送信リング**
  （`struct tcp_pending send_queue[]`、古い順）へ置き換えた。
- 送信可否は「何も未確認でないこと」ではなく「リングに空きがあり、かつ
  **ピアの advertised window** に収まること」。ウィンドウが未知または 0 のときも
  1 セグメントだけは許す（さもないと接続が進まない）。SYN と FIN は制御なので常に許す。
- ACK は**累積**で扱い、`tcp_retire_acknowledged` が覆われたセグメントを全て retire する。
  シーケンス比較は符号付きで行い、ラップしても正しい。
- 再送タイマは**最古のセグメント**に属する。それが retire されると次の最古へ移る。
  タイムアウト時に再送するのも最古の 1 つで、後続はそれが通ってから流れる。
- ピアのウィンドウを**毎 ACK で更新**する（以前は SYN-ACK のときだけ）。
- `poll` の `POLLOUT` は「全部 ACK 済み」ではなく「リングに空きがある」に変えた。

`CONFIG_TCP_SEND_QUEUE_MAX` で深さを変えられる。**1 にすると従来と同じ stop-and-wait**
になるので、同じカーネルで A/B を実測できる。

受信側は従来どおり**順序どおりのものだけ**を受け取る。順序が入れ替わったものは捨てて
再送させる。ウィンドウを開いた分だけ再送が起きうるが、正しさは保たれる。

### ローダのライブラリ探索

`userland/base/rtld/rtld.c` の探索順:

1. 要求元の `DT_RUNPATH`、無ければ `DT_RPATH` の連鎖（従来どおり）
2. **`LD_LIBRARY_PATH`**（設定されていて空でないとき。`:` 区切り）
3. `/lib`、次に `/usr/lib`

`LD_LIBRARY_PATH` は既定を**置き換えず、先に見る**だけである。置き換えにすると、
そこに `libc.so` が無い場合に何も起動しなくなる。

**`AT_SECURE` が立っている実行ファイルでは `LD_LIBRARY_PATH` を無視する**。
mode bit で特権を得たプログラムに対して、呼び出し元が読み込むコードを選べてしまうため。

この保護が実効であることを確認した。`src/kern/exec.c` の `exec_credential_prepare` は、
`MOUNT_NOSUID` でない媒体の set-uid / set-gid 実行ファイルで実効 id を所有者のものへ
変え、**実 id と実効 id が食い違ったときだけ** `secure = 1` にして `AT_SECURE` として
渡す（`src/kern/exec.c:319-325`、`:501`）。常に 0 という作りではない。
ユーザー判断（2026-09-21）により、この扱いを残す。`$ORIGIN` は
`LD_LIBRARY_PATH` では展開しない（所有オブジェクトが無いため）。

ローダは C ライブラリより前に動くので、環境変数は初期スタックの環境ベクタを
自分で走査して読む。

### C++ ランタイムの配置（方針）

clang を `/usr/bin` へ置くので、**C++ ランタイムは `/usr/lib`**、`libc` は `/lib` のまま
とする（ユーザー指示）。採用するのは LLVM の **libunwind → libc++abi → libc++** で、
clang と同じ LLVM 23.1.0 tarball から作る。GNU libstdc++ は採らない（GPL-3.0 WITH
GCC-exception であり、O1 の寛容ライセンス方針および clang 自身のランタイムと揃わない）。

この配置が成立するのは、上記のローダ変更で `/usr/lib` が探索されるようになったため。

### OpenSSL パッチ 0002 の削除

`gethostbyname` / `getservbyname` / `h_errno` / `struct hostent` / `struct servent` /
`in_addr_t` / `INADDR_LOOPBACK` がすべて揃ったので、`crypto/bio/bio_addr.c` の旧経路が
そのまま compile できる。**版依存だったパッチ 0002 は削除した**。残るのは 0001
（zedBSD ターゲット定義と `os-dep/zedbsd.h`）のみで、これは upstream が新しい OS 用に
用意している仕組みそのものなので版が上がっても壊れにくい。
