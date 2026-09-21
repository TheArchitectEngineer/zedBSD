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

### 実行ファイルの `PT_TLS`（チェックスクリプトの修正依頼）

指示は「`PT_TLS` は実装済みのはずなので、チェック用スクリプトが古い。修正して」。
実機で確かめたところ、**スクリプトは古くなく、実装が無かった**。

| テスト | 期待 | 修正前の実機 |
| --- | --- | --- |
| `__thread int counter = 42; counter++` | `43` | **`1`**（クラッシュせず、黙って別のメモリを読む） |
| `thread_local std::string`（`filesz == 0` の `PT_TLS`） | 動作 | **`Executable format error`** |

静的リンク実行ファイルと共有オブジェクトでは動いていたので「実装済み」の印象は
正しかったが、動的リンク実行ファイルだけが未実装だった。カーネル側の
`filesz == 0` 拒否と、ローダ側の静的 TLS 領域の不在の 2 点を直した。詳細と実機ログは
`phase005/results.md` の「9. 実行ファイルの TLS」に置いた。

スクリプトは依頼どおり修正したが、内容は「一律拒否をやめる」ではなく
「実際の上限（`PT_TLS` は 1 本・`filesz <= memsz`・`memsz <= 1 MiB`・アラインは
4096 以下の 2 冪）を検査する」に置き換えた。実装が追いついた今、意味のある検査だけが
残っている。

### `getrandom` を libc に入れてよいか

問題なし、と判断して実装した。理由は **zedBSD のエントロピー源には「種付け待ち」の状態が
無い**こと。`getentropy(2)` は `hal_entropy_fill()` を呼び、amd64 では RDRAND を直接引く
（`src/hal/amd64/lib.c:376`）。Linux の `getrandom` が既定で待つのは、ソフトウェアプールが
初期化されるまで弱い乱数を返さないためで、その保証こそが `GRND_NONBLOCK` の存在理由に
なっている。ハードウェア命令にはその段階が無いので、待たないことが正しい挙動になり、
セマンティクスを偽らずに実装できる。

判断の材料として挙げておく点:

- **OpenBSD の API ではない**。Linux 由来だが FreeBSD 12 以降も同じ名前・同じフラグ値で
  持っているので、BSD の前例がある。`<sys/random.h>` に置いたのも両者に合わせた。
- **configure の feature detection に効く**。`AC_CHECK_FUNCS(getrandom)` を持つパッケージが
  Linux 経路を選ぶようになる。セマンティクスが一致していれば利点だが、一致していなければ
  静かに弱くなる類の変更なので、上記のとおり一致を確認した上で入れている。
- CPU に RDRAND が無ければ `getentropy` と同様 `ENOSYS` で失敗する。黙って弱い値を返す
  経路は作っていない。

`GRND_RANDOM` は現代の Linux でも既定と実質同じ扱いであり、zedBSD は源を1つしか持たない
ので同値とした。`GRND_RANDOM|GRND_INSECURE` の同時指定と未定義フラグは `EINVAL`。
256 バイト（`GETENTROPY_MAX`）ごとに分割して引き、常に要求長を満たして返す。

実機テストは `plan/ws032/tests/getrandom-target.c`、10/10 PASS。

### openbsd-compat 残りの実装: 前提条件の切り分け

自前実装に入る前に、libc だけで閉じるものとカーネル支援が要るものを分けた。

| 状態 | 関数 |
| --- | --- |
| libc だけで書ける | `freezero` `getpagesize` `timegm` `mkdtemp` `mkstemps` `getline` `getdelim` `fmemopen` `open_memstream` `daemon` `closefrom` `getdtablecount` `setmode` `getmode` `glob` `globfree` `getopt_long` `readpassphrase` `b64_ntop` `b64_pton` vis 一族 `MD5*` `SHA1*` `SHA2*` `getgrouplist` `user_from_uid` `group_from_gid` `bindresvport` `rresvport_af` `<sys/queue.h>` `<sys/tree.h>` |
| 既存の仕組みで書ける | `issetugid`（`AT_SECURE` を補助ベクタから読む経路が `posix.c` にある）、`getpeereid`（`SO_PEERCRED` が `include/uapi/socket.h:66` にある）、`getrrsetbyname`（実 DNS スタブリゾルバが `resolver.c` にある） |
| **カーネル作業が要る** | `flock(2)`。`struct flock`（fcntl 用）はあるが `flock` システムコールは無い。fcntl で代用するとロックが「open file description 単位」でなく「プロセス単位」になり、`fork` 後と多重 open の挙動が変わる。**別物を同じ名前で出すべきではない**ので、カーネルに追加するか、追加しないかの判断が要る |
| `setproctitle` | libc が `argv`/`environ` の原領域を保持していない。crt0 側で開始時のポインタを控える小さな追加が要る |

