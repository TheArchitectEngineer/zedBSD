# AAT の道具（`plan/tools/aat/`、WS173 p003）

エージェントが host から SSH で zedBSD の desktop を人のように操作して受け入れを確かめる（Agent Acceptance Test）ための道具。
素の Latitude 5330（AAT の image をユーザーが毎回 USB から起動し、エージェントは起動の後に SSH で入る。2026-10-05 ユーザーの決定）と、`plan/tools/guest/guest.py` の QEMU の guest（`--qemu`）に同じ命令が効く。

| file | 役目 |
| --- | --- |
| `aat` | CLI（python3）。下の命令 |
| `config-amd64-aat.mk` | AAT の image の config: UAT の config（`plan/ws159/tests/config-amd64-uat.mk`）＋試験の注入（`CONFIG_INPUT_TEST_INJECT`、`aat-input`）＋撮影の口（`keiland-shot`、P1 の package が入ったら足す）。製品の config には入れない |
| `build-image.sh` | AAT の image を作る（root の鍵と host 鍵は guest の harness の物、harness の `net.conf` は入れない） |
| `tests/run-host.sh` | host の自己試験（`--local`、偽の `aat-input`（`fake-aat-input.py`）と偽の撮影 `fake-shot.py`） |

## 使い方

```sh
plan/tools/aat/build-image.sh build/aat                 # 5330 用（USB に書く: build/aat/hdd-image.img）
export AAT_TARGET=5330                                   # root@10.0.30.3（--target user@host[:port] でも）
plan/tools/aat/aat check                                 # 届くか、注入・撮影の命令と log があるか
plan/tools/aat/aat start                                 # aat-input の server を起こす（画面の大きさは撮影から）
plan/tools/aat/aat type kei; plan/tools/aat/aat key enter    # 例: greeter で login
plan/tools/aat/aat mark login
plan/tools/aat/aat wait-log 'ZWL MAP' --since login --timeout 30
plan/tools/aat/aat windows                               # compositor の ZWL の行から窓の一覧
plan/tools/aat/aat click 640 400; plan/tools/aat/aat drag 100 100 400 300
plan/tools/aat/aat wheel 640 400 -3; plan/tools/aat/aat key ctrl+alt+t   # wheel は N > 0 で上
plan/tools/aat/aat shot build/aat-shots/step1.png        # 画面を手元に
plan/tools/aat/aat stop
```

- 鍵: `aat` は自分の checkout の `plan/tmp/guest/id_ed25519`（guest の harness の鍵、`guest.py keys` が作る、git に入らない）を使う。image は**同じ checkout** で作る（`build-image.sh` がその公開鍵を `/root/.ssh/authorized_keys` に入れる）。別の鍵は `--identity`。
- 接続は 1 本を使い回す（ssh の ControlMaster、`build/aat/<target>/`）。2 回目からの命令は速い。
- 座標は画面の pixel（左上が 0,0）。`move`・`click`・`drag`・`wheel` は `aat-input` の絶対の pointer（`move-to`）。`rel` は相対の mouse の量（compositor の加速がかかる）。`drag` は左 button だけ（`aat-input` の drag）。他の button で引くなら `move`・`down`・`move`・`up`。
- `key` の名前は evdev の名前の小文字（`enter`・`leftctrl`・`f5`、`ctrl`・`alt`・`shift`・`super` は左）。`type` は ASCII を US の配列で打つ（改行は `key enter`）。日本語は IME（`key` で切り替えて仮名を打つ）か貼り付けで。
- log の既定は `/run/user/1000/session.log`（`AAT_LOG`、`--log`）。`mark NAME` で今の長さを覚え、`--since NAME` でその後だけを見る。UTF-8 は host で解く（guest の shell を通さない）。
- `windows`・`where` は `ZWL MAP`・`ZWL UNMAP`・`ZWL WINDOW centred`・`ZWL RESIZE settled`・`ZWL RESIZE end`・`ZWL GLASS press move` の行から、surface ごとの最後の位置と大きさ。app の名前・題名・titlebar の部品の位置は今の log に無い（下の「P1 への依頼」）。
- QEMU と実機の証拠は分けて書く。QEMU の guest の判定に console・serial の log を使わない（AGENTS.md）。この道具は SSH と撮影だけを使う。

環境変数: `AAT_TARGET`、`AAT_LOG`、`AAT_INPUT`（既定 `/bin/aat-input`）、`AAT_SHOT`（撮影、既定 `/bin/keiland-shot {path}`）、`AAT_RUN_DIR`（撮影の一時の置き場、既定 `/run/aat`）、`AAT_STATE`（host の状態、既定 `build/aat`）、`AAT_PASSWORD`（root 以外で入った時の sudo、既定 kei）。

## target の側の口（P1、ws173-p001・p002）

- 入力: `aat-input`（`userland/tests/aat-input`、root）。`aat-input start --width W --height H` が `/dev/input-inject` に相対の mouse・絶対の pointer（W×H の画素、既定 1920×1200）・keyboard を宣言して背景の server になり（`/run/aat-input.sock`）、`AAT-INPUT ready` を出す。`aat-input COMMAND ...` は 1 つの命令を送り `ok` か `error WHY`。命令は `move-to X Y`・`move DX DY`・`click [BUTTON] [X Y]`・`double-click`・`down`・`up`・`drag X1 Y1 X2 Y2 [STEPS]`・`wheel N`・`hwheel N`・`key NAME[+NAME...]`・`key-down`・`key-up`・`type TEXT`・`sleep MS`・`stop`。`aat` の各命令は 1 つか少数の `aat-input` の命令に対応する（SSH の 1 回ずつ、ControlMaster で速い）。
- 撮影: `keiland-shot OUT.png`（compositor が合成した画面、root）。aat は書かれた PNG の大きさを画面の大きさとして `aat-input start` に渡す。

## P1 への依頼（log）

窓の座標の helper のために、compositor の log に次があると良い（無くても `windows` は動くが、窓を app で探せない）:

- 窓の app と題名: 例 `ZWL TOPLEVEL surface=S app_id=ID title=T`（map と題名の変化の時）。
- 窓の今の位置と大きさの一覧を出す口（例 `keiland-shot --windows`）。

## 自己試験

- host（実装の担当）: `sh plan/tools/aat/tests/run-host.sh` → `aat-host: PASS`。CLI・`aat-input` に送る命令・拒否・mark と wait-log・`windows`・転送を、偽の `aat-input` と撮影で確かめる。
- QEMU（T1）: AAT の config を harness つきで作る（QEMU の network には harness の `net.conf` が要る）:
  1. `plan/tools/guest/test-image.sh plan/tools/aat/config-amd64-aat.mk BUILD`、`plan/tools/guest/guest.sh start BUILD/hdd-image.img`。
  2. `plan/tools/aat/aat --qemu check`（`have /dev/input-inject`・`have /bin/aat-input`。`keiland-shot` は P1 の物が入るまで missing）、`aat --qemu run 'uname -a'`、`aat --qemu put`・`get` の往復、`aat --qemu mark m --log /tmp/x.log` の後に `aat --qemu run 'echo hello >> /tmp/x.log'` と `aat --qemu wait-log hello --since m --log /tmp/x.log`。
  3. 撮影が入った後（無ければ `aat --qemu start --size 1280x800` で入力だけ）: `aat --qemu start`、greeter で `aat --qemu type kei`・`key enter`、`wait-log 'ZWL MAP'`、`windows`、`click`・`drag`・`wheel`・`key`、各段で `shot` の PNG。合格: 各命令が 0 で終わり、PNG に操作の結果が見える。
