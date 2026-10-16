# ws039-p018 BUG-331 の調べ（RDNA 機の desktop の停止と reset）

- 親: [WS039](../ws.md)、状態: planned（2026-10-16 夜 ユーザー「RDNA機はフリーズはいったんおいておいて、先にフェーズをすべて終わらせましょう。」で後回し）
- 起点: [BUG-331](../../bugs/BUG-331.md)。ユーザー「原因がわからなければ、あとで調べるフェーズを作って、先に進めていいです。」
- 分かっている事（2026-10-16 P3）: p017 の 2 段目の試験の kernel Y（console の framebuffer を kernel が WC で map、BAR0 を移動）で 1 回停止。kernel X（console を UC、BAR0 を移動）は App Home の開閉 70 回・app 5 つで停止せず。Y の自動の試験は途中で止めた。
- 再開: Y で同じ自動の試験（aat の Super の注入、plan/ws039/tests/config.mk の CONFIG_INPUT_TEST_INJECT）、必要なら Y に amdrdna.bar.grow=0。
