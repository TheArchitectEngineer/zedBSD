# ws104-p008 / q522 全文規約の最終 source の確認

Implementation: `cd48e74d110a1e504c2b87ac288d41cdc928367c`（WIP）。Source baseline: `ac5453cc`（WS104 実装前の承認・計画）。規約: `plan/coding-style.md` 全文 §1〜§14、SHA256 `2244803c00d4a3346ab4f74c227f16a5b65c439759dfc1184697ab9f63df3930`。Guardrail / AGENTS の project rules を適用。簡約版は無い。

Conformance status: cleared。最終成功出口の追加確認で見つけた handoff_descriptor の注釈 1 箇所を修正し、移動 / 新規実装 92 関数を再確認。機械検査・全文の手動確認の未解決項目 0（下記の理由つきの保持を除く）。

## 対象

C / header 83 file。新規・移動した実装と境界 header は全文、既存の共通実装は WS104 の全変更箇所とそれを含む関数・呼び出し順・所有を確認。[source manifest](source-manifest.json) は shell / Makefile / toolchain の範囲と最終 SHA256 も保持。削除・移動元は最終 tree に無い。

全文: `userland/desktop/keiland/browser.h`、`userland/desktop/keiland/keiland.h`、`userland/desktop/keiland/keiui.h`、`userland/desktop/keiland/primary-selection-unstable-v1-client-protocol.h`、`userland/desktop/keiland/tablet-unstable-v2-client-protocol.h`、`userland/desktop/keiland/truetype.h`、`userland/desktop/keiland/wayland-client-core.h`、`userland/desktop/keiland/wayland-client-protocol.h`、`userland/desktop/keiland/wayland-client.h`、`userland/desktop/keiland/wayland-egl-core.h`、`userland/desktop/keiland/wayland-egl.h`、`userland/desktop/keiland/wayland-util.h`、`userland/desktop/keiland/wayland/input-method-unstable-v2-client-protocol.h`、`userland/desktop/keiland/wayland/primary-selection-unstable-v1-client-protocol.h`、`userland/desktop/keiland/wayland/tablet-unstable-v2-client-protocol.h`、`userland/desktop/keiland/wayland/text-input-unstable-v3-client-protocol.h`、`userland/desktop/keiland/wayland/virtual-keyboard-unstable-v1-client-protocol.h`、`userland/desktop/keiland/wayland/wayland-client-core.h`、`userland/desktop/keiland/wayland/wayland-client-protocol.h`、`userland/desktop/keiland/wayland/wayland-client.h`、`userland/desktop/keiland/wayland/wayland-util.h`、`userland/desktop/keiland/wayland/xdg-shell-client-protocol.h`、`userland/desktop/keiland/xdg-shell-client-protocol.h`、`userland/desktop/libkeiland/zedbsd/audio-zedbsd.c`、`userland/desktop/libkeiland/zedbsd/network-link-zedbsd.c`、`userland/desktop/libkeiland/zedbsd/network-zedbsd.c`、`userland/desktop/paths.h`、`userland/desktop/wayland/import.c`、`userland/desktop/wayland/zedbsd/gpu-buffer-zedbsd.c`、`userland/desktop/wayland/zedbsd/gpu-zedbsd.c`、`userland/desktop/wayland/zedbsd/gpu-zedbsd.h`、`userland/desktop/wayland/zedbsd/handoff-zedbsd.c`、`userland/desktop/wayland/zedbsd/input-zedbsd.c`、`userland/desktop/wayland/zedbsd/os-zedbsd.c`、`userland/desktop/wayland/zwl-evdev.h`、`userland/desktop/wayland/zwl-gpu.h`、`userland/desktop/wayland/zwl-input.h`、`userland/desktop/wayland/zwl-os.h`。

既存の変更箇所: `userland/desktop/browser/main.c`、`userland/desktop/browser/text/text.h`、`userland/desktop/files/apps.c`、`userland/desktop/files/files.h`、`userland/desktop/files/main.c`、`userland/desktop/files/ui-desktop-actions.c`、`userland/desktop/files/ui-desktop.c`、`userland/desktop/imageview/main.c`、`userland/desktop/ime/main.c`、`userland/desktop/kuidemo/main.c`、`userland/desktop/libkeiui/chooser.c`、`userland/desktop/libwayland/data-device-protocol.c`、`userland/desktop/libwayland/input-method-protocol.c`、`userland/desktop/libwayland/primary-selection-protocol.c`、`userland/desktop/libwayland/subsurface-protocol.c`、`userland/desktop/libwayland/tablet-protocol.c`、`userland/desktop/libwayland/text-input-protocol.c`、`userland/desktop/libwayland/touch-protocol.c`、`userland/desktop/libwayland/virtual-keyboard-protocol.c`、`userland/desktop/mview/main.c`、`userland/desktop/notes/main.c`、`userland/desktop/pdfviewer/main.c`、`userland/desktop/settings/look.c`、`userland/desktop/settings/main.c`、`userland/desktop/settings/page-home.c`、`userland/desktop/settings/page-input.c`、`userland/desktop/settings/settings.h`、`userland/desktop/terminal/main.c`、`userland/desktop/textedit/main.c`、`userland/desktop/textedit/textedit.h`、`userland/desktop/wayland/compose.c`、`userland/desktop/wayland/compose.h`、`userland/desktop/wayland/corner.c`、`userland/desktop/wayland/desktop.c`、`userland/desktop/wayland/glass.c`、`userland/desktop/wayland/home.c`、`userland/desktop/wayland/input-method.c`、`userland/desktop/wayland/input.c`、`userland/desktop/wayland/main.c`、`userland/desktop/wayland/objects.c`、`userland/desktop/wayland/protocol.c`、`userland/desktop/wayland/tablet.c`、`userland/desktop/wayland/touch.c`、`userland/desktop/wayland/zwl.h`、`userland/desktop/xserver/server.c`。

