<!-- awesome-plan project=zedbsd record=ws090-p025 -->

# ws090-p025: Browser の web の form の欄で IME を受け付ける

Status: planned（2026-10-07 Q1 が作成、ws090-p022 の項目 6 を分けた）
Disposition: normal
Parent: [WS090](../ws.md)
Queue: q833（P1、WS031 の後）

## 範囲

- ws090-p022 の項目 6: Browser の page の `<input>`・`<textarea>` に focus がある時、IME の preedit・commit が欄に届く（正常系）。
- libbrowser の公開の browser.h の ABI は変えない（要るなら Q1 へ）。

## 確認

- host の試験（headless の page に preedit と commit を渡す）、QEMU は T1（AAT の image で Browser の form に「nihon」→ 日本）。

## Settings の User name の欄（2026-10-07、T1-317・T1-321、P1）

症状: User name の欄（plain、IME を受けない欄）で Alt+Space の後「nihon」を打つと `n` だけが出て、Space で空、Enter で Full name へ。T1-321 で `KEI-IME ACTIVATE` は出るが preedit・commit の log は無い。
調べたこと: `page-users-admin.c` は User name を `SE_FIELD_PLAIN` で描き、libkeiland の `kl_field` は plain の欄に caret を出さない（`kl_ui_text_wanted` は 0）ので、Settings は text input を off にするはず。compositor は text input が有効な時だけ IME に鍵を渡す。それなのに IME が activate している（`ime_current`: App Home・title bar の欄・app の有効な text input のどれか）。どれが activate させたか、今の log では分からない。
今回: compositor に `KWL TEXT enable client= surface=`・`KWL TEXT disable …` の log を足した（`wayland/text-input.c`）。T1 の再試験で、`KWL IME activate …`（`field home`・`field client= surface=`・`client=` のどれか）と `KWL TEXT` の並びから原因を決めて直す。