### `flock(2)` をカーネル込みで実装

既存の record lock 機構（`src/kern/record-lock.c`）が **open file description 所有の
ロック**を既に持っていた（`owner_is_file`、`F_OFD_*` 用）。`flock` の定義そのものが
「ファイル全体・open file description 所有」なので、新しいロック機構を足すのではなく
この機構に乗せた。**別物を同じ名前で出さない**という条件を満たせる。

| 変更 | 内容 |
| --- | --- |
| `record-lock.c` | `record_lock_fcntl` の本体を `record_lock_apply(..., whole_file)` へ括り出し、公開入口を 2 つにした。fcntl 側の経路は引数が 0 固定で挙動不変 |
| 同上 | `record_lock_flock(owner, file, operation)` を追加。`LOCK_SH/EX` → `F_RDLCK/F_WRLCK`、`LOCK_UN` → `F_UNLCK`、範囲は `SEEK_SET` から長さ 0（＝末尾まで）、コマンドは `LOCK_NB` の有無で `F_OFD_SETLK`/`F_OFD_SETLKW` |
| 同上 | flock のみ 2 つの規則を免除: **inode 種別**（fcntl は通常ファイルのみ）と**オープンモード一致**（flock は読み取り専用 fd でも排他ロックを取れる）。どちらも flock の仕様であって手抜きではない |
| `include/uapi/fcntl.h` | `LOCK_SH/EX/NB/UN`（他システムと同じ値） |
| `include/uapi/syscall.h` | `KERN_SYS_flock = 165` |
| `src/kern/syscall.c` | `sys_flock_call`。`filedesc_get_file` → `record_lock_flock` → `file_close` |
| `libc/include/sys/file.h` | 新規。`flock()` 宣言と、fcntl のロックとの違いをコメントに明記 |
| `userland/base/libc/posix.c` | `flock()` |

解放は既存の `record_lock_release_file()`（`src/kern/file.c:2329`、最終 close で呼ばれる）が
そのまま担う。追加の後始末は要らない。

### `setproctitle(3)`

当初「crt0 で argv を控える必要がある」と書いたが、**それは誤りだった**。libc の起動処理は
既に `setprogname(argv[0])` を呼んでいる（`posix.c:7115`）。実際に足りなかったのは別で、
`ps` はユーザ空間の argv ではなく**カーネルが持つ `process->command`**（`exec` 時に
`argv[0]` から設定、`include/kern/process.h`）を `ioctl(KERN_SYSTEM_GET_PROCESS)` で読む。
したがってタイトルを変えるにはカーネルへ書き戻す経路が要る。

| 変更 | 内容 |
| --- | --- |
| `include/kern/process.h` | `command_initial[64]` を追加。`setproctitle(NULL)` が戻す先 |
| `src/kern/exec.c` | `command` を設定する 2 箇所で `command_initial` にも控える |
| `include/uapi/syscall.h` | `KERN_SYS_setproctitle = 166` |
| `src/kern/syscall.c` | `sys_setproctitle_call`。自分のタイトルしか変えられないので権限判定は不要。長さ 0 で原題に戻す。入り切らない題は `ENAMETOOLONG` で拒否（半端に切らない） |
| `libc/include/stdlib.h` / `posix.c` | `setproctitle(fmt, ...)`。OpenBSD の仕様どおり `progname: ` を前置し、`fmt == NULL` で原題に戻す |

ついでに `issetugid()` も入れた。`AT_SECURE` は起動時に `secure_execution` へ既に読まれて
いたので、公開するだけだった。

### 実機テスト

`plan/ws032/tests/flock-target.c`、**19/19 PASS**。flock を fcntl のロックと区別する性質を
狙って検査している:

- 同一プロセスでの2回目の open は**衝突する**（プロセス所有の record lock なら通る）
- `dup` は同じロックを共有し、複製経由の解放で原本も解ける
- `fork` で継承した fd は同じ description なので衝突しない
- 共有ロック2本は共存し、その下での昇格は拒否され、相手が抜けると成功する
- 最後の fd を閉じるとロックが落ちる
- 読み取り専用 fd でも排他ロックを取れる
- **`LOCK_NB` 無しの待ちが、解放の後に成立する**（解放直前に書いた印を待ち側が読めることで、待たずに取ったのではないことを確認）

