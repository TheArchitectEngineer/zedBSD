<!-- awesome-plan project=zedbsd record=ws075p027 -->

# ws075-p027: L1 の判定（窓 10 個で C6 中央値 100 ms 以内・描画の停止 0）

Phase ID: `ws075-p027`
Parent: [WS075](../ws.md)
Status: cleared（2026-09-30、P1。L1 を満たす: C6 中央値 91.3 ms・p90 135.9 ms（5 run、200 試料）、stress-117 の 100 回で停止 0・draw の拒否 0・set の消失 0。5330 の実機は未実施）
Phase disposition: normal
承認: 2026-09-30 Q1 の指示（段の計画の L1）。

## 範囲

最終の image で段 L1 を判定する（ws.md の「段の計画」）。WS075 は L1 で区切り、L2 以降は全 WS が L1 にそろってから（Q1）。

## 方法

- image: この tree（bcc5fdd6 = main を取り込んだ後、p026 の pacing の変更を含む）を `plan/ws075/demo/build-demo-image.sh build/ws075-p008/pt passthrough
  ZEDBSD_GRAPHICAL_BOOT=n "ZEDBSD_BOOT_EXTRA_LINES=display=edp login=graphical"` で（`build/ws075-p027/l1.img`、source の未 commit の変更 0）。
- 5330 の QEMU の VFIO passthrough（`flock /tmp/i915-hw.lock` の下、hdmi-h4-hw.sh）。
- 止まらないこと: 10 app を開き `plan/ws075/tests/hdmi/stress-117.sh 100`、stop の後に kernel log の `draw refused`・`not a descriptor set`・`exhausted`。
- C6: `plan/ws075/tests/hdmi/measure-apps.sh` を同じ image で 5 run、`plan/ws075/tests/hdmi/c6.py`。

## 結果

| 確認 | 結果 |
| --- | --- |
| stress-117 の 100 回 | 100 回とも描画が続く（3 s に最少 38 flip）、「no stop」。kernel log: draw の拒否 0・set の消失 0・object の枠の尽き 0 |
| measure-apps 1〜5 の draw の拒否 | 各 0 |
| C6（c6.py、5 run・200 試料） | **中央値 91.3 ms**・p90 135.9 ms、run の中央値 87.1〜95.0 ms（標準偏差 3.0 ms）、出ない試行 0 |
| 10 app の flip の率・compositor の 1 run | 14.0〜14.5/s・6.43〜6.54 ms |

| run | C6 中央値・p90 | 10 app の flip | compositor の占有・1 run |
| --- | --- | --- | --- |
| 1 | 87.1・139.9 ms | 14.4/s | 64.0%・6.54 ms |
| 2 | 89.5・135.9 ms | 14.5/s | 62.8%・6.43 ms |
| 3 | 90.7・133.2 ms | 14.3/s | 64.4%・6.44 ms |
| 4 | 95.0・136.7 ms | 14.3/s | 65.3%・6.49 ms |
| 5 | 92.6・133.8 ms | 14.0/s | 64.9%・6.43 ms |

判定: L1（C6 中央値 100 ms 以内・描画の停止 0）を満たす。C6（50 ms）は満たさない（L3）。

QEMU と実機: 全て 5330 の passthrough。素の 5330 での確認は未実施（段ごとに 1 回、ユーザーの実機）。

## 残り

- L2（75 ms）: p028・p029（全 WS が L1 にそろってから、Q1）。
