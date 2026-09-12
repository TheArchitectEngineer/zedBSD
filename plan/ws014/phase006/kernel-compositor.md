# p006: kernel handle・fd・zwlの実装契約

2026-09-13、q309 / q309-i01。担当範囲の実装と限定検証を記録する。Phase・Queueの受入状態は変更しない。GPU/Venus、libwayland、Vulkan WSI、最終実QEMU受入は各担当の証拠と合わせて評価する。

## Kの参照寿命

[`handle.h`](../../../include/kern/handle.h) / [`handle.c`](../../../src/kern/handle.c) は、VFS file・inodeを作らず、型・参照数・release callback・`void *object`を持つ独立wrapperを確保する。Uへカーネルポインタは公開しない。汎用opsは `kernel_handle_ops.release(object)` のみ。

| API | 成功時の所有権・エラー規約 |
| --- | --- |
| `handle_create(type, ops, object, &h)` | 0 / 正のerrno。成功時のみpayload所有権を取り、wrapperの参照1を返す。失敗時payloadは呼出元所有のまま。 |
| `handle_get(h)` / `handle_put(h)` | 既存の強い参照またはfd table lockで保護されたhをgetする。最終putはpayloadのreleaseを一度呼び、wrapperも解放する。 |
| `handle_fd_create(h, flags)` | fd / 負のerrno。現在プロセスのfd tableへ追加参照をinstallする。呼出元の入力参照は常に残る。flagsは `O_CLOEXEC` / `O_CLOFORK` のみ。 |
| `handle_fd_get(fd, expected_type)` | 現在プロセスのfdをtable lock下で強く参照し、完全一致するimmutable typeを確認して返す。不在・異種はNULL。呼出元はputする。 |

[`fd-object.h`](../../../include/kern/fd-object.h) / [`fd-object.c`](../../../src/kern/fd-object.c) の小さなtag付き共通参照がfileまたはhandleを保持する。socketは既存file経由のまま。単純な構造体コピーは参照追加ではない。`get`が追加し、`put`はcarrierを先に空にしてからdestructorを呼ぶ。

[`filedesc.c`](../../../src/kern/filedesc.c) のgetはtable lock下、最終releaseはlock外。close、dup/dup2、fork、CLOFORK、exec/CLOEXECを共通参照へ統合し、既存file専用APIはwrapperとして維持した。install成功は供給参照を消費し、失敗は消費しない。`filedesc_commit_objects` は全slotを検証して一括publishし、成功時carrierを空にする。fdをUへcopyoutする処理はreserve → copyout → commitを使い、失敗後に数値fdをcloseすることで別threadの再利用fdを閉じるrollbackは使わない。

[`syscall.c`](../../../src/kern/syscall.c) ではhandleの `F_GETFD` / `F_SETFD` / `F_DUPFD*` を扱う。非I/O handleのstatus/owner/lock系fcntl、fstat、mmapは `EOPNOTSUPP`、ioctlは `ENOTTY`。既存file専用read/writeは `EBADF`。[`poll.c`](../../../src/kern/poll.c) は生きたhandleを有効・非readyとして扱い、`POLLNVAL` にしない。これはfence/readiness機能を追加したことを意味しない。

## SCM_RIGHTSと中断時の回収

[`unix-socket.c`](../../../src/kern/net/unix-socket.c) とsyscallの送受信処理は同じ `fd_object` を運ぶ。送信待ちmessageが強い参照を持つため、送信元fdを閉じても受信前にpayloadは消えない。受信transactionは一時参照を取り、予約・copyout失敗でabortしてもmessageを残し、再受信できる。commit、control切り詰め、送信失敗、未読messageを含むsocket破棄は、それぞれ所有する参照だけを落とす。

fdの本数だけではGPU使用中・mapping・scanoutの寿命を判定しない。GPU側の独立importとbackend参照がその保持を担う。プロセスの強制終了でもfd tableとsocket queueのK参照は通常の終了処理で回収され、GPU openの終了はGPU側のscanout撤去経路へ進む。GPU撤去失敗時の不確実なハードウェア使用を、汎用handle層だけで「解放済み」と判断しない。

## zwlの通信・表示・アプリ所有権

| source | 責務 |
| --- | --- |
| [`main.c`](../../../userland/base/zwl/main.c) / [`zwl.h`](../../../userland/base/zwl/zwl.h) | 有限poll loop、複数client、独立GPU open、SIGINT/SIGTERM終了、socket世代管理。 |
| [`wire.c`](../../../userland/base/zwl/wire.c) | 標準のnative-endian header、独立byte/fd FIFO、SCM_RIGHTS、短いread/write、bounded error flush。 |
| [`protocol.c`](../../../userland/base/zwl/protocol.c) | registry/version、core surface、fullscreen xdg-shell configure/ack、commit、private buffer factory。 |
| [`objects.c`](../../../userland/base/zwl/objects.c) | client別object ID、buffer destroy後の遅延寿命、frame.done、buffer.release、切断回収。 |
| [`display.c`](../../../userland/base/zwl/display.c) | authoritative metadata付き独立GPU import、mode選択、lease、完了したscanout置換。 |
| [`Makefile`](../../../userland/base/zwl/Makefile) | amd64 packageと `/bin/zwl` の登録。 |