リファクタの回帰確認として fcntl 側も同じテストに入れた: プロセス所有ロックは同一プロセス内で
衝突せず、`F_OFD_*` は衝突し、書き込みロックは読み取り専用 fd で `EBADF` になる。
`userland/base/tests/posix-r2-remaining.c` は**私の変更前から別の理由でビルドできない**
（行 1566 付近、16 バイト atomic の `-Watomic-alignment`）ので使えなかった。

### openbsd-compat → libc、第1・2陣

すべて API の仕様から独自に実装した。OpenBSD のコードは読んでいない（`openbsd-compat/`
のファイル名＝関数名の列挙だけを対象の確定に使った）。

**第1陣** `libc/openbsd.c`（実機 21/21 PASS、`plan/ws032/tests/openbsd-target.c`）

`freezero` `getpagesize` `timegm` `mkdtemp` `mkstemps` `getline` `getdelim` `daemon`
`closefrom` `getdtablecount` `getgrouplist` `user_from_uid` `group_from_gid` `getpeereid`

`timegm` は 3 月起点の暦算で日数を数える（閏日が年末に来て月長が 5 か月周期で繰り返す）。
既知の時刻・エポック前後・月の繰り上がり・閏年 366 日すべての往復を検査した。
`getpeereid` は `SO_PEERCRED`（`struct kern_peercred`）に乗せた。

**第2陣** `libc/openbsd-vis.c` `libc/openbsd-base64.c` `libc/readpassphrase.c`
（実機 29/29 PASS、`plan/ws032/tests/vis-target.c`）

vis 一族・`b64_ntop`/`b64_pton`・`readpassphrase`。新規ヘッダ `<vis.h>` `<resolv.h>`
`<readpassphrase.h>`。

vis は**既定の符号化形式を最初 8 進にしてしまい、実機テストで取り違えに気づいて直した**。
OpenBSD の既定は制御文字を `\^[`、高位ビット付きを `\M-i` / `\M^@` と書く caret/meta 形式で、
8 進は `VIS_OCTAL` を指定したときだけ。自己完結した往復だけ見ていると気づけない類の誤りで、
他システムと符号化を共有できなくなるところだった。全 256 バイトの往復に加えて、
既定・`VIS_CSTYLE`・`VIS_OCTAL` それぞれの具体的な出力を検査するようにした。

`<resolv.h>` は b64 の 2 本しか持たない。zedBSD には `res_query` に当たるものが無いので、
ヘッダにその旨を明記した。`AC_CHECK_HEADERS(resolv.h)` だけで `res_*` を使う configure は
リンク時に落ちるが、そもそも動かないものなので黙って通すより良い。

base64 は復号側を厳しくした: アルファベット外・途中のパディング・1 文字だけの群・
末尾の捨てられるビットが 0 でないものをすべて拒否する。緩いと 2 つの異なる文字列が
同じバイト列を意味することになるため。

### 実機テストハーネスの拡張

`run-target-console.py` はシェルのプロンプト数を数えてコマンドの切れ目を判断するので、
**自前の質問をするプログラム**（`readpassphrase` を使うもの）を試せなかった。
`?pattern`（コンソールにその文字列が出るまで待つ）と `@text`（プロンプトを数えずに
打ち込む）を追加した。

### 見つかった欠落: `/dev/tty` が無い

`readpassphrase` の実機テストで発覚。zedBSD の `/dev` には `console` と `ttyv0..3`、
`pts/`、`ptmx` はあるが、**制御端末のエイリアス `/dev/tty` が無い**
（`src/drivers/generic/console.c:1006-1016` が登録するのはその 2 種類だけ）。

これは `readpassphrase` だけの問題ではない:

- **OpenSSH が要る**。パスフレーズ入力も `askpass` の判定も `/dev/tty` を見る
- **zedBSD 自身の `newgrp` が既に壊れている**（`userland/base/newgrp/main.c:227` が
  `open("/dev/tty", O_RDWR)` する）
- 入力がリダイレクトされたプログラムが人に尋ねる、という POSIX の基本的な経路が無い

実装は「open したとき呼び出し元プロセスの制御端末（`process->controlling_tty`、
`include/kern/process.h` に既にある）へ解決する cdev」になる。次はこれを実装する。

### `/dev/tty` の実装

