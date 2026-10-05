<!-- awesome-plan project=zedbsd record=ws154-p002 -->

# ws154-p002: IME の選択の仕組みと Languages の頁

Status: cleared（2026-10-05 Q1: T1-173b で languages-p002 PASS（ja・none・SKK の変換と確定・頁の switch）。cleared）。以前: in-progress（実装と build は済み、QEMU は T1 待ち）
Disposition: normal
Parent: [WS154](../ws.md)
Queue: Q1（2026-10-05、P2）
依存: [p001](../phase001/phase.md)（D1・D2・D6。1・2 は Q1 が承認）、[p003](../phase003/phase.md)（SKK の engine）

## 実装（2026-10-05、P2）

- **設定の key**（D1）: `settings-keys.c` に `ime.method`（compositor、int 0〜2、既定 1、KEPT）。
- **compositor**（D2、`wayland/`）:
  - `zwl.h` に `ime_method`（既定 1、`main.c`）。`settings.c` の `settings_apply` が `ime.method` を読み、起動の後の変更なら `zwl_ime_method_changed` を呼ぶ。
  - `input-method.c`: `ime_spawn` が `--method=none|ja|skk` を渡す（log `ZWL IME started pid= client= --method=…`）。`zwl_ime_method_changed` は、今の program と違う method なら `replacing` を立てて program に SIGTERM を送る（program は辞書を保存して終わる）。接続が切れると `ime_lost` はすぐに起動し直す（1 秒待たない）。この起動は「1 分に 3 回」の数に入れず、諦めた（given_up）後でも起動する（log `ZWL IME method=N`）。
- **IME の program**（`ime/main.c`）: `main` が `--method=` を読み（無い・知らない語は日本語）、`main_engines` は none → direct だけ、ja → direct・ja（今と同じ）、skk → direct・skk（REmacs の辞書 2 つと `~/.config/kei/ime/skk-jisyo`）。log `KEI-IME METHOD N`。利用者の辞書の path の関数に file 名の引数を足した。
- **Languages の頁**（D6、`settings/page-languages.c`）: card「Input method」に 3 つ（Japanese・SKK・None (English)）。各々の右に switch（選ばれた物が on、押すとそれを選ぶ。radio のように 1 つだけ）。card「Display language」は English だけ（WS158 まで）。`se_look_set_number(app, "ime.method", …)` で書き、log `LANGUAGES ime method=N`。`look.c` が `ime.method` を読む（既定 1）。
- **SKK の mode の言語の ID**（D3）は p004。今の SKK は 1 つの ID `skk`（label「あ」）。

## Settings の頁の登録の形（WS164 の Welcome が後で hook を足すため）

- 頁は `settings/settings.h` の `enum se_page_id` に 1 つ（表の順＝一覧の順）と、`settings/pages.c` の `se_pages[]` に同じ位置の 1 行で登録する。行の項目は `{ id, group, glyph, name, summary, word, keywords, ready, draw, press, key, drag }`（`struct se_page`）。
- この Phase で足した行: `{ SE_PAGE_LANGUAGES, SE_GROUP_PERSONALIZATION, SE_GLYPH_GLOBE, "Languages", "The input method and the display language.", "languages", "languages language input method ime japanese skk english keyboard", 1, se_languages_draw, se_languages_press, NULL, NULL }`。enum では `SE_PAGE_DISPLAY` の後、`SE_PAGE_STORAGE` の前（Personalization の最後）。
- `draw` は `int (*)(struct se_app *, struct fm_canvas *, int x, int top, int width)` で、下の端を返す。`press` は `void (*)(struct se_app *, int index)` で、`se_toggle_draw`・`se_button_draw` などに渡した index で呼ばれる。頁を command line で開く語は `word`（`settings languages`）。
- 頁の状態は `struct se_look`（`look.c` が `kl_settings_get_int` で読み、`se_look_set_number` で書く）に置いた（`ime_method`）。`main.c` は変えていない。
- 頁を足すと後ろの頁の enum の値がずれる（`SE_ACTION_PAGE_FIRST + id` の menu の action も）。数でなく `word` で頁を開く試験には影響しない。

## 確かめ

- build: zedBSD の `bin/wayland`・`bin/settings`・`bin/keiland-ime`（warning 0）、Linux の Keiland（warning 0）。style-check: `page-languages.c` は違反 0、変えた既存の file（`input-method.c`・`settings.c`・`look.c`・`pages.c`・`ime/main.c`）は関数ごとに増えていない。
- QEMU（T1 に依頼）: `plan/ws154/tests/languages-p002.sh`（image は `plan/ws154/tests/config-amd64-languages.mk`）。既定の ja、none（Alt+Space で日本語にならない）、skk（`Kanji` Space → ▼漢字 → Enter で 漢字）、ja に戻す、Settings の Languages の頁の SKK の switch で skk になる（PNG 2 枚）。
- 未実施: FreeBSD の build、実機。