公開するのは `wl_compositor` v4、`wl_output` v2、fullscreen用 `xdg_wm_base` v1、`zed_gpu_buffer_v1` v1。private factoryのopcode 1 / signature `nha` は、new_id、SCM_RIGHTS fd、64-byte metadata arrayから通常の `wl_buffer` を作る。fdはwire payloadに数値wordを持たない。完全なframeのbytesよりfdが遅れて到着しても、frameを消費せず保留する。未読fdは切断時にcloseする。

compositor自身のGPU fdへ `GPU_RESOURCE_IMPORT` し、Kが返す全metadataをwireの主張と比較する。importは受信fdを消費しないため、request側がcloseする。選択extentを `GPU_DISPLAY_MODE_VALIDATE(refresh=0)` に渡し、返却refreshをoutput eventとPRESENTに使う。描画完了をproducer側が保証した画像を `GPU_DISPLAY_PRESENT_FIFO | GPU_DISPLAY_PRESENT_BLOB` で表示する。通常の共有・表示経路にCPU readbackはない。

pending、queued、current、frontは別のbuffer保持。前のfrontは次のPRESENT成功またはDISPLAY_RELEASE成功までreleaseしない。未表示queued bufferの置換はMAILBOXとしてreleaseできる。frame.doneは表示処理の進行通知で、表示中bufferのreleaseとは別。metadataだけの再commitは直前のqueued/current contentを保つ。

`wl_buffer.destroy` はprotocol identityを退役させるが、使用中importは保持する。Vulkan swapchainの破棄からアプリ所有の `wl_surface` を暗黙unmapしない。明示null attach+commitだけがxdg configure/ackをresetし、remapにはemptycommit → 新configure → ack → bufferが必要。surface破棄または切断はscanoutを撤去してからそのclientの保持を解く。

通常のsocket cleanupはbind後のdevice/inodeを照合し、置換されたpathnameを削除しない。同じ権限の悪意あるpathname置換に対するatomic compare-and-unlink機構ではない。SIGKILLではUのcleanup/log/unlinkは走らず、Kがfdを回収する。再起動試験はharnessが自分で作った停止済み世代のpathだけを削除する。

```
/bin/zwl --socket=/tmp/wayland-0 --timeout=150
ZWL READY socket=... width=... height=... timeout_ms=... pid=...
ZWL PRESENT ... flags=3 refresh=...
ZWL EXIT frames=... error=0 cleanup_failed=0 pid=...
```

READY/EXITのpidでharnessが起動した世代を識別する。既存pathは起動時に削除せずbindを失敗させる。デフォルトextentは320×240。入力、装飾、popup/interactive resize、一般DE、複数output、汎用libwayland-server、全Wayland互換/CTS認証は対象外。対応しないrequestは成功を装わずprotocol errorにする。fenced scanout選択完了は物理host vblankの保証ではない。

## 限定検証と証拠

リポジトリrootから実行する。

| command | 実コードと確認内容 | 保存先 |
| --- | --- | --- |
| `sh plan/ws014/tests/run-handle-fd-test.sh` | 実handle/fd/poll/AF_UNIXの寿命、fd予約、dup/fork/exec、型、不在、送信元close、受信abort/retry、切り詰め/未読rights回収。実sys_sendmsg/recvmsg/fcntl/ioctlを使うcopyout失敗rollbackとfd flags/type error。通常・ASan/UBSan PASS。 | [`build/q309-handle-fd/`](../../../build/q309-handle-fd/) のordinary/sanitizeおよびsyscall各log。 |
| `sh plan/ws014/tests/run-zwl-test.sh` | 実wire/SCM/objects/protocol/displayをリンクしGPU ioctl完了だけを制御。configure/remap、FIFO/frame/release、MAILBOX、複数client、表示中destroy/ID再利用、present失敗、遅延fd、control truncation、実socket backpressureのbyte一致、切断・socket世代保護。通常・ASan/UBSan PASS。 | [`build/q309-zwl/`](../../../build/q309-zwl/) のordinary/sanitize各log。 |

fixturesは [`handle-fd.c`](../tests/handle-fd.c)、[`handle-fd-syscall.c`](../tests/handle-fd-syscall.c)、[`zwl-protocol.c`](../tests/zwl-protocol.c)。各binaryは20秒timeoutで有限化し、ASan leak検出とUBSan停止を有効にしている。zwlのtarget header構文検査も `cc -std=gnu11 -Wall -Wextra -Werror -DKERN_USER_ABI_LP64 -Iinclude -Ilibc/include -Iinclude/uapi -fsyntax-only userland/base/zwl/{main,wire,objects,protocol,display}.c` でPASS。global target buildと最終image固定はroot担当。

