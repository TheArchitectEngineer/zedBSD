<!-- awesome-plan project=zedbsd record=ws264-p001 -->

# ws264-p001 設計

Status: in-progress（2026-10-19 P1、q1257。cleared は Q1 の判定）

- 親: [WS264](../ws.md)。結果: [design.md](../design.md)。
- 起動の道は QEMU の source（`hw/ppc/mac_newworld.c`）と、gdbstub で止めた最小の flat image で確かめた（design §1）: OpenBIOS は 0x01000000 へ、firmware の translation のまま r5 = client interface で入る。
