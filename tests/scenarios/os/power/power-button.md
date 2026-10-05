---
id: os.power.power-button
title: 電源 button を短く押しても電源は切れない
status: active
areas: [acpi, power, compositor]
paths: [src/drivers/acpi/, userland/desktop/wayland/system.c]
machine: hardware
human: hands
since: BUG-196
---

## 目的
BUG-196（電源 button でただちに電源が切れた）が直ったままであることを確かめる。

## 準備
5330 の desktop。エージェントは `systemevents -c power -t 60000` を動かし、session の log に mark を付ける。

## 操作と確認
1. 操作: 人が電源 button を短く押す。
   確認事項: 電源と事象。正解: 電源は切れない。`systemevents` に `event N power press 1 power-button -`、session の log に `ZWL EVENT power button`。確認方法: 人が機械を見る、出力と `aat wait-log 'ZWL EVENT power button'`。

## 合格
電源が入ったままで、2 つの行がある。

## 注記
押した時の動作（menu・sleep）は WS132 p008。