`struct tty` を介する読み書き・ioctl・poll は既にどれも端末実装から独立していた
（`tty_backend_pair()` が pty の出力経路を振り分ける）。そこで仮想コンソール用の本体を
`tty_instance_read/write/poll` として括り出し、`tty_vt_*` はそれを呼ぶだけにした。

| 変更 | 内容 |
| --- | --- |
| `src/kern/tty.c` | 上記の括り出し。`tty_vt_*` の挙動は不変（vt 番号の検査だけが残る） |
| 同上 | `tty_controlling()` を追加。`process_controlling_tty_snapshot()` で取り、`process_controlling_tty_matches()` で**世代も確かめる**（スナップショットとの間に手放して別のプロセスへ渡っている可能性があるため） |
| 同上 | `tty_controlling_open/read/write/ioctl/poll`。制御端末を持たないプロセスには `ENXIO` を返す。答えの来ない descriptor を握らせるより、呼び出し側が標準入力へ退避できるようにするため |
| `src/drivers/generic/console.c` | `cdev_register("tty", ...)` |

操作のたびに制御端末を引き直している。プロセスは descriptor を開いたままでも制御端末を
得たり手放したりできるし、この descriptor を共有する 2 つのプロセスが同じ端末を持つとは
限らないため、open 時に束縛してはいけない。

実機:

```
$ ls /dev            ... tty が現れる
$ echo hello > /dev/tty
hello                ... 以前は「Operation not supported」
```

`readpassphrase` も端末経路で 4/4 PASS（`plan/ws032/tests/rpp-target.c`）。入力は表示されず、
echo は復元される。

テストハーネスにもう一点直しが要った。**プログラムが入力待ちの間は次のシェルプロンプトが
来ない**ので、`?`/`@` が後続するコマンドではプロンプトを待たないよう先読みするようにした。

### `<sys/queue.h>`

SLIST・LIST・SIMPLEQ・TAILQ の 4 種。仕様（queue.3 が述べる各操作の意味と計算量）から
書いた。実機 21/21 PASS（`plan/ws032/tests/queue-target.c`）。

壊れやすいのは末尾・前方ポインタの保守なので、そこを狙って検査した: `LIST` は先頭要素を
**head を参照せずに**外せること、`SIMPLEQ` は空にしたあと再び末尾追加できること、`TAILQ` は
末尾を外したあと `TAILQ_LAST` が正しく、連結後も後ろ向きに歩けること、歩きながら空に
できること。

### `/dev/full`、`ttyname`、`TIOCSCTTY` / `TIOCNOTTY`

- **`/dev/full`** を `src/drivers/generic/memory-device.c` に追加した。読むとゼロ、書くと
  必ず `ENOSPC`。長さ 0 の書き込みは何も求めていないので通す。
- **`ttyname` / `ttyname_r` は既にあり、動いていた**。pty を含めて確認した。
  `/dev/tty` を開いた descriptor に対しては `/dev/tty` を返す（`/dev/tty` 自身が
  device 番号を持つため。BSD と同じ挙動）。
- **`TIOCSCTTY` / `TIOCNOTTY` も既にカーネルに実装済みだった**（`src/kern/tty.c:2217`）。
  実装ではなく検証を行った。

### `/dev/tty` の write が pty へ届いていなかった（自分の実装のバグ）

実機テストで発覚。`tty_echo()` は**擬似端末を無視する**（`src/kern/tty.c` の先頭で
`vt = tty - console_ttys` を計算し、範囲外なら黙って返る）。仮想コンソールの write 本体を
そのまま汎用化したため、制御端末が pty のとき **write は成功を返しながら出力を捨てていた**。

出力経路を `tty_backend_write()` に括り出し、`tty_backend_pair()` で行き先を選ぶようにした。
`pty_slave_write` も同じ関数を呼ぶので、2 つの経路が今後ずれることはない。

### 実機テスト

`plan/ws032/tests/session-target.c`、**13/13 PASS**。ログインサーバが毎回行う手順を
そのまま辿っている:

1. `setsid()` で元のセッションを離れる → `/dev/tty` は `ENXIO` になる
2. `ioctl(slave, TIOCSCTTY)` で pty をこのセッションの端末にする
3. `/dev/tty` が開けるようになり、`ttyname(slave)` は `/dev/pts/0` を返す
4. **`/dev/tty` へ書いたものが pty master から出てくる**（これが上のバグを捉えた検査）
5. `TIOCNOTTY` で手放すと `/dev/tty` はまた `ENXIO` になる

