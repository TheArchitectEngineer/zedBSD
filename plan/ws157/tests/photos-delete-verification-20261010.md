# Photos メディア削除: 2026-10-10 検証

Owner: codex/ws197-media-uat、base/main `39ebb1b06f6549f454ce974242a540725c7b0c1b`。
Queue: [photos-delete-20261010](../codex-queue.md)、[p010](../phase010/phase.md) / [p011](../phase011/phase.md)。

## 実装と確認条件

Photosで選択→Photo→Delete…（またはDeleteキー）→確認。取り込んだMedia内のコピーとmetadataを削除し、取り込み元を残す。実装方式の任意質問は未回答、確認dialog付き完全削除の前提をユーザーへ明示して進めた。実機・既存のユーザーメディアは削除していない。

経路は `Photos worker → kl_system_media_remove_ids → manager31/media v2 → compositor → libkeiland-backend posix_spawn/pipe → mediastorage remove-list`。CLIが同じユーザーのMedia/metadata.dbとFiles内のコピーを変更し、lock解放後にlibkeilandでnotify、compositorがwatcherへCHANGED、Photosがmetadata snapshotを取得する。画像・動画本体をWaylandで送らない。新APIのFDはcaller-owned。manager29/30にはmedia v1を作り、新remove APIはENOTSUP。

Stable IDはUI確認中・worker中ともコピーし、配列index/caller pathを削除対象にしない。CLIは全文ID検証とdedupを先に行う。削除したIDのalbum membershipを除去、他の拡張JSON情報を保つ。残り0件でもmetadataを保存する。他windowの古いmarks/album編集で削除IDを復元しない。

## 有限host試験

private workspace: `.claude/worktrees/ws197-media-uat/build/ws197-media-uat/`。入力は専用のtemp HOMEと生成fixtureだけ。

| Probe | 結果 |
| --- | --- |
| `photos-delete-cli.py` + production Linux mediastorage | PASS: image/video削除、import source保持、他copy保持、album参照除去、未知root/album/残るmediaのJSON field保持、malformed list事前拒否、duplicate ID、stale marks/membership、最終1件の削除と空DB再読込 |
| `photos-delete-wire.c`（existing host-media-wire fixtureの限定拡張） | PASS: production libkeiland/relay/static backend/CLIでAPI削除→snapshot、独立CLI削除→watch CHANGED→再取得。manager31/media v2とmanager29/media v1/ENOTSUP。ASan/UBSan/LSan異常無し |
| `photos-delete-rollback.c`（production database.cにhostだけのrename失敗注入） | PASS: metadata.dbのreplacementをEIOにするとmanaged fileの元pathを復元、旧JSON再読込でID保持。ASan/UBSan/LSan異常無し |
| `photos-delete-worker.c`（production media-worker.c、controlled API peer） | PASS: caller ID変更後もコピー保持、UIのqueue/take即時、busy拒否、caller-owned FDとremoved flag、cancel/stop/restart。ASan/UBSan/LSan異常無し |

CLIの最終source build後の実行:

```sh
LD_LIBRARY_PATH=build/ws197-media-uat-linux/lib python3 build/ws197-media-uat/photos-delete-cli.py
```

wire probeはGCC14.2とsystem Wayland server/clientを使用、`-fsanitize=address,undefined -fno-sanitize-recover=all`、production `wayland/media-library.c`, `libkeiland-backend/media/media.c`, `system/system-protocol.c` とLinux libkeilandをlink。headerはprivate include directoryにkeiland.hだけ複写し、system Wayland headerとrepoの同梱headerを混ぜない。temp HOMEのmanager31/29各1回、timeout25秒。host socketとLSanに必要なsandbox外実行でPASS。最初のsandbox下LSanのptrace制限は確認不能として扱い、承認済みhost実行で検証した。productionにtest-only switchは追加していない。

rollbackとworkerは各短いhost probe。実際のGPU/UI描画、耐久、停電時の復旧は検証していない。

### Private probe SHA256

| Artifact | SHA256 |
| --- | --- |
| photos-delete-cli.py | a3f18257eebadb90b7cfe659b586d3a2a73f3e1acd8393e2d0f7a057dfe2deb8 |
| photos-delete-wire.c | fa16f068bab812da5595fab2493a1cd852ae09f7d02370c14fec00a74611aafd |
| photos-delete-rollback.c | 3899d3993c3ef23b06aff825775c631eb192548e6d94afec420a436f13c537f1 |
| photos-delete-worker.c | 29153c72df9fbf377629f1d24c769d729e34fd0256414c6fbfcee4922dfb9663 |

