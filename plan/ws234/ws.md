<!-- awesome-plan project=zedbsd record=ws234 -->

# WS234: libkeiland の DLL 版（zedBSD 用と Windows 用）で、同じ .exe が zedBSD と Windows の両方で動く

<!-- awesome-plan-current:start -->
Status: planning・**保留**（2026-10-14 ユーザーが起票。同日「すみません、WS080とWS234はいったん保留します。」で保留。p001 の設計は始めた直後に止めた、成果なし）
Primary Milestone: MG006
Related Milestones: MG002
Parent: [Master](../master.md)
Depends on: [WS080](../ws080/ws.md)（`ld.coff`、zedBSD で PE/COFF の EXE と DLL を読む）
<!-- awesome-plan-current:end -->

## ユーザーの指示（原文、2026-10-14）

「WS080は今から実装します。

新しいWSを作ります。今から設計と実装を行います。
・ libkeilandのDLL版を作成 (zedBSD用。amd64ではcalling convensionが違うのでMS ABIにして、DLL内からシステムサービスへの呼び出しではSYSV ABIにする。cdeclの有無で定義を分けられる。)
・ libkeilandのWindows DLLを作成。Win64 APIとDirectX 12で実装。
・Windows/zedBSDの両方で起動するアプリが作れるようにする。
・フォント描画やイメージ読み込み、ファイルアクセスなどもすべてlibkeilandに実装して、libkeiland経由にする。
・DLL版のlibkeilandには、C runtimeも入れる。POSIXフル実装でなくてよい。
・わかりやすくいうとOPENSTEPみたいなものですね。」

## 目標

- `libkeiland.dll` を 2 つ作る: (a) zedBSD 用（PE32+ の DLL、export は MS x64 ABI、DLL の中から zedBSD の system service を呼ぶ所は SysV ABI）、(b) Windows 用（Win64 API と Direct3D 12 で実装）。
- アプリの .exe は `libkeiland.dll` だけに依存し、同じ binary が zedBSD（WS080 の `ld.coff` 経由）と Windows の両方で起動して同じように動く。
- font の描画、画像の読み込み、file の access なども libkeiland の API に入れ、アプリは OS を直に呼ばない。
- DLL 版の libkeiland は C runtime（POSIX の全部でなくてよい部分集合）を含む。
- 位置づけ: OPENSTEP のような、OS の上の移植可能なアプリの層。

## Phase

| ID | 内容 | 状態 | 依存 |
| --- | --- | --- | --- |
| ws234-p001 | 設計: API の範囲（今の libkeiland の API との関係）、ABI の切り替え（header の呼び出し規約の macro）、CRT の部分集合、zedBSD 用の DLL の作り方（toolchain・link・import）、Windows 用の backend（Win64・D3D12・DirectWrite か自前の font）、build と配布、試験の方法、Phase の分割 | planned（保留） | WS080 の design.md |
