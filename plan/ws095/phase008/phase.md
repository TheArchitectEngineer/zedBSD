<!-- awesome-plan project=zedbsd record=ws095-p008 -->

# ws095-p008: zdesktop の自前の field と Files の field

Status: in-progress（2026-10-06 q820、P1。source の変更は無し（下の確認のとおり前の Phase で済んでいた）、guest の試験 `ime-p008.sh` を足して T1 待ち）
Disposition: normal
Parent: [WS095](../ws.md)
Queue: q820（P1）
Prerequisites: p005・p006、WS127（Files 最重点）との merge 順
Investigation bound: timebox 3〜4h

## 範囲

- titlebar の検索（`wayland/titlebar-shell.c`、design §4.4）で compositor 内の field に IME を結ぶ（compositor 自身が text input の相手になる経路）。
- Files（`userland/desktop/files/`）の検索・rename の field。

## 受け入れ

titlebar の検索と Files の検索・rename に日本語を入力・確定できる PNG。Files の host 試験（`plan/tools/files/`）に回帰無し、`boot-test.sh`。

## 所有 path

`userland/desktop/wayland/titlebar-shell.c`（compositor）、`userland/desktop/files/`、`plan/ws095/`

## 未決の判断

WS127 の Files の作業と同時に行うか、後にするか（main）

## Standards / evidence

[Guardrail](../../guardrail.md)、[C全文規約](../../coding-style.md)、[design.md](../design.md) を変更前に読む。新しい C は全文規約。zedBSD の起動は `plan/tools/boot-test.sh` だけ、guest の操作は serial/SSH、画面は PNG で目視しユーザーに見せる。serial/console log を判定に使わない。共有 `build/` と toolchain は読取専用。全体の `make check` は禁止。QEMU の証拠と実機の証拠を分け、やっていない確認は未実施と書く。具体コマンド/結果は実行時に記録する（今は計画のみ）。

2026-10-02 / ws095-beta1-plan-20261002: 計画担当が作成。

## 実施（2026-10-06、P1）

### 調べ（source はもう済んでいた）

- titlebar の検索・path の欄（compositor 自身の field、`wayland/titlebar-shell.c`）は、BUG-177 の作業で IME の相手になっている: `wayland/input-method.c` の `IME_FIELD_TITLEBAR`（`kwl_titlebar_field_surface`・`kwl_titlebar_field_state` で text と caret を input method に渡す）。Files の検索はこの title bar の欄（`KL_CONTROL_SEARCH`、Files は `KL_WINDOW_CONTROL_TEXT` で query を受ける）なので、同じ経路で日本語が入る。
- Files の名前の変更の欄は ws090-p022・p023 で `kl_field`（`files/rename.c`）にし、`FM_EVENT_TEXT`・`_DELETE`・`_PREEDIT` で input method の commit・削除・preedit を受ける。
- よって WS127（Files）との file の調整は要らない（この Phase で Files の source を変えない）。

### 試験

`plan/ws095/tests/ime-p008.sh`（新、IME の image `build-ime-image.sh`）: (1) Ctrl+F で title bar の検索欄、Alt+Space、"nihongo"、Space で preedit（`search-preedit.png`）、Enter で 日本語 を確定し Files の query が 日本語、(2) Esc、Ctrl+A、F2、"kanji"、Space、Enter で名前の欄に 漢字、Alt+Space、Enter で `a.txt` → `漢字.txt`（Files の RENAME の行と folder の一覧、`renamed.png`）。判定は log・SSH の一覧・PNG（console の log は使わない）。

未実施: QEMU（T1）、boot-test（T1）。
