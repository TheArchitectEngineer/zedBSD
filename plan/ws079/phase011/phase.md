<!-- awesome-plan project=zedbsd record=ws079-p011 -->

# ws079-p011: Notes の仕上げ（PDF の中の名前を Kei に、Kei の glass の toolbar、pen の hover、描画の cache、部分の消しゴム）

<!-- awesome-plan-current:start -->
Status: in-progress（2026-09-28、Kei desktop subagent）
Disposition: normal
Parent: [WS079](../ws.md)
Queue: main の指示（Kei desktop subagent、2026-09-28、2026-10-17 のデモに向けて）。Awesome Plan の Queue の item ではない
Resume point: 下の「進み」
<!-- awesome-plan-current:end -->

## 範囲（main の指示、2026-09-28）

1. PDF の中に見える名前を Kei に（main の判断、[design-input-notes.md](../design-input-notes.md) の末尾）: `/Producer (Kei Notes)`、
   添付 `kei-notes.bin`・`application/x-kei-notes`。互換は要らない。libpdf は writer の producer の文字列だけに触れる（p006 が reader と content を編集中）。
2. Notes の toolbar と外回りを Kei の見た目に（[kei-identity-design.md](../../ws035/kei-identity-design.md)）: files と titlebar の glass の調子
   （すりガラスの半透明、slate の文字、選んだ道具は Kei の青）、page の後ろの柔らかい背景、すっきりした全画面、pen の近接中の cursor か hover の点。
3. 速さ: 確定した stroke を page の画像に焼き（design-input-notes §5.3）、毎 frame は描いている stroke だけを描く。Venus で多数（例 500）の
   stroke の frame の時間を前後で測る。
4. 時間があれば部分の消しゴム（消しゴムが横切った所で stroke を切る）。

試験: `notes-p005.sh`・`notes-pen.sh`・`notes-gesture.sh`・`run-notes-host.sh` を再び通す。画面は `build/ws035-shots/ws079-p011-20260928-*.png`。

## 進み

### 1. 名前（2026-09-28）

| file | 変更 |
| --- | --- |
| `userland/base/libpdf/writer.c` | libpdf の writer は producer を呼び手から受けず固定の文字列を書くので、既定を変えた: 1519 行 `/Producer (zedBSD Notes)` → `/Producer (Kei Notes)`、1602 行の添付の説明 `/Desc (zedBSD Notes edit data)` → `/Desc (Kei Notes edit data)`（どちらも PDF の中で見える文字列。`write_information()` と `write_attachment_objects()` の中だけ） |
| `userland/desktop/notes/notes.h` | `NOTES_ATTACHMENT_NAME` `"kei-notes.bin"`、`NOTES_ATTACHMENT_TYPE` `"application/x-kei-notes"` |
| `plan/ws079/design-pdf.md` | §1 の `/Info`、§2 の添付の名前・subtype・説明、他の viewer の説明 |
| `plan/ws079/tests/run-notes-host.sh`・`notes-p005.sh` | `qpdf --show-attachment=kei-notes.bin` |

libpdf の試験（`host-pdf-writer.c`・`host-pdf-reader.c`・`run-pdf-writer.sh`）は添付の名前を呼び手として自分で渡しているだけなので変えていない
（p006 が reader の試験を編集中）。

確認: `sh plan/ws079/tests/run-notes-host.sh` → plain・ASan・UBSan とも ok、`pdfinfo` の Producer は `Kei Notes`、`qpdf --list-attachments` は `kei-notes.bin -> 9,0`。
