<!-- awesome-plan project=zedbsd record=ws148-p002 -->
# ws148-p002: Privacy・Security・Accessibility の頁を無くし、最近の履歴の口を Files と Storage に置く（q824）

Parent: [WS148](../ws.md)（WS149・WS151 の p002 も兼ねる）
Status: in-progress（2026-10-06 P2 q824: 実装と host 試験まで。QEMU の目視は T1）
Disposition: normal
Queue: q824

## ユーザーの決定（2026-10-06、Q1 経由のクリック）

Privacy・Security・Accessibility の頁は 3 つとも「頁を無くす」。Privacy は [p001](../phase001/phase.md) の推奨どおり、Files の Recents に「履歴を消す」、Settings の Storage に「最近の項目を残す」の switch を置く。Security・Accessibility は頁を取り除くだけ（lock の設定などは作らない）。

## 実装

- Settings: `settings.h` の `SE_PAGE_PRIVACY`・`SE_PAGE_SECURITY`・`SE_PAGE_ACCESSIBILITY` と `pages.c` の 3 行を削除した。sidebar・Home の一覧・検索・command line の語（`--page=privacy` など）・menu は `se_pages` の表から作られるので一緒に消える。glyph（EYE・LOCK・PERSON）は残す（LOCK は Network の頁が使う）。
- 翻訳: `locale/settings.keys` の 6 行を削除し、`tools/i18n/tr.py update` で `locale/ja/settings.tr` を作り直した（`tr.py check`: 98 件、0 problems）。
- libkeiland（KL_VERSION 50）: `kl_recent_clear`（一覧を空に）、`kl_recent_set_keep`（0 で一覧を空にして止める。一覧の横の `recent.off` が印）、`kl_recent_keep`。止めている間 `kl_recent_add` は何も足さない。`exports.map` は `exports.py` で作り直した（`--check` OK）。
  - p001 の案は設定の key `recent.keep` だった。`kl_recent_add` は各 app の中で compositor との接続なしに呼ばれるため、一覧と同じ場所の印の file にした（技術の選択）。
- Files: Recents の題の右に「Clear Recents」（一覧が空なら押せない）。`fm_action_clear_recents` が `kl_recent_clear` を呼び、Recents を読み直す。log `ZFILES RECENTS clear error=`。file そのものは消さない。
- Settings の Storage: Trash の下に「Recent items」の card。「Keep recent items」の switch と説明の行（off にした後は「一覧を消して残さない」）。log `ZSETTINGS STORAGE recent keep=` / `recent set keep= error=`。
- 記録: `plan/ws089/design.md` の頁の表の System の行。

## 確認（2026-10-06 P2、host のみ）

| 確認 | 結果 |
| --- | --- |
| `plan/ws148/tests/run-host-recent.sh`（Settings の host renderer: switch を off → 一覧が空、`recent.off` がある。on → `recent.off` が無い。`--page=privacy` は頁を名指さない） | PASS |
| `plan/ws148/tests/run-host-files-recents.sh`（Files の host renderer: Recents の 2 件 → Clear Recents → 0 件、一覧の file は空、file は残る） | PASS |
| build（zedBSD: libkeiland.so・files・settings・wayland、`BUILD=build/p2-b194`。Linux: `keiland-linux.mk all`） | warning 0 |
| style-check（変えた C） | 新しい違反 0 |

host の絵: `build/ws148-recent/off.png`（Storage の card と、System が Users・Updates・About だけの sidebar）、`build/ws148-files-recents/before.png`・`after.png`。

未実施: QEMU（T1）: Settings の sidebar に 3 つの頁が無いこと、Storage の switch、Files の Clear Recents の目視と log。

## 残り

- 準正常・異常は [backlog-p2](../../ws177/backlog-p2.md) の q824 の行。