初回実QEMUの [`q309-wayland-001/evidence/console.log`](../temp/remote/q309-wayland-001/evidence/console.log) には、producer終了後の独立context import/GPU-copy 1024 pixels一致、FIFO/MAILBOX各6 frame、各swapchain recreate、zwl終了12 frames / error=0 / cleanup_failed=0を記録。pixel oracleとcaptureは同じevidence directoryにある。この初回実測は後続の明示remap修正・PRESENT flags/refresh・EXIT pid追加前であり、最終ソースのlifecycle受入はrootの後続試験で確認する。

HAL変更、git add/commit/push、GitHub公開、Phase/Master/Queueの状態変更はこの担当では行っていない。

## 最終規約整理と再検証

`plan/coding-style.md` §14を新規production sourceとq309変更部分へ適用し、宣言・public/static順・独立参照・lock境界・関数呼出と条件の分離・semantic paragraph・brace/空行・成功/失敗returnを再確認した。無関係な既存syscall/network処理の全体リファクタリングへは拡張していない。wltestのmain/window/headerも整理し、1000ms delayの秒/nsec正規化・nanosleep/EINTRの扱い・Wayland呼出順を保持した。xdg-shell headerは他OSの生成headerも渡せる標準入口 `<xdg-shell-client-protocol.h>` を使用する。

影響するK・直接syscall・zwlの上記fixtureを通常・ASan/UBSanで再実行してPASS。target headersを使うGCC `-std=gnu11 -O1 -Wall -Wextra -Werror -fanalyzer` の実compileは、handle/fd-object/filedesc、zwl wire/objects/protocol/display、wltest main/windowでPASS。`git diff --check`もPASS。

zwl/main.cのanalyzerには `listen_socket` のbind失敗returnでfd-leak診断が一件残る（[`zwl-main-analyzer.log`](../../../build/q309-wltest-style/zwl-main-analyzer.log)）。listenerはcallerの `server->listener` が所有し、mainは初期化失敗でも `service_cleanup` へ進む。追加した実socket fixtureで、失敗したbindのcaller cleanup後にlistenerがEBADFとなり、既存pathnameは保持されることを通常・ASan/UBSanで確認した。callerへ保持された所有権をanalyzerが追跡できない診断と判断し、warningを隠すproduction分岐や抑制pragmaは追加していない。最終実QEMU/lifecycleの判断はrootの後続証拠で行う。


## MSG_PEEKのレビュー訂正と追加検証

最終レビューで、file互換wrapperのpeek成功後に参照が失われるという報告をしたが、これは誤報だった。実際の `unix_socket_receive_begin` はrights付きmessageへの `MSG_PEEK` を一時参照取得前に `EOPNOTSUPP` で拒否するため、指摘した経路には到達しない。peek成功を期待した追加fixtureもこの既存契約を誤解しており、失敗はproductionの参照寿命欠陥を再現したものではない。最初の観測は [`peek-contract-original-failure.log`](../../../build/q309-handle-fd/peek-contract-original-failure.log) に保存した。wrapperへ一時的に加えた変更だけを元に戻し、既存のrights付きpeek非対応契約を維持した。

`handle-fd.c` の追加fixtureは送信元をcloseしてqueueだけがfileを保持する状態を作り、peekの `EOPNOTSUPP`、出力参照なし、queueの生存を確認する。その後の通常受信と、別messageを未読のままendpointを破棄する場合をそれぞれ実行し、最後の所有者からの解放が各一回であることを確認した。`sh plan/ws014/tests/run-handle-fd-test.sh` は直接syscall fixtureも含め、通常・ASan/UBSanの全4実行でPASS。追加したのは契約検証で、rights付きpeekの対応を追加したわけではない。

復元後の `src/kern/net/unix-socket.c` はSHA256 `67e2619451b4a978b7f39de98551ae0dd2386e860527fbf386646914e7fce479` で、`q309-wayland-004` のbuild前後manifestと一致する。`q309-direct-002` が記録するproduction source 99件、`q309-wayland-004` の132件も、それぞれbuild前後の全記録と一致した。照合対象は `src/`・`include/`・`libc/`・`userland/`・`platform/` 配下の記録で、末尾 `~` のbackupを除く。direct002はunix-socket.cを記録していないため、その未収録fileをdirect002で照合したとは扱わない。[`accepted-production-hashes.json`](../../../build/q309-handle-fd/accepted-production-hashes.json) に件数・不一致なし・収録有無を保存した。今回productionの挙動変更は残しておらず、この訂正のための実VM再試験は行っていない。

## q309最終受入

最終kernelでq309-direct-002とq309-wayland-004を受入済み。先行実測時点の未完了記述は履歴として保持し、現状は[結果と失敗履歴](results.md)および[最終証拠](final-evidence/verification.json)を参照する。p006 cleared、p004はplanning・未queue。
