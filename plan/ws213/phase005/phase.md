<!-- awesome-plan project=zedbsd record=ws213-p005 -->

# ws213-p005: S3: 作業 thread（glyph の raster・壁紙・大きな字）

Parent: [WS213](../ws.md)
Status: planned（p004 の後）
Phase disposition: normal

## 範囲

- p001 の 5 節。M は cell と metrics だけ、raster の job を W に、walk の始めに終わった job を cell の割り当ての順に書く、未着の字は描かずに印、その写しの layer は無効。壁紙の画素とぼかし・大きな字を W に。

設計は [p001](../phase001/phase.md)。

## 受け入れの条件

1. host: p001 の 9 節 (j) の試験 PASS。
2. build warning 0（下のコマンド）。
3. Pi: 日本語の UI で初出の字の多い画面（Settings・App Home の検索）で walk の最長が p004 より下がる。壁紙の変更の間も cursor の present が止まらない。
4. T1（i915・Venus）で回帰なし、30 分まで。

## コマンドと上限

- build（warning 0）: `make -j16 ZEDBSD_CONFIG=config/ci/config-rpi4.mk build/arm64/bin/wayland`、`make -j16 ZEDBSD_CONFIG=config/ci/config-amd64.mk build/amd64/bin/wayland`、`make -j16 keiland-linux`（各 900 秒まで）。FreeBSD は T1 の guest（`plan/tools/keiland-freebsd/backend-test.sh`）。
- host 試験は各 120 秒まで。同じ条件で変更なしの再試行は 3 回まで。QEMU・実機は T1 と Q1 経由（担当は QEMU を起動しない）。
