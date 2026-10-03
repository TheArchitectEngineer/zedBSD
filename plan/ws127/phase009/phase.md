<!-- awesome-plan project=zedbsd record=ws127-p009 -->
# ws127-p009: Files の host 試験 p009・p010・p013 の FAIL

Status: in-progress（q656、P2 generation7。原因を直し host で PASS、Q1 の判定待ち）
Disposition: normal
Parent: [WS127](../ws.md)
Queue: q656（2026-10-04 Q1「Files の host-p009・p010・p013 の既存の FAIL は見つけた担当がすぐ直す方針…Phase は ws127-p009」）

## 原因

試験の前提が古いだけ（source の不具合ではない）。`plan/tools/files/host-render.c` の既定の font が `build/ws035-fonts/Inter.ttf`（git に無い、以前の build/ の手作りの
置き場）を指していた。2026-10-03 夜に build/ を作り直してから、そこに font が無く、`files-render` が `cannot open build/ws035-fonts/Inter.ttf` で何もせずに
終わり、全ての期待の行が MISSING になっていた（p009・p010・p013 とも main a7940cf でも同じ FAIL、ws135-p005 の時に確認）。font は今 tree の
`userland/desktop/fonts/` にある（compositor の package が install する物と同じ）。

## 直し（試験だけ）

- `plan/tools/files/host-render.c`: 既定の font を `userland/desktop/fonts/Inter.ttf` に。
- `plan/tools/files/host-run.sh`: fallback の font を host の `/usr/share/fonts/...` の有無に頼らず tree の `userland/desktop/fonts/DroidSansFallbackFull.ttf` に。

## 検証（host）

`sh plan/tools/files/host-build.sh` の後、`host-p009.sh` PASS（moveto まで全て ok）、`host-p010.sh` PASS、`host-p013.sh` PASS（MISSING 0）。QEMU は不要（host の試験の道具）。
