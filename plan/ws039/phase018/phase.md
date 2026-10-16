# ws039-p018 BUG-331 の調べ（RDNA 機の desktop の停止と reset）

- 親: [WS039](../ws.md)、状態: planned（2026-10-16 夜 ユーザー「RDNA機はフリーズはいったんおいておいて、先にフェーズをすべて終わらせましょう。」で後回し）
- 起点: [BUG-331](../../bugs/BUG-331.md)。ユーザー「原因がわからなければ、あとで調べるフェーズを作って、先に進めていいです。」
- 分かっている事（2026-10-16 P3）: p017 の 2 段目の試験の kernel Y（console の framebuffer を kernel が WC で map、BAR0 を移動）で 1 回停止。kernel X（console を UC、BAR0 を移動）は App Home の開閉 70 回・app 5 つで停止せず。Y の自動の試験は途中で止めた。
- 再開: Y で同じ自動の試験（aat の Super の注入、plan/ws039/tests/config.mk の CONFIG_INPUT_TEST_INJECT）、必要なら Y に amdrdna.bar.grow=0。

## 2026-10-16 P3 の追記（記録の詳細）

- 止まった起動: 19:09 の起動（kernel a7b2cab8 = Y の code、BAR0 を 0x4200000000 に 8 GiB、kernel の console の map を WC）。USB の sda2 の `log/boot-000019.txt` は「20 s after boot」（19:09:32 JST）の印の後に A9・`lease 1 claimed`・`thermal reading 10` まであり、「60 s after boot」の印も「the system is stopping」も無い → 19:09:40〜19:10:10 の間に止まり、reset（人の操作ではない）で firmware の BootOrder の Debian（19:10:29 起動）へ。止まる前の fault・hung・device lost・panic の行は無い。Debian の journal に zedBSD の側の記録は無い。iTCO の watchdog（Debian が shutdown で 10 分に arm）は時刻が合わない。
- その直前 19:08:18 に P3 の script がユーザーの操作中に zedBSD を reboot した（P3 の過ち）。ユーザーの見た「App Home を閉じる animation 中の停止」は 19:09 の起動の方と見る（mouse も止まり同じ画面のまま、その後 Linux）。
- 自動の試験（`plan/tools/aat/aat` で Super を注入、App Home の開閉、5 回ごとに app を Home から起動して開いたままに。判定は KWL HOME の行・ssh の応答 3 秒ごと）:
  - X（kernel 4418743c: Y と同じで console の map を UC、test のみ）: 19:32〜19:36 に 30 回（launch は Enter で通らず）、19:37〜19:40 に 40 回（click で app 5 つが開いたまま）。停止なし、ssh の無応答 0、`fault=0 hung=0`、cpu_free=923MiB。
  - **Y（kernel 2a215f07、WC）の試験は実施していない**: 19:40 に X から Debian に戻す手順が失敗し（`hw-run.sh debian` の既定の zedBSD の address が 10.0.10.3 で、10.0.30.5 の zedBSD に届かなかった）、Y は install されなかった。その後 ユーザーの判断で中断。
- p017 の 2 段目は X の形（console の map を HAL と同じ UC）で仕上げた。WC の別名（kernel の WC と HAL の UC が同じ GOP の page を map）が原因の候補として残る。
- 再開の手順: Y を install（Debian から `~/ws039/mnt-boot` に USB の sda2 を mount して vmunix を置き、`hw-run.sh boot 0003`）、`plan/tools/aat/aat --target root@10.0.30.5`（`AAT_INPUT=/tmp/aat-input`、aat-input は build の `bin/aat-input` を /tmp に）で同じ試験。止まらなければ Y に WC と HAL の UC の別名が無い形（HAL の map も WC、P4 の BUG-330 の方向）も比べる。
- 2026-10-16 21:0x P5 の発見（p019）: amdrdna の desktop の間も kernel の text console が GOP の framebuffer（UC）に描き、kmsg の 1 行ごとに 1920x1080 を描き直して node の lock の中で約 2 s 止まる（amdrdna の present_claim が kern_text_suspend を呼んでいない、i915・bcm2711 は呼ぶ）。停止との関係は未確認、P5 が present.c で直す。