## tools と結果

- `Debian clang-format version 19.1.7 (3+b1)`。root `.clang-format` を継承、`ColumnLimit: 0` の scoped formatting。規約の定義引数の改行と 3 節以上の条件の改行を手動で復元。static prototype は 1 物理行。formatter config は変更していない。
- `python3 build/ws104-control/style-scope.py` は正本 `plan/tools/style-check.py` を読み、全移動実装または変更行の範囲を仕分けした。変更範囲の機械違反 **0**。[範囲・報告全件](style-scope.json)。Python 3、checked-in checker。既存範囲外 68 件、公開 header の extern C 宣言の誤検出 52 件は下記。
- `git diff --check` PASS。
- `python3 build/ws104-control/literals.py compare` PASS。最終規約修正の前後で、移動実装 / import の string と char literal の multiset は file ごとに一致。[修正前 Counter](literals-before.json)。制御 flow の同値は別に手動確認した。
- `sh plan/tools/keiland-os-boundary/check.sh`: C1〜C5 PASS。`display.c` の複写を保持して uapi/gpu.h の include を一時追加すると C1 FAIL / exit 1、byte / SHA256 同一に戻した後 PASS。C1 の検出確認中には image を作らない。常用する script の POSIX sh・`set -eu`・root 解決・一時 directory cleanup・各失敗の出口と全項目の集約を確認した。
- 初回 review build: `make -j64 disk-image` exit 0、自前 warning 0。最終 source を確定後、同じ build を必須 suite で実行し、その結果と runtime は別の [Phase](phase.md) / regression-results に記録する。

## 手動確認（§1〜§14）

file の license・include・型・global・static prototype・public / static 定義順を確認。subscriber allocation と socket、input fd、server の Vulkan limits、handoff の process buffer の生存期間を説明した。関数の先頭の目的、各 decision / paragraph / loop / refusal / success の注釈を確認した。

宣言は block の先頭、意味のある status / field / decoded byte の名前、未使用引数の void、public 境界の prototype、C89-compatible evdev constant header を確認。各構造体の初期化は独立し、Vulkan object の作成・割当・検査は順に個別。呼出しを条件から分離し、成功は最後に明示する。非同値な評価順の入替え、wire ABI / exported API / log string / fd 所有の変更は行っていない。pure な word lookup は既存の引数式を保持する。

break に置き換えた device lookup・network field lookup・nonblocking audio read は、それぞれ停止条件と最終 return の値が以前と同じことを確認。audio の Boolean assignment は明示的な 0 / 1 へ、Vulkan result は status へ変更。失敗時の errno の取得、resource 解放、credential の wipe の順を保存した。共通 compose の acquire 成功後の release は初期化失敗 / 正常終了の各道で 1 回、acquire 失敗・prepare 失敗では release しない。

sysroot の変更はユーザーが承認した p001 の manifest diff の範囲のみ。Makefile の OS source list / include path、host script の source path / include path は既存の compile 形を保つ。kernel / HAL の API・外部 package / lock された toolchain の実装・生成 code は変更していない。新しい shell checker は上記の個別確認、その他は既存 shell / Make の慣習で確認。新しい test-only env control は追加していない。

## 理由つきの残し方・検査の限界

1. 移動した公開 header 24 file の宣言本文・型 / field の従来の簡潔な注釈・license を保持した（p002 の版 21 / audio API の承認差分以外）。p001 の byte 同一性と public ABI を守るため、たとえば `keiui.h` の `kui_scroll` の既存の section / field コメントを全て新しい ownership prose へ書き換えることはしていない。この宣言 header の従来の注釈形を限定的な残し方として記録する。新規実装 / 新規境界型には full standard を適用した。WS105 で実際に ownership が変わる header はその範囲を改めて確認する。
2. 公開 header は C function definition を含まない。checker が `extern "C"` block 内の prototype を function-body paragraph と誤認する 52 件は原文を読んで仕分けした。これは上記の手動の従来コメント形の保持とは別の制約。機械 tool の成功だけでは全文適合を主張しない。
3. 共通 file の未変更箇所の既存報告 68 件（Files 4、IME 9、mview 13、terminal 1、compose 3、input-method 38）は記録したが、関係のない全 file の整形を行っていない。WS104 の変更行に掛かる違反は 0。
4. best-effort の fcntl / close / shutdown、credential diagnostic の明示 wipe、errno の取得、fd / Vulkan object の解放は従来の責務・順を保存する。後続 call より前の即時 refusal を一律に入れると leak / credential retention / errno 上書きになる所では、その cleanup を終えてから拒否する。分類の早期 return は完了した分類の短い branch として保持。各箇所の目的を注釈で説明した。
5. static analysis 専用 tool は要求されておらず別途未実施。機械検査で証明できない ABI / 所有 / 処理順は上記の手動 review と Phase の bounded 回帰で確認。Linux compositor・実機 5330・他 platform は未実施。QEMU の console / serial log は起動・回帰の判定に使わない。

## 最終コメント修正の再確認（2026-10-01T05:43:12.897152+00:00）

実装 cd48e74d110a1e504c2b87ac288d41cdc928367c は回帰時 6efb4f2b の処理本文と同じ（成功出口の注釈 1 行だけ）。build / boot を再確認。main と forge / criteria / notes / Settings / volume の compositor ELF は SHA256 同一で、runtime の結果を維持できる。最終 source manifest・style-scope を更新。`make -j64 disk-image` の再実行 exit 0 は別に保存し、既に更新済みのため stdout が空であった。image の check OK は直前の build-final.log の記録。 [同一性・build の証拠](final-comment-build.json)。
