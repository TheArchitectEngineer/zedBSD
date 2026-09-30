<!-- awesome-plan project=zedbsd record=ws099-p016 -->

# ws099-p016: zdesktop の buffer の import の短縮

Status: cleared（2026-09-30、サブエージェント P4、worktree `ws090-widgets`（branch `wt/ws090`）。QEMU の Venus だけ。5330 は実機の lock（`/tmp/i915-hw.lock`）が 2 回見て使用中で、未実施）
Disposition: normal
Parent: [WS099](../ws.md)
Queue: なし（2026-09-30 Q1 の割り当て）

## 範囲と受け入れ

ws094-p009 で分かったこと: zdesktop は client の GPU の buffer（wl_buffer）ごとに、Vulkan の image を import するとき、layout の変更を
1 回だけの command buffer で submit し、`vkQueueWaitIdle` で待っていた（`import.c` の `import_layout`）。この待ちは、走っている合成の frame の
終わりも含むので、QEMU の Venus で 1 枚約 100 ms（import 全体は約 210 ms）かかっていた。swapchain の 3 枚で、app の最初の frame が遅れていた。

- この待ちを無くす（layout の変更を次の合成の command buffer の barrier にまとめる）。
- 前後で測る: app の起動から zdesktop が最初に描くまで（WS094 の (a')、Files の perf100）、App Home の icon の click から app の最初の frame まで。
- 5330 は lock が空いていれば 1 run。
- 他の agent と file を分ける（import.c・compose.c の周り）。
- 回帰: C9、WS079-p010、Model viewer と wlshm の起動（Vulkan と wl_shm）、boot test。

## 設計

- `zwl_import` に `layout_pending` を足した。`zwl_compose` には、前の frame の後に import した image の表 `layouts`（最大 `ZWL_LAYOUTS_MAX` = 32）を足した。
- import（`import_image`）は、layout の変更を submit しない。image を表に足すだけにした。表が満ちていたときだけ、今までどおり submit して待つ。
- 次の合成（`compose_record`）は、command buffer を始めた直後、render pass の前に、表の image の barrier をまとめて 1 回記録する（`zwl_import_layouts_record`）。
  - barrier の中身は前と同じ: UNDEFINED → GENERAL、TOP_OF_PIPE → FRAGMENT_SHADER。
  - image を初めて sample できるのはその frame なので、barrier はそれより前に効く。
- その frame が submit できた後で表を空ける（`zwl_import_layouts_done`）。submit できなかった frame の barrier は、次の frame がまた記録する。
- 表にある間に buffer が消えたら、`import_release` が表から外す。
- 前との違い（危険の見積もり）:
  - 前は client が描く前に layout を変えていた。今は client が描いた後、最初の合成で変える。
  - UNDEFINED からの変更は、規格の上では中身を捨ててよい。
  - ただし、この image は外部のメモリの linear の image で、圧縮や tiling が無い。QEMU の Venus（Lavapipe）で中身は保たれた（下の画面）。
  - 5330 の i915 の実行器での確認は未実施。
- 変えた file:
  - `userland/desktop/wayland/import.c`・`compose.c`・`compose.h`。
  - 計測の log のために `home.c` に 1 行: `ZWL HOME launched waited_ms at_ms`。icon の click から窓の最初の image まで。`zwl_home_launched` の中の printf だけ。
  - P6・P1・P3 の file（display.c・seat.c・shell.c・backdrop.c・glass の周り・keyboard.c）は触っていない。

## 計測（QEMU の Venus、1280x800、`build/ws081/demo-win-venus.img` の複写に build した wayland・files・library を入れた guest）

| 値 | 前 | 後 | 差 |
| --- | --- | --- | --- |
| WS094 (a'): `files --desktop` の起動 → zdesktop が最初に描く（perf100、3 回の中央値） | 2977 ms | **2652 ms** | −325 |
| App Home の icon の click → Files の窓の最初の image（`import-launch.sh`、3 回の中央値） | 2534 ms | **2286 ms** | −248 |
| 同じ → その後の最初の合成の frame | 2655 ms | **2407 ms** | −248 |
| App Home → Model viewer の窓の最初の image（前は 1 回目が App Home の待ちの外で 2 回の中央値） | 4338 ms | **4079 ms** | −259 |
| 同じ → 最初の合成の frame | 4439 ms | **4182 ms** | −257 |

- どの run も import は 3 枚（`ZWL IMPORT`）。短縮は約 250〜330 ms で、3 枚 × 約 100 ms の待ちに合う。
- 残りは、Venus の同期の呼び出しの約 10 ms（F-064）と、app 自身の起動・Vulkan の準備。
- C5 の試験（`c5-transitions.sh`、App Home と Wiseview の開閉の最初の frame）は import を通らない。後で FAIL のまま（first_max 215 ms、p001 の 102〜215 ms と同じ）で、この Phase の範囲では変わらない。
- 5330: 未実施。

## 確認

| 確認 | 命令 | 結果 |
| --- | --- | --- |
| build | `make -j16 ZEDBSD_CONFIG=plan/ws035/tests/config-amd64-zdesktop.mk BUILD=build/amd64 build/amd64/bin/wayland …`、`plan/ws099/tests/build-criteria-image.sh build/amd64` | rc 0、warning 0 |
| style | `plan/tools/style-check.py`（import.c・compose.c・compose.h・home.c） | 0 件 |
| 計測 | `plan/ws099/tests/import-launch.sh`（新規）、`plan/ws094/tests/files-desktop-guest.sh … install perf100` | 上の表 |
| 回帰: C9 | `plan/ws099/tests/criteria.sh build/ws099-p016-criteria.img build/ws099-p016/criteria C9 C5` | C9 の 10 本すべて PASS（p052・p053・p072・p076・p126・p128・p134・p137・p138・cursor-owner）。C5 は FAIL（上、前と同じ） |
| 回帰: WS079-p010 | `plan/ws079/tests/zdesktop-p010.sh build/amd64 build/ws099-p016/p010` | PASS |
| 回帰: Vulkan と wl_shm の client | zdesktop の上で `wlshm`・`mview --windowed`・`wltest --windowed` を起動（`ZWL MAP` 3、`ZWL IMPORT` 6、ERROR・FAILED 無し）、App Home からの Files・Model viewer（上の計測） | PASS（`build/ws099-p016/clients.png`、`after/Files.png`、`after/Model-viewer.png`） |
| boot test | `OUTPUT=build/ws099-p016-boot plan/tools/boot-test.sh build/ws099-p016-criteria.img` | PASS（`build/ws099-p016-boot/login.png`） |
| 5330 | lock の確認 `flock -n /tmp/i915-hw.lock` | 2 回とも使用中。未実施 |

## 残り

- 5330 での前後の計測と、i915 の実行器で import した image の中身が保たれることの確認（lock が空いたとき）。
- App Home の「Shared memory」は、この image の App Home の 1 ページ目に無かった。そのため wlshm は App Home ではなく、直接起動で確かめた。
