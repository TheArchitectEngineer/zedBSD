<!-- awesome-plan project=zedbsd record=ws001-p041 -->

# ws001-p041: ls の XCU の option（台帳 #73）

Status: in-progress（2026-10-07 q834 P2: 実装と host の差分 24/24、zedBSD の build warning 0。guest の回帰は p043 の後に T1 にまとめる）
Parent: [WS001](../ws.md)
Queue: q834（2026-10-07、P2）

## 範囲（Q1 の ACK 2026-10-07、正常系）

`userland/base/ls/main.c` に XCU の足りない option: -A・-c・-u・-f・-g・-o・-H・-k・-p・-s・-S。block の単位は XCU どおり 512 byte（-k で 1024、-l の total も）。

## 実装

- -A（dot の名前、. と .. を除く。-a と後の物が勝つ）、-f（directory の順、. と .. も）、-c・-u（-t と -l の時刻。-l でなければそれで並べる: GNU と同じ）、
  -S（大きい順、-t と後の物が勝つ）、-g・-o（-l から owner・group を除く）、-H（command line の symbolic link を辿る）、-k、-p（directory に /、-F が優先）、
  -s（名前の前に block の数、どの形式でも total の行）。-t の比較は秒と nanosecond（GNU と同じ）。
- total と -s は 512 byte の block（前は 1024、GNU の既定）。-h は大きさのまま。

## 確かめ（2026-10-07）

- host: `python3 plan/tools/utils/util-diff.py --bin build/ws001-p041 --only ls` → 24/24（GNU coreutils 9.7 の POSIXLY_CORRECT と、新しい case `plan/tools/utils/cases/ls.sh`）。
- 旧版との比較 `plan/tools/utils/ls-compare.sh`: 違いは 11 組で、どれも意図した物（-l の total が 512 byte の単位、-t の同じ秒の中が nanosecond の順）。
- zedBSD: `make ZEDBSD_CONFIG=config/ci/config-amd64.mk BUILD=build/ws001-p041z build/ws001-p041z/bin/ls` warning 0。style-check 0。
- host の build 一覧（`plan/ws001/tests/build-host-ws001.sh`）に ls を足した。
- guest: 未実施（p043 の後に T1 へ）。

## GNU と違う所

- 指す先の無い symbolic link を -H・-L で command line に書くと、GNU は「cannot access」で status 2、zedBSD は link そのものを書く（今までの -L と同じ、XCU の「link そのものの情報」）。case から外した。

## 積み残し（backlog へ）

locale の照合（C 以外）、block・character の device の major/minor の欄、-l の時刻の locale の書式。
