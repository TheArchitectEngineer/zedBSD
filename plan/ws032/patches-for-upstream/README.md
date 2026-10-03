# ツリー外プロジェクトへ当てるパッチ

zedBSD 側の変更によって、外部プロジェクトの zedBSD 向け回避コードが不要になったり、
compile できなくなったりした場合、その修正をここにパッチとして置く。**このリポジトリ
では適用しない**（当該プロジェクトの upstream が持つべき変更のため）。

| パッチ | 対象 | 内容 |
| --- | --- | --- |
| `noct-0001-drop-zedbsd-sigaction-workaround.patch` | NoctLang（`src/api/api-term-ansi.c`、`src/cli/cli-repl.c`） | `#if defined(NOCT_TARGET_ZEDBSD)` の sigaction 回避分岐を削除する |

## noct-0001 の背景

zedBSD の `struct sigaction` は `sa_handler` が関数ポインタではなく `uint64_t` だった
ため、Noct は zedBSD 向けに `(uint64_t)(uintptr_t)handler` と詰める分岐を持っていた。
WS032 で UAPI を POSIX 準拠（`sa_handler` は `void (*)(int)`）に直したので、この分岐は
compile error になり、`#else` 側が正しい形になった。

適用: `patch -p1 -d <NoctLang のソースツリー> < noct-0001-drop-zedbsd-sigaction-workaround.patch`

対象とした Noct の版は、`userland/base/noct/version.mk` が pin している版
（本パッチ作成時点で 2.0.1、archive SHA-256 `0083328e…`）。
