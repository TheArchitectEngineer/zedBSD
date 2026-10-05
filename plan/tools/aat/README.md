# AAT の道具（`plan/tools/aat/`、WS173 p003）

エージェントが host から SSH で zedBSD の desktop を人のように操作して受け入れを確かめる（Agent Acceptance Test）ための道具。
素の Latitude 5330（AAT の image を USB から起動）と、`plan/tools/guest/guest.py` の QEMU の guest（`--qemu`）に同じ命令が効く。

| file | 役目 |
| --- | --- |
| `aat` | CLI（python3）。下の命令 |
| `config-amd64-aat.mk` | AAT の image の config: UAT の config（`plan/ws159/tests/config-amd64-uat.mk`）＋試験の注入（`CONFIG_INPUT_TEST_INJECT`）＋撮影の口。製品の config には入れない |
| `build-image.sh` | AAT の image を作る（root の鍵と host 鍵は guest の harness の物、harness の `net.conf` は入れない） |
| `tests/run-host.sh` | host の自己試験（`--local`、偽の注入 `fake-inject.py` と偽の撮影 `fake-shot.py`） |

## 使い方

```sh
plan/tools/aat/build-image.sh build/aat                 # 5330 用（USB に書く: build/aat/hdd-image.img）
export AAT_TARGET=5330                                   # root@10.0.30.3（--target user@host[:port] でも）
plan/tools/aat/aat check                                 # 届くか、注入・撮影の命令と log があるか
plan/tools/aat/aat start                                 # 注入の daemon を起こす（画面の大きさは撮影から）
plan/tools/aat/aat type kei; plan/tools/aat/aat key enter    # 例: greeter で login
plan/tools/aat/aat mark login
plan/tools/aat/aat wait-log 'ZWL MAP' --since login --timeout 30
plan/tools/aat/aat windows                               # compositor の ZWL の行から窓の一覧
plan/tools/aat/aat click 640 400; plan/tools/aat/aat drag 100 100 400 300
plan/tools/aat/aat wheel 640 400 3; plan/tools/aat/aat key ctrl+alt+t
plan/tools/aat/aat shot build/aat-shots/step1.png        # 画面を手元に
plan/tools/aat/aat stop
```

- 鍵: `aat` は自分の checkout の `plan/tmp/guest/id_ed25519`（guest の harness の鍵、`guest.py keys` が作る、git に入らない）を使う。image は**同じ checkout** で作る（`build-image.sh` がその公開鍵を `/root/.ssh/authorized_keys` に入れる）。別の鍵は `--identity`。
- 接続は 1 本を使い回す（ssh の ControlMaster、`build/aat/<target>/`）。2 回目からの命令は速い。
- 座標は画面の pixel（左上が 0,0）。`move`・`click`・`drag`・`wheel` は絶対の位置（注入の pointer の絶対の軸）。`rel` は mouse の相対の量（compositor の加速がかかる）。
- `type` は ASCII を US の配列で打つ。日本語は IME（`key` で切り替えて仮名を打つ）か貼り付けで。
- log の既定は `/run/user/1000/session.log`（`AAT_LOG`、`--log`）。`mark NAME` で今の長さを覚え、`--since NAME` でその後だけを見る。UTF-8 は host で解く（guest の shell を通さない）。
- `windows`・`where` は `ZWL MAP`・`ZWL UNMAP`・`ZWL WINDOW centred`・`ZWL RESIZE settled`・`ZWL RESIZE end`・`ZWL GLASS press move` の行から、surface ごとの最後の位置と大きさ。app の名前・題名・titlebar の部品の位置は今の log に無い（下の「P1 への依頼」）。
- QEMU と実機の証拠は分けて書く。QEMU の guest の判定に console・serial の log を使わない（AGENTS.md）。この道具は SSH と撮影だけを使う。

環境変数: `AAT_TARGET`、`AAT_LOG`、`AAT_INJECT`（注入の daemon、既定 `/bin/aatinject`）、`AAT_SHOT`（撮影、既定 `/bin/keiland-shot {path}`）、`AAT_RUN_DIR`（target の作業の directory、既定 `/run/aat`）、`AAT_STATE`（host の状態、既定 `build/aat`）。

