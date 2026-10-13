<!-- awesome-plan project=zedbsd record=ws120 -->

# WS120: 音楽アプリ

<!-- awesome-plan-current:start -->
Status: completed（2026-10-13 ユーザー「完了の記録だけが残る物: WS120・WS168・WS178・WS187 →closeしてください。」。旧: incomplete）
Primary Milestone: MG006
Objectives: O2
Parent: [Master](../master.md)
Queue: なし
<!-- awesome-plan-current:end -->

## 目的

ユーザー（2026-10-02）「音楽アプリを実装する。」Keiland の音楽アプリ（local の file の collection と再生）。2026-10-07 のユーザーの決定で範囲を改めた: 形式は m4a（MP4 の AAC）だけ、AAC の decode は videoplayer と同じ libavcodec の dlopen の add-in（独自の decoder は後）、既存の音楽の service との連携はベータ4（外部の service の icon・名前は出さない）。

## 結果（2026-10-13 完了）

- 設計（p001）: 2026-10-07 の決定に沿った app の構成（tags・library・play・view・main）。旧い計画（WAV・FLAC・MP3・Ogg、独自の decoder、libkeiland の PCM 再生の API）は取り下げた。
- `userland/desktop/music/`（p008・p009）: m4a の metadata（mvhd の長さ、ilst の題・artist・album・track・cover）、`~/Music` の collection（album ごと）、再生・一時停止・seek・次・前、検索、Files の `audio/mp4` の関連付けと App Home の「Music」。音は audiod へ。
- 確認: host の試験（tags・library・view）と QEMU の AAT `apps.music.open-from-home`・`apps.music.play`（T1-315、HDA 付き、PLAY・POSITION・ENDED、playing の PNG を Q1 が目視）。音は耳で聞いていない。
- 回帰: `tests/scenarios/apps/music/`（open-from-home・play・failures）。

## 制限・移管

- 既存の音楽の service との連携はベータ4。独自の AAC-LC の decoder は後の扱いだった（今の Music は libmedia の自前の AAC-LC で decode する、`tests/scenarios/apps/music/play.md`）。
- 磨きの残りは WS177 の backlog-p2 の WS120 の行（WS177 が扱った）。
- 全文規約の見直し（p007）は 2026-10-07 の決定の置き換えで canceled（規約の整形はベータ3）。実機の音は未確認。

## Phase

| Phase | 内容 | 最終の状態 |
| --- | --- | --- |
| ws120-p001 | 設計（2026-10-07 の決定: m4a、libavcodec の add-in、service の連携はベータ4）と app の構成 | cleared（2026-10-07） |
| ws120-p002 | libkeiland の PCM 再生の API | canceled（2026-10-08、2026-10-07 の決定で置き換え） |
| ws120-p003 | decoder: WAV・FLAC | canceled（2026-10-08、同上） |
| ws120-p004 | decoder: MP3 | canceled（2026-10-08、同上） |
| ws120-p005 | Music の app の MVP（WAV・FLAC・MP3） | canceled（2026-10-08、p008・p009 に置き換え） |
| ws120-p006 | Ogg Vorbis | canceled（2026-10-08、同上） |
| ws120-p007 | 全文規約と回帰、制限の整理 | canceled（2026-10-08、同上） |
| ws120-p008 | m4a の metadata と `~/Music` の collection（tags.c・library.c）、host の試験 | cleared（2026-10-07、T1-315） |
| ws120-p009 | Music の app（一覧・再生・seek・次・Files・App Home）、libavcodec の add-in と audiod | cleared（2026-10-07、T1-315） |

Phase の directory とこの WS だけの試験は、完了の規則（AGENTS.md）で削除する（git の履歴に残る、削除は Q1）。