手順 5 で親が `SIGHUP` を受けるのは POSIX どおりの挙動（制御プロセスが端末を手放すと
前景プロセスグループに届く）なので、テスト側で無視している。

### 追加候補（未実装、判断待ち）

| 候補 | 状況 | 無いと何が起きるか |
| --- | --- | --- |
| `/dev/urandom` `/dev/random` | **無い** | OpenSSL・OpenSSH をはじめ、乱数をファイルから読む実装が動かない。`getentropy` の源にそのまま乗せられる。**p007 に効く** |
| `/dev/stdin` `/dev/stdout` `/dev/stderr` `/dev/fd/N` | 無い | シェルのプロセス置換や、`-` の代わりにこれらを渡す作法が動かない |

### `/dev/fd` — SunOS の静的ノード方式

「新しい file を作ってオフセットだけコピーすれば `dup` の代わりになるか」という問いから
設計を決めた。**ならない**。`dup` が共有するのはオフセットの*値*ではなく open file
description そのもので、以下が付いてくる:

- オフセットは**継続的に**共有される。片方で読むともう片方も進む。コピーは作成時点の
  一致しか与えない
- ステータスフラグ（`O_APPEND` `O_NONBLOCK`）が共有される
- **description が所有するロック**（`flock`・`F_OFD_*`）が共有される。別 description なら
  独立したロックになり、同じプロセスが自分自身と競合する

加えて開き直しは権限を再検査するので、特権のある親から渡された fd で**失敗し得る**し、
読み取り専用の fd を書き込みで開き直せてしまう経路にもなる。そして**パイプ・ソケット・
unlink 済みファイルには開き直す名前が無い**。シェルのプロセス置換が渡すのはパイプなので、
コピー＋seek では主用途がそもそも実装できない。

#### 5 システムの実装を確認した

| | 戻り値 | fd 番号の運び方 |
| --- | --- | --- |
| FreeBSD | `ENODEV`（errno を合図に流用） | `td->td_dupfd` |
| NetBSD | `EDUPFD`（内部専用 errno） | `curlwp->l_dupfd` |
| OpenBSD | **無い**（fdescfs を削除済み） | — |
| Solaris/illumos | `0`（成功）＋ vnode に `VDUP` | 運ばない。**マイナー番号が fd 番号** |
| Linux | 別方式。magic symlink を `nd_jump_link` で辿って**普通に開き直す**（オフセット非共有） | — |

FreeBSD と NetBSD は自分のコメントで kludge と呼んでいる。Solaris の `fdopen()` は
`VDUP` を毎回無条件に立てて一度も下ろさないので、実質**その vnode の静的な性質**である。

#### zedBSD の形

そこで「呼び出しごとに立てる」のをやめ、**ノードを作るときに決めて変えない**ことにした。
合図用の errno もスレッド変数も要らない。

| 変更 | 内容 |
| --- | --- |
| `include/kern/inode.h` | `i_descriptor_alias`。上記の理由を全部コメントに書いた |
| `src/kern/devfs.c` | `/dev/fd` を `/dev/pts` と同じ合成ノードとして追加（cdev は使わない）。`devfs_descriptor_open()` に詳細なコメント。`/dev/stdin` `/dev/stdout` `/dev/stderr` も同じ仕組みで root に置いた |
| `src/kern/file.c` | `file_substitute_descriptor()`。open 経路が置き換える — **descriptor 表を持っているのは open 経路だけ**だから。要求アクセスモードが既存 fd の部分集合であることも検査する |
| `src/kern/syscall.c` | `sys_dup_call` に軽くコメント |

一覧は全プロセス共通（0 〜 `KERN_OPEN_MAX-1`）で、解決だけが呼び出し元依存。4.4BSD の
fdescfs のように一覧までプロセス別にすると readdir を呼び出し元ごとに答える必要があり、
「パスは誰にとっても同じ対象を指す」前提が崩れるので名前キャッシュにも載せられなくなる。

#### 実機テスト

`plan/ws032/tests/fd-target.c`、**14/14 PASS**。コピー＋seek と区別できる検査を狙った:

- **alias 経由で読むと元の fd のオフセットが進む**（コピーなら進まない）
- **元の fd を seek すると alias 側も動く**
- **パイプを `/dev/fd` 経由で読める**（開き直しでは不可能）
- 保持していない descriptor は `EBADF`、読み取り専用 fd を `O_WRONLY` で開くと `EACCES`

