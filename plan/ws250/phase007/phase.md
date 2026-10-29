<!-- awesome-plan project=zedbsd record=ws250-p007 -->

# ws250-p007: 規約の全文と出典の監査

Status: cleared（2026-10-29 P5、q1676-i02。C の規約の全文と出典の見直しは f6c18a9c5。ws.md を完了の形にするのは WS の受け入れの残り（下）を Q1 が決めてから）
Parent: [WS250](../ws.md)

## 範囲

p002〜p005 の C を plan/coding-style.md の全文で見直す。「Value(s)/Procedure referenced:」の block の点検。ws.md を完了の形に。依存: 全部。

## 結果（2026-10-29 P5、q1676-i02）

- 見直し（conformance-reviewer に頼んだ、f6c18a9c5、書き方だけ）: runtime.c・work.c・recover.c・vk-node.c・perf.c・display/display.c・rpc.c・node.c・nvrtx-work.h。
  引数の中の関数の呼び出しを変数に出す、3 clause 以上の条件を 1 行 1 clause に、条件演算子を if に、段落の comment を足す、work.c の先頭の comment の古い名前
  （`nvrtx_work_submit()` → `nvrtx_work_channel_submit()`）。
- 出典: 足した block の「Value(s) referenced:」「Procedure referenced:」は project・版・file・関数が揃っている（nouveau v6.19 の gf100.c の 0x409b00、ogkm 570.144）。
  足した行に開発の管理情報は無い。
- 残した点（既存の書き方と同じで直さない）: 宣言の直後の代入（`node = argument;` 等）と `*_made = 1;` の flag に段落の comment が無い、`nvrtx_work_channel_open` の
  `if (error == 0) error = f();` の連なり（早期 return に変えると後始末の流れが変わる）。`platform/amd64/vmunix.mk` の既存の comment に開発の管理情報
  （`ws037-p007 stage D1` 等）があるが、この WS の変更行ではない。
- 確かめ（P5、14:37）: vmunix rc=0・warning 0（cksum 346018419）、`plan/ws250/tests/nvrtx-recover-host-test.sh` 45 checks 0 failed、
  `plan/tools/nvrtx/nvrtx-vk-host-test.sh` PASS 86 checks 0 failures。見直しの後の kernel は実機で流していない（書き方だけの差）。

## WS の受け入れの確認（2026-10-29 P5）

| 条件 | 結果 |
| --- | --- |
| 終わらない shader の app だけが DEVICE_LOST、他の app と compositor は動き続ける | 実機 S2〜S5 で確かめた（p004）。GR の上の停滞は 1 回 4.3 s（期限 2 s＋STOP 2.3 s） |
| GPU の fault（MMU fault・GR exception）の app | 未実施（RC の event の道は STOP の中の RC で通ったが、fault を出す試験の app が無い） |
| GPU の上の取り消し（GSP の RM の口） | STOP_CHANNEL・RESET_CHANNEL・FREE で FREED（p003 の U1） |
| 取り消しの後に GPU を使える | 後の vkhang --loops・mview・desktop が描けた。`isolate` は実装したが、stuck が出ず実機で通っていない（S6 の条件に当たらない）。`reset` は作らない（p006 の選択肢） |
| compositor が詰まった仕事の後ろで待たされない形 | session ごとの channel（p002）。GR の WFI による停滞は残る（GFXP は Future Work） |
| 試験 | host 試験 PASS、実機 S0〜S5 と S1 の probe。QEMU の boot の回帰（design.md 6.2）は未実施 |

→ 推し: 残り（QEMU の boot の回帰、fault の app、isolate の道）は WS の完了の前に Q1 が要否を決める。GR の停滞を縮める案（RESET を先に）は p003 の判断の点。