## Named build

zedBSD amd64は既存config.mkから作った本セッションの `config/current-uat.mk`、共有sysroot/toolchainはread-onlyで使用。別セッションのbuild directoryは変更しない。

```sh
make -j8 BUILD=build/ws197-media-uat ZEDBSD_CONFIG=config/current-uat.mk \
 ZEDBSD_SYSROOT_AMD64=/home/awe/zedBSD-claude1/build/amd64/sysroot \
 -o /home/awe/zedBSD-claude1/build/amd64/sysroot/.zedbsd-sysroot-complete \
 -o /home/awe/zedBSD-claude1/.claude/worktrees/ws197-media-uat/build/llvm/.zedbsd-install-23.1.0-zedbsd8 \
 'DYNAMIC_CPPFLAGS=-nostdinc -I. -Iinclude -Iuserland/desktop/include -isystem /home/awe/zedBSD-claude1/build/amd64/sysroot/usr/include -DHAL_ARCH_AMD64 -DKERN_USER_ABI_LP64 -DKERN_DYNAMIC_LIBC' \
 build/ws197-media-uat/bin/mediastorage build/ws197-media-uat/bin/photos \
 build/ws197-media-uat/bin/wayland build/ws197-media-uat/dynamic/libkeiland.so

make -f userland/desktop/keiland-linux.mk -j8 \
 KEILAND_LINUX_BUILD=build/ws197-media-uat-linux \
 KEILAND_LINUX_EMOJI=/home/awe/zedBSD-claude1/build/distfiles/NotoColorEmoji-2.047.ttf \
 build/ws197-media-uat-linux/lib/libkeiland.so build/ws197-media-uat-linux/bin/mediastorage
```

最終両build exit0、warning0/error0。target clang23.1.0、host GCC14.2.0。途中でexports.mapの生成漏れを検出→exports.pyを実行して解消。Linux Photos targetはこのMakefileに無く、試行時にNo ruleとなったためLinux Photos build成功とは扱わない。zedBSD Photosはcompile/link済み。

private final log SHA256:

- photos-delete-build.log: `14b1c0a7e627826bb9abbfe0a78673f9c7e710504546c98dc57282ad46f990f5`
- photos-delete-linux-build.log: `fb37cf3e23b160609c7cef661588b6154a67694be1538d41b1673c511654d930`

## 全文規約・境界

C全文 [coding-style](../../coding-style.md) §1–14とGuardrail/automationを読み、今回変更した全18 C/headerをmanual review。source scopeはpublic keiland.h、backend header/media.c、system protocol header/table/client、mediastorage database/main/notify、Photos app/library/main/worker header/source/photos/snapshot/view、Wayland media relay。generated exports.mapはexports.pyで生成し `--check` PASS。

ANSI declaration/public-static順、one-line forward宣言、copyright、meaningful paragraph/return、fallible callのguard、short-circuit、ownership、lock/FD寿命、modal確認のID、worker cancel/close、JSON commit/rollbackを確認。edited rangesにclang-format19.1.7、定義argument TABと3節以上のcompound conditionの改行は全文に従って保持。関係ないtableのmass reformatは戻した。

`python3 plan/tools/style-check.py <18 changed C/header> --summary` はkeiland.hの既存paragraph-comment24件だけ。同じHEAD baselineに対して差分比較し、新規finding0、他17 fileは0。既存無関係のheader違反は修正しない。Python3.13.5。`git diff --check` PASS。新しいHAL/UAPI/toolchain/依存/OS-specific header/UNIX socketは追加していない。

## 限界・再開条件

GUI操作は実機でユーザー確認。新libkeiland/compositor/Photos/mediastorageを同じimageに含めて再起動後、選択→Delete…→Cancel、もう一度→Delete、開いている別Photos windowの更新、image/video、元取り込みsource保持を確認する。本セッションは実機更新/desktop restartを行わない。

metadata saveの通常failureはfile復元を試験済み。停電/killでrenameとcommitの間に止まる場合のjournal recoveryは実装しておらず、Media直下の `.deleting-<id>-*` にcopyが残ることがある。複数IDは各項目commitで、後のdisk errorによって先の項目だけ削除され得る。commit後unlink failureはmetadataから除かれてcopyがstagingに残るためerrorと最新snapshotで結果を示す。batch全体のatomicityや停電耐性は主張しない。

QEMU/image build/aggregate make check/負荷・耐久/全回帰無し。WS157全体のベータ3 acceptanceは未完、今回のscoped Queueだけcleared予定。shared Master/Queue/cache投影・GitHub公開はQ1、push無し。
