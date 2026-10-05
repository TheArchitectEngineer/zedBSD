<!-- awesome-plan project=zedbsd record=ws110 -->

# WS110: コンポジタの通常起動を既定にし試験modeを明示

Status: completed（2026-10-05、Q1 の判定: p001・p002 は T1-168、p003 は見直しと build で cleared）
Primary Milestone: MG006
Related Milestones: MG001
Parent: [Master](../master.md)
Queue: Q1（P2、2026-10-05）
Resume point: なし（完了）。

## 目標

2026-10-02 ユーザー:「--sessionがないと開発モードになるのを廃止。--testingをつけると開発モードにするように変更。関連するテストスクリプトも修正する。」2026-10-05 ユーザーの決定（Q1 経由）: `--testing` を必須にする（案 A）、実装する、他の WS の試験の機械の置き換えを WS110 の範囲として許す。

## 結果

- **引数の契約**（`userland/desktop/wayland/role.c`・`role.h`、`main.c`）:
  - 引数なしと `--session`（互換の別名）: 通常の session。期限なし、lock の idle 10 分、Files の desktop、Log Out。
  - `--testing`: 有限の試験（既定 150 秒）。`--timeout=N`・`--max-frames=M` は `--testing` が要る。
  - `--greeter --auth-fd=N`: login 画面（変わらない）。
  - `--testing` と `--session`・`--greeter`・`--control-fd`・`--lock-idle`、`--greeter` と `--session` の組み合わせは理由の 1 行と exit 2。
  - 引数を全部読んでから role を 1 度決めるので、順に依らない。READY の行の末尾に `role=normal|testing|greeter`。
- **試験の起動**: tree が追う試験の 233 file・246 行に `--testing` を足した（置き換えの script は git の履歴にある、main の 8aa4174d）。製品の起動（sessiond の session.sh・greeter.c、Linux・FreeBSD の keiland.desktop・keiland-desktop.in、deb の run.py）は `--session` のまま動く。展示用の `plan/ws035/demo/run-zdesktop.sh` は今までの動きを保って `--testing`。
- **証拠**: host の role の試験 16 case、Linux の Keiland の build（warning 0）と拒む 6 通りの実起動、zedBSD の compositor の build（warning 0）。QEMU（T1-168）: roles-guest、files-p002（置き換えた試験の代表）、zdesktop-p095（greeter → session → Log Out）。実機は未実施。
- **規約**: role.c・role.h と試験は違反 0、main.c は関数ごとに増えていない（p003）。

## 制限・移管

- FreeBSD の build と FreeBSD での起動は未実施（native の FreeBSD が要る）。同じ main.c なので契約は同じ。
- 試験を新しく書く時は、有限に終わらせるなら `--testing --timeout=N` を付ける（付けないと期限なしの session になる）。
- 試験は [plan/tools/compositor/](../tools/compositor/README.md) に移した（`run-host-role.sh`・`roles-guest.sh`）。Master の Tools 節への登録は Q1。

## Phase

| Phase | 内容 | 状態 |
| --- | --- | --- |
| p001 | role と引数の契約、`role.c`、host の試験 | cleared（T1-168） |
| p002 | 試験の起動の置き換え（233 file・246 行）、代表の試験 | cleared（T1-168） |
| p003 | 全文規約と回帰 | cleared（2026-10-05 Q1） |

設計の検討（`design.md`・`launch-candidates.txt`）と Phase の記録は git の履歴にある（最後の版は main の `plan/ws110/`、8aa4174d 以降）。
