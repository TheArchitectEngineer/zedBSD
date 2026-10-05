<!-- awesome-plan project=zedbsd record=ws173-p003 -->

# ws173-p003: AAT の host の道具と AAT の image の config

Status: in-progress（2026-10-05 夜、P2 g16、q777。CLI の骨組み・SSH・log・窓の座標・撮影の取得・注入の命令と host の自己試験まで。P1 の注入（p001）と撮影（p002）の口が入るまで、target での確かめは未）
Disposition: normal
Parent: [WS173](../ws.md)
Queue: q777（Q1、2026-10-05 夜、最優先）
依存: [p001](../ws.md)（`/dev/input-inject` のマウスとキーボード、P1）、[p002](../ws.md)（`keiland-shot`、P1）。口の形が決まるまで CLI の骨組みと SSH の部分を先に（Q1 の指示）

## 範囲（Q1 の指示、2026-10-05 夜）

host の道具 `plan/tools/aat/`: SSH で素の 5330（UAT の image、10.0.30.3）に入り、1 つの CLI で、マウスの移動・click・drag・wheel、key・文字列の入力、画面の撮影の取得、log の行の待ち、compositor の log からの窓の座標。AAT の image の config `plan/tools/aat/config-amd64-aat.mk`（UAT の config ＋試験の注入＋撮影の口、製品には入れない）。QEMU での自己試験を T1 に頼む手順。

## 作った物

- `plan/tools/aat/aat`（python3、1 file）: 命令 `check`・`run`・`get`・`put`・`start`・`stop`・`move`・`rel`・`click`（`--button`・`--count`）・`down`・`up`・`drag`・`wheel`・`key`（`ctrl+alt+t` の chord）・`type`（ASCII、US の配列）・`shot`・`mark`・`wait-log`・`lines`・`windows`・`where`。target は `--target user@host[:port]`・`--target 5330`（root@10.0.30.3）・`--qemu`（`GUEST_RUNTIME/session.json`）・`--local`（自己試験）。
  - SSH は ControlMaster で 1 本を使い回す。鍵は checkout の guest の harness の鍵（`plan/tmp/guest/id_ed25519`）。root 以外の user では `sudo -S`（`AAT_PASSWORD`、既定 kei。zedBSD の sudo に `-n` は無い）。
  - log は host で UTF-8 を解く（`tail -c +N` で mark の後の bytes を取る）。`wait-log` は 0.3 秒ごとに読む。
  - 窓: `ZWL MAP`・`UNMAP`・`WINDOW centred`・`RESIZE settled`・`RESIZE end`・`GLASS press move` の行から surface ごとの最後の位置と大きさ。
  - 注入: target の root の daemon（`AAT_INJECT`、既定 `/bin/aatinject`）に FIFO で `SEQ VERB ARGS` の行（`abs`・`rel`・`button`・`wheel`・`key`・`wait`）を送り、done の file の `done SEQ STATUS` を全部の行について確かめる（1 行でも拒否なら失敗）。protocol は [README](../../tools/aat/README.md) の「注入の daemon の protocol」（**P1 への案**）。
  - 撮影: `AAT_SHOT`（既定 `/bin/keiland-shot {path}`）で target に書かせて `cat` で取る。PNG の header で確かめ、`start` は撮影の大きさを画面の大きさにする。
- `plan/tools/aat/config-amd64-aat.mk`: UAT の config ＋ `CONFIG_INPUT_TEST_INJECT := y`。P1 の daemon と撮影の package の名前は入った時に足す（今は注記）。
- `plan/tools/aat/build-image.sh BUILD`: `test-image.sh --no-harness` に harness の root の鍵と host 鍵だけを足す（harness の `/etc/net.conf` は QEMU の USB の network の設定で、実機の network を置き換えるので入れない）。
- `plan/tools/aat/tests/run-host.sh`（`fake-inject.py`・`fake-shot.py`）: host の自己試験。
- `plan/tools/aat/README.md`: 使い方、protocol の案、P1 への依頼、自己試験の手順。

## 判断（P2）

- **絶対の pointer を P1 に頼む**: compositor は相対の mouse に加速をかける（`pointer-accel.c`）ので、相対の量では狙った pixel に置けない。注入の pointer に絶対の軸（QEMU の usb-tablet と同じ）を足してもらい、`move`・`click`・`drag`・`wheel` はそれを使う。`rel` は相対の確かめ用。
- 注入の daemon は 1 本を起こしたままにする（`/dev/input-inject` は閉じると device が消える。命令ごとに開くと compositor に毎回 hotplug が見える）。
- 完了の確かめは done の file（FIFO は一方向なので）。ssh の 1 回の呼び出しの中で FIFO に書き、done を待つ。
- 窓の app の名前・題名・titlebar の部品の座標は今の log に無い。P1 に log の行か一覧の口を頼む（README）。

## 確かめ（host）

- `sh plan/tools/aat/tests/run-host.sh` → `aat-host: PASS`（start と画面の大きさ、click・double・drag・wheel・rel・chord・type の protocol の行、画面の外・知らない key・ASCII の外の拒否、注入の拒否の報告、mark と wait-log・lines・timeout、`where`・`windows`・unmap、shot、run の状態、put・get、stop の後の拒否）。
- `build-image.sh` の引数（net.conf を除き root の鍵と host 鍵が残る）を dry-run で確かめた。image の build は未。

## 未実施

- target（QEMU・5330）での確かめ: P1 の p001・p002 の後。QEMU は T1（README の「自己試験」）。SSH・run・get・put・mark・wait-log の部分は今の image でも QEMU で確かめられる（README の手順の 2）。
- 5330 の実機: 未実施（AAT の image を USB に書くのはユーザー）。
- P1 の口の形が案と違ったら `aat` の `inject`・`take_shot` を合わせる。
