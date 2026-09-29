<!-- awesome-plan project=zedbsd record=ws075p009 -->

# ws075-p009: 安定: BUG-056・BUG-057 ほか p002〜p008 で出た bug

Phase ID: `ws075-p009`
Parent: [WS075](../ws.md)
Status: in-progress（2026-09-29）
Phase disposition: normal
承認: 2026-09-29 main の依頼（WS075 の i915 の subagent、項目 2）。

## 現状（2026-09-29 の調べ）

| bug | 状態 | この Phase で |
| --- | --- | --- |
| [BUG-056](../../bugs/BUG-056.md)（zdesktop で compositor が client の後片付けの後に終わる） | resolved（2026-09-28、BUG-077 と同じ原因、GPU core の修正 404830b4） | デモの形の連続の実機の run で再発が無いことを見る |
| [BUG-057](../../bugs/BUG-057.md)（GLX の zgears が数百 frame の後に止まる） | resolved（2026-09-27、ws069-p010: kernel の unix socket の待ちが waitq_sleep の EAGAIN を失敗にしていた） | 同上（Gears を回し続ける） |
| [BUG-058](../../bugs/BUG-058.md)（App Home の zgears が最初の frame の前に終わる） | tracking（p017 の 6 回で再現せず） | 同上（App Home から Gears を起動） |
| [BUG-085](../../bugs/BUG-085.md)（wlkill の直後に compositor が止まる） | tracking（p015 の 11 回で再現せず） | 同上（client の終わりの後も描画が進むこと） |
| [BUG-094](../../bugs/BUG-094.md)（HAL の起動時の時間の測定が vCPU の停止で狂う） | scheduled（p015 で `lapic.c`・`timecounter.c` を修正、修正の後の 10 回で 0。p015 は cleared） | p015 の証拠と、この Phase の起動を合わせて resolved にする |
| [BUG-095](../../bugs/BUG-095.md)（capture の image の power-off） | tracking（原因は init の oneshot。base system の判断） | 範囲外（i915 でない）。触れない |

p002〜p008 で新しく出た i915 の bug は無い（p008 の「request の終わりの 150 回に 1 回が 1 ms の期限で見つかる」は安全網の働きで、不具合ではない）。

## 範囲

デモの構成（内蔵 LCD、`display=edp`、自動の login の session、App Home の application）での連続の実機の run で、上の bug の再発が
無いことを確かめる。再発したら gdbstub で原因を調べて直す（i915 の側）。

非範囲: GPU の engine の reset（`drv_i915_worker_engine_reset` の未実装の道。GPU の hang は今も device lost で compositor が終わる）。
大きいので、要れば別の Phase。

## 手順（実機の passthrough、Phase の終わりに 1 回、`flock /tmp/i915-hw.lock`）

1. デモの passthrough の image（main を merge した tree、`build-demo-image.sh ... passthrough ZEDBSD_GRAPHICAL_BOOT=n
   "ZEDBSD_BOOT_EXTRA_LINES=display=edp login=graphical"`）を `hdmi-h4-hw.sh start`。
2. App Home から Files・Notes・Terminal・PDF Viewer・Browser・Model viewer・Gears・X terminal を順に起動し、各 1 枚を撮る（窓が描かれること）。
3. Gears を開いたまま、周期の drag の負荷（`h4-ctl.py load`）で 20 分以上。途中と終わりに `rate`・`latency` と撮影（描画が進むこと、
   Gears が回ること = 2 枚の窓の中が違う）。
4. 終わりに gdbstub で worker の数（executed・failed、待ちの終わり方）を読む。
5. 判定: 全ての app の窓が出る、描画が最後まで進む（flip の率が落ちない）、worker の failed 0。

## 記録

（実施中）
