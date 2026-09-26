# ws001-p024: 実行系の utility（xargs・time・nohup・env・pwd）

Status: cleared（2026-09-27）
Parent: [WS001](../ws.md)
Queue: なし（2026-09-27 ユーザー指示のサブエージェントの作業。main session が merge する）

## 目的

台帳（ws.md §12）の #152 xargs、#122 time、#89 nohup、#39 env、#99 pwd を XCU（POSIX.1-2024）の要求に合わせる。
どれも utility を子として、または自分の代わりに起動する utility で、状態 126/127 と PATH の探索の扱いを揃える。

## 範囲

- `xargs`: `-0`・`-E`・`-I`・`-L`・`-n`・`-p`・`-r`・`-s`・`-t`・`-x`。引用（`'`・`"`・`\`）、論理行と行末の空白による継続、
  `-I` の行単位と置換、`-s` の大きさ（utility 名と各引数を null 終端で数える。既定は ARG_MAX − 2048 − 環境、上限 128 KiB）。
  状態: utility が 1〜254 → 最後に 123、255 → 124 で即停止、signal → 125、起動できない 126、無い 127。utility の標準入力は `/dev/null`。
- `time`: `-p` の書式（`real`・`user`・`sys`）、`wait4` による子の CPU 時間、状態（utility の状態、126/127、signal は 128+n）。
- `nohup`: SIGHUP の無視、stdout が端末なら `nohup.out`（無理なら `$HOME/nohup.out`、mode 0600、追記）、stderr の行き先、
  端末の stdin を `/dev/null` に、状態 126/127（nohup 自身の失敗も 127）。
- `env`: 新しい環境の PATH で探す、`#!` の無い file を sh で動かす（execvp）、stdout の書き込みの失敗、状態 125/126/127。
- `pwd`（新設、`/bin/pwd`）: `-L`（既定）・`-P`、`$PWD` の妥当性（絶対・`.`/`..` を含まない・同じ directory）。operand は無視する（GNU と shell の builtin と同じ）。

範囲外: GNU の長い option（WS045 の方針に従う）、`xargs` の LC_MESSAGES の yesexpr（POSIX locale の `y`/`Y` だけ）。

## 受け入れ条件

1. host の差分試験（GNU の POSIX mode と比較）: `plan/tools/utils/cases/{xargs,time,nohup,env,pwd}.sh` が全件一致。
2. 端末が要る case（`nohup` の `nohup.out`、`xargs -p`）: `plan/ws001/tests/tty-host-test.py` が全件一致。
3. 5 つの source が `plan/tools/style-check.py` で違反 0。
4. amd64 の target build（`make ... build/<dir>/bin/<utility>`）が warning 0。
5. amd64 guest で export した case を流して一致（`plan/ws001/tests/guest-run.sh`）。

## 記録

### 変更

- `userland/base/xargs/main.c`: 書き直し。全 option、引用、論理行、`-I` の置換、状態 123/124/125/126/127、子の stdin を `/dev/null`、
  exec の失敗を CLOEXEC の pipe で親へ伝える（utility 自身の 126/127 と区別する）。
  大きさは `-s` の数え方（文字列と null）と kernel の数え方（加えて各 pointer と終端の null pointer）の両方で制限する。
  zedBSD の `ARG_MAX` は 16384（`include/uapi/limits.h`）で kernel は pointer の表も数えるため、文字列だけで数えると
  guest で `E2BIG` になった（guest の試験で発見）。
- `userland/base/time/main.c`: 書き直し。`-p`、`wait4` による user/sys の CPU 時間、SIGINT/SIGQUIT を無視して待つ、状態 126/127・128+n。
- `userland/base/nohup/main.c`: 書き直し。`nohup.out`・`$HOME/nohup.out`（0600・追記）、stderr の行き先の規則、端末の stdin を `/dev/null`、状態 126/127。
- `userland/base/env/main.c`: 新しい環境を `environ` にしてから `execvp`（新しい PATH で探す、`#!` の無い file を sh で動かす）。
  option の読み取りを関数に分け、`-i` の繰り返しと `--`、stdout の書き込みの失敗（状態 1）、自身の失敗 125。
- `userland/base/pwd/`（新設）: `-L`・`-P`。`config/ci/config-{amd64,pcat,pc98,intelmac}.mk` の program の一覧に `pwd` を足した。
- `userland/base/sh/builtins.c`: **`env` の builtin を削除した**（`/usr/bin/env` が走る）。builtin は `-i` を 1 回しか読まず、`--` を
  扱わず、`#!` の無い file を動かせず（126）、guest の env の case が 4 件落ちた。POSIX は env を builtin にすることを求めない。
  sh の差分試験（`plan/tools/sh/sh-diff.py`、oils は main tree の `build/ws042/oils` を読むだけ）: 変更前 1435/1458、変更後 1437/1458。
  差は時間に依る case（`noclobber on &>> >>`、`$0 with -c/-i/stdin`）で env と無関係。

### 試験（2026-09-27）

| 確認 | 結果 |
| --- | --- |
| host の差分試験 `python3 plan/tools/utils/util-diff.py --bin build/ws001/bin --only <u>`（build は `sh plan/ws001/tests/build-host-ws001.sh`） | xargs 54/54、time 10/10、nohup 9/9、env 18/18、pwd 11/11 |
| 端末の case `python3 plan/ws001/tests/tty-host-test.py` | 7/7（nohup.out の作成・追記・mode、HOME への退避、開けないときの 127、stdin、`xargs -p`） |
| amd64 guest（QEMU、KVM、NVMe 起動、serial console）`sh plan/ws001/tests/guest-run.sh build/ws001/guest-p024.out xargs time nohup env pwd` | 102/102（最初は 97/102: sh の env builtin の 4 件と、xargs の case の誤り 1 件。case は `$0` を数えていなかった） |
| style `python3 plan/tools/style-check.py`（5 file） | 違反 0 |
| amd64 の target build（`make ZEDBSD_CONFIG=config/ci/config-amd64.mk BUILD=build/ws001-amd64 ... build/ws001-amd64/bin/<u>`）と guest image の build | warning 0 |
| 実機 | 未実施 |

guest の image は `plan/ws001/tests/config-amd64-lean-guest.mk`（CI の amd64 から clang・libcxx・remacs・OpenSSH・OpenSSL・GPU の demo・firmware を
除き、serial mirror を有効にした構成）。worktree の新しい build でも 1 分以内に作れる。host の Noct は main tree の
`build/NoctLang/build-static/noct` を読むだけで使う。

### 残り

- `xargs -p` の yesexpr は POSIX locale の `y`/`Y` だけ（LC_MESSAGES の locale の対応は LIBC-LOCALE-01 の後）。
- `time` の既定（`-p` 無し）の書式は `-p` と同じ（POSIX は未規定）。
- `pwd -L` の `PATH_MAX` を超える `$PWD` は未試験。
- 観察: `ARG_MAX` が 16 KiB と小さい（POSIX の最小 4096 は満たす）。長い command line を作る script（configure の link 行など）で
  `E2BIG` になりうる。kernel の範囲なので WS001 では変えない（記録のみ）。
