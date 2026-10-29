<!-- awesome-plan project=zedbsd record=ws284-p003 -->

# ws284-p003 libyolox の骨格と CPU の参照

- Parent: [ws284](../ws.md)、Queue: q1667 / q1667-i04（P6、実装だけ・試験なし、branch agent/p6-ws282c）
- Status: in-progress（実装の途中・試験未実施。2026-10-29 q1667-i05 で build の規則を足した、branch agent/p6-ws282d）

## 実装した事（userland/desktop/libyolox/）

- `yolox.h`（design §3.2 の API と error の番号）、`yolox-internal.h`、`model.c`（ZNET の読みと §2.3 の確かめ全部: header・表・section の境界と整列・op ごとの規則・graph（一度だけ作られる、前に作られた物だけを読む、add/concat の形、head の tensor））、`cpu.c`（NCHW の素直な conv・maxpool（窓を画像に切り詰め）・upsample・add・concat・space-to-depth、SiLU）、`preprocess.c`（§1.7 の letterbox と 11 bit の整数の bilinear）、`decode.c`（§1.6 の decode と zedBSD の NMS の規則）、`classes.c`（COCO 80）、`runner.c`（config の既定と struct_size、runner（CPU のみ。VULKAN は UNSUPPORTED、submit は NOT_SHARED、collect は IDLE）、detect、read_tensor（debug だけ））、`exports.map`、package の `Makefile`（amd64、既定 n）。
- 確かめた事: host の cc（`-std=c11 -O2 -Wall -Wextra -Werror -c`）で 7 つの .c が warning 0。zedBSD の build は未実施。

## 再開点（2026-10-29 P6）

1. zedBSD の package の build: `plan/ws284/tests/config.mk`（`ZEDBSD_USER_PROGRAMS += libyolox`）で `make ... build/ws282/p6c-amd64/dynamic/libyolox.so` は「No rule to make target」で rc 2（2026-10-29）。shared library の規則は `platform/common/userland-dynamic.mk` に library ごとに書かれている（libmedia の例）ので、そこに libyolox を足す必要がある（共有の build の file なので未着手、Q1 の判断）。arm64（rpi5）を arch に足すかの判断も。
2. host の試験 T2b（小さな合成の ZNET を書いて壊す）・T2（make-reference.sh の出力と比べる program）を書く。`Makefile.linux` は未作成。
3. graph.c（GPU の融合と分割と割り当て）は p004 と一緒に。p004 の Vulkan の backend は未着手。
4. p002 の残り: package `yolox-nano-weights`（取得・変換・固定の SHA-256・install・NOTICE・manifest。固定の SHA-256 は変換を一度流すまで決まらない）は未着手。変換の script（cf0b079e5）と参照の script（ffeaeced5）は書いた、流していない。

## 2026-10-29 P6（q1667-i05、agent/p6-ws282d）

- 再開点 1 を済ませた: `platform/common/userland-dynamic.mk` に `$(DYNAMIC_DIR)/libyolox.so` の規則（libc だけを link、`exports.map`、`check-dynamic-elf.py`）。`plan/ws284/tests/config-rpi5.mk`（新規）。`make ZEDBSD_CONFIG=plan/ws284/tests/config.mk BUILD=build/ws284/p6d-amd64 build/ws284/p6d-amd64/dynamic/libyolox.so` と rpi5 の同じ物が rc 0・warning 0（log は worktree の `build/ws282/logs/ws284-libyolox-*.log`）。実行は未実施。
