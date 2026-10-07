<!-- awesome-plan project=zedbsd record=ws001-p042 -->

# ws001-p042: find の XCU の primary と式（台帳 #47）

Status: in-progress（2026-10-07 q834 P2: 実装、host の差分 38/38、zedBSD の build warning 0。guest の回帰は p043 の後に T1 にまとめる）
Parent: [WS001](../ws.md)
Queue: q834（2026-10-07、P2）

## 範囲（Q1 の ACK 2026-10-07、正常系）

式の優先と全 primary、-exec … {} + と ;、-ok、-H/-L、link の loop の検出、-xdev。

## 調べた結果と実装

前からあった物（式の優先 `!` > `-a`（並べるだけでも）> `-o`、括弧、-name・-path・-type・-links・-size・-atime/-ctime/-mtime・-newer・
-user/-group・-nouser/-nogroup・-prune・-depth・-xdev・-print・-exec ;・-ok）は新しい case で確かめた。足りなかった物を `userland/base/find/main.c` に足した。

- `-exec … {} +`: pathname を集め、{ARG_MAX} から 2048 byte、環境、utility とその引数を引いた大きさに収まるだけ 1 回で走らせる（`batch_add`・`batch_run`、
  最後に `batch_finish`）。primary は常に真、utility が 0 以外で終われば find の status が 0 以外。`+` は `{}` だけの引数の直後のときだけ終わりで、
  ほかの `+` は引数（XCU）。-ok は `;` だけ。
- `-exec … ;` の引数の中の `{}`（`x{}y` など）も pathname に置き換える（XCU では実装定義、GNU・BSD と同じ）。utility を走らせる前に stdout を flush する（出力の順）。
- `-perm` の symbolic mode（`u=rw`、`-u+x`、`-g=r`）: chmod の `mode_apply` を共有し、bit の無い雛形に当てる（`+` で who が無ければ umask が効く、chmod と同じ）。
- `-L`・`-H`: 指す先の無い link はその link の情報（`-type l` で見つかる、XCU）。
- loop: 祖先と同じ directory は検査の前に診断し、書かない（前は書いてから診断）。
- 末尾が `/` の operand（`find d/`）の下の名前に `/` を重ねない（`d/s`、前は `d//s`）。

## 確かめ（2026-10-07）

- host: `python3 plan/tools/utils/util-diff.py --bin build/ws001-p042/bin --only find` → 38/38（GNU findutils 4.10.0 の POSIXLY_CORRECT、新しい case `plan/tools/utils/cases/find.sh`）。
- GNU の拡張の case（`--cases plan/tools/gnu-utils/cases --gnu --only misc`）164/164、後退なし。
- `plan/tools/utils/build-host-utils.sh` の find に `chmod/mode.c` を足して build と 38/38 を確かめた。
- zedBSD: `make ZEDBSD_CONFIG=config/ci/config-amd64.mk BUILD=build/ws001-p041z build/ws001-p041z/bin/find`（-Werror）warning 0。変えた所の style-check 0（file の残りは前からの 150 件ほど）。
- guest: 未実施（p043 の後に T1 へ。zedBSD の {ARG_MAX} は 16 KiB なので、長い名前 600 個で `-exec … {} +` が 2 回以上に分かれることを guest で見る）。

## GNU と違う所

- `-perm` の symbolic mode で who の無い `+`（`-perm -+w`）: zedBSD は umask を効かせる（XCU の「`=` は umask に関わらず」の対比の読み、BSD の setmode と同じ）。
  GNU findutils は umask を見ない（umask 022 で `+w` が 0222）。case には入れていない。Q1 に判断点として送った（既定は XCU の読み）。

## 積み残し（backlog へ）

-ok の答えの locale（yesexpr、今は y/Y）、深さ 128 を超える木（今は「nesting limit exceeded」で止まる）、loop の後の位置の回復の詳細。
