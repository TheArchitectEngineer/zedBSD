<!-- awesome-plan project=zedbsd record=ws170-p002 -->

# ws170-p002: 連絡先と会話の保存（`~/Documents/Phone`）

Status: in-progress（実装・host の試験済み。app の QEMU は p003 と一緒に T1）
Disposition: normal
Parent: [WS170](../ws.md)
Queue: q831（2026-10-07、P2）
依存: [p001](../phase001/phase.md)

## 実装（2026-10-07、P2）

`userland/desktop/phone/store.c`（新規）。設計は [p001](../phase001/phase.md) §1。

- `contacts/<id>.vcf`（FN・TEL、`TEL;TYPE=…:` も読む）、`messages/<id>/<UNIX 秒>-<通し番号>-in|out.txt`（Kind・Channel・Direction・Date・State・Detail の header、空行、本文）。1 item 1 file、書き換えは `.new` に書いて rename。
- 開く時に全部を読み、連絡先は最後の item の新しい順、timeline は日時の順。頭文字（最初と最後の語の最初の文字、UTF-8）と色（名前の hash）は store が作る。日と時刻の語（Today・Yesterday・「Sat, 3 Oct」、「09:41」）、送った message の detail は state から（Sending・Sent・Delivered・Not delivered）。
- API: `ph_store_open`・`_close`・`ph_contacts`・`ph_store_add_contact`・`ph_store_find_number`（数字だけで比べる）・`ph_store_add_item`・`ph_store_set_state`・`ph_store_mark_read`・`ph_channel_word`。

## 確かめ

- host: `sh plan/ws170/tests/run-host-phone-store.sh` → PASS 17（空の store、連絡先と item の追加、state の file への書き直し、日本語の名前の頭文字、他の program が書いた vCard、開き直しで連絡先の順・番号での検索・timeline の順・2 行の本文・未読の数・delivered の保持、既読にして開き直し）。ASan・UBSan。
- style-check 0。

## 積み残し

[WS177 backlog-p2](../../ws177/backlog-p2.md) の WS170 p002 の行。