## 注入の daemon の protocol（target の側、ws173-p001 の P1 への案）

`aat start` は target で root として次を起こす（`nohup`、pid は `$AAT_RUN_DIR/inject.pid`）:

```
$AAT_INJECT --fifo=/run/aat/inject.in --done=/run/aat/inject.done --width=W --height=H
```

- 起動の時に `/dev/input-inject` を 2 回開いて、**pointer**（絶対の軸 `ABS_X` 0..W-1・`ABS_Y` 0..H-1、`REL_X`・`REL_Y`・`REL_WHEEL`・`REL_HWHEEL`、`BTN_LEFT`・`BTN_RIGHT`・`BTN_MIDDLE`）と **keyboard**（evdev の key の code）を宣言し、終わるまで開いておく（閉じると device が消える）。
  - 絶対の軸が要る理由: compositor は相対の mouse に加速をかける（`userland/desktop/wayland/pointer-accel.c`）ので、相対の量では狙った pixel に置けない。QEMU の usb-tablet と同じ絶対の pointer を 1 つ宣言できれば足りる（相対と絶対を別の device にしてもよい）。
- FIFO を読み（書き手が閉じたら開き直す）、1 行 1 命令 `SEQ VERB ARGS`（SEQ は 1 から増える整数）:

| VERB | 引数 | 意味 |
| --- | --- | --- |
| `abs` | `X Y` | pointer を画面の pixel の位置へ（範囲の外は拒否） |
| `rel` | `DX DY` | 相対の移動（mouse の count） |
| `button` | `CODE 0\|1` | button（`BTN_LEFT` 0x110・`BTN_RIGHT` 0x111・`BTN_MIDDLE` 0x112）を離す・押す |
| `wheel` | `DY DX` | wheel の detent（DY > 0 は下へ、`REL_WHEEL` は -DY） |
| `key` | `CODE 0\|1` | key（evdev の code、1..0x2ff）を離す・押す |
| `wait` | `MS` | 待つ（0..10000） |

- 各行を行ったら（拒否したら）`--done` の file に `done SEQ STATUS\n` を足す（STATUS は 0、拒否は errno の値。例 22）。各命令の後に `SYN_REPORT`。
- 宣言した device を、終わる時（SIGTERM）に閉じる。

## 撮影の口（ws173-p002 の P1 への案）

`keiland-shot PATH`: compositor が合成した今の画面を PNG で PATH に書き、0 で終わる（root で動く）。aat は書かれた PNG の大きさを画面の大きさとして使う。

## P1 への依頼（log）

窓の座標の helper のために、compositor の log に次があると良い（無くても `windows` は動くが、窓を app で探せない）:

- 窓の app と題名: 例 `ZWL TOPLEVEL surface=S app_id=ID title=T`（map と題名の変化の時）。
- 窓の今の位置と大きさの一覧を出す口（例 `keiland-shot --windows`）。

## 自己試験

- host（実装の担当）: `sh plan/tools/aat/tests/run-host.sh` → `aat-host: PASS`。CLI・protocol の行・mark と wait-log・`windows`・転送を、偽の注入と撮影で確かめる。
- QEMU（T1）: AAT の config を harness つきで作る（QEMU の network には harness の `net.conf` が要る）:
  1. `plan/tools/guest/test-image.sh plan/tools/aat/config-amd64-aat.mk BUILD`、`plan/tools/guest/guest.sh start BUILD/hdd-image.img`。
  2. `plan/tools/aat/aat --qemu check`（`have /dev/input-inject`。注入・撮影の命令は P1 の物が入るまで missing）、`aat --qemu run 'uname -a'`、`aat --qemu put`・`get` の往復、`aat --qemu mark m --log /tmp/x.log` の後に `aat --qemu run 'echo hello >> /tmp/x.log'` と `aat --qemu wait-log hello --since m --log /tmp/x.log`。
  3. P1 の注入と撮影が入った後: `aat --qemu start`、greeter で `aat --qemu type kei`・`key enter`、`wait-log 'ZWL MAP'`、`windows`、`click`・`drag`・`wheel`・`key`、各段で `shot` の PNG。合格: 各命令が 0 で終わり、PNG に操作の結果が見える。
