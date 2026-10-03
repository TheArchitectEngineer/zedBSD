# Context全体の実停止確認: レビュー用提案

状態: 初回automatic approval review拒否後、具体patch・既存承認根拠・独立レビューと8つの意味profileを確認し、正規の再審査でguest差分を適用した。application.jsonに2026-09-13T12:18:17Zのhash付き適用とtarget syntax PASSを保存した。hostも新しいq312-quiesce/q312-quiesce-delayのbuild/installとINIT socketpair各6caseがPASS。実native停止のVM結果は統合evidenceを参照し、ここではbuild/protocol-only結果と区別する。temporary guest copyでtarget構文確認とtransport/backend各ordinary＋ASan/UBSanの4profileはPASS。新host機構のcontext/proxy各ordinary＋ASan/UBSanの4profileはPASS。native VkDeviceWaitIdleはfixtureが制御するmockであり、新host pairのbuild/install/INIT成功は別の実binary証拠に記録し、実native停止のVM結果と区別する。

## 必要性

descriptor/callbackが0でも、opaque raw streamでGPU_JOB未追跡のnative QueueSubmitが動いている可能性がある。したがって一般UAPIのstop_poll0を返すには、実全VkDeviceのidleを確認する。この変更は承認済みp009 R3の「実停止・pending descriptor退役、確認不能ならquarantine」の具体化であり、停止証明をmanaged libraryへの期待へ弱めない。

## 提案機構

- 新paired profileはexact168B/magic0x5a424453/flags7。proxy/serverの既存exact INIT handshakeも7を要求する。旧strict3は既知の通常job能力として解析するが、libvulkanはnative instance生成前にexact7を必須として拒否する。dirtyな旧strict3の局所stopは非対応としてglobal fallback。
- guest backendは既存SUBMIT_3Dのexact16byte private payload `51 53 42 5a 01 00 00 00 00 00 00 00 00 00 00 00` とCPU0 fenceを非待機投稿する。通常Venus opcode空間と区別し、new ioctl/HAL/QEMU変更はしない。
- hostは後続decoder投稿を閉じ、専用threadでcontext内の全native VkDeviceWaitIdleを実行する。SUCCESSだけをnative access停止の証明とする。DEVICE_LOST・OOM・その他失敗ではACKを出さない。
- 停止後のnonzero markerも拒否し、marker追加と停止開始を同じmutexで直列化する。そのcontextのqueue callback実行も終わるまで待ってからだけ、保留CPU0 fenceをretireする。後続CPU0 fenceもすべて保留し、最後の受信順identityを保存する。retireと後続fence処理をmutexで直列化し、逆順水位やuint32 wrapの偽ACKを防ぐ。
- p008 strict proxyはring0を含む全timelineでsocket断force-retireを禁止している。新fixtureでCPU0を明示して確認する。
- decoder ringが存在するcontextは初版非対応でACKを出さない。永久pending/host失敗/unsupported ringは共通stop期限でglobal quarantineへ進む。強制native GPU cancelは実装しない。
- guestはexact24byte/OK_NODATAかつmatching fence/contextのACKだけを採用する。stop専用ownerは常時予約creditではなく、control slot全使用時はEAGAINで再試行し、共通期限を超えればfallbackする。
- native command投稿可能性を送出前にsticky記録し、失敗/不明でも消さない。metadata-onlyで一度もnative streamを送っておらず、descriptor/callbackも全退役済みのcontextに限りnative idleを省略する。未使用RESERVEDなどが残る場合はhost ACKを要求する。
- guestは実ACK後に未投稿RESERVEDの不明所有権をERRORで退役できる。すでにQEMUへ投稿した旧descriptorは別途実返却を要求し、ACKだけでDMAを再利用しない。
- host context teardownはquiescence threadをjoinしてからnative objectとcallback storageを破棄する。別context/共有allocationの所有権は保持する。isolated buildはrender-server-worker=processを固定し、thread modeでのpeer非干渉を保証しない。

## 適用境界

対象は作業repository内のVenus backendと、private server awe@10.0.10.25の新しいisolated q312-quiesce source/build/installだけ。完成後のdelay専用pairも別prefixに再生成する。既存q311-strictとq312-delayは上書きしない。host system package、GDM、VFIO、物理GPU reset、firmware/HAL、git add/commit/pushは含まない。検証はlocal bounded fixture/target syntaxとprivate QEMU VMに限定する。

自動承認レビューはproduction transport/teardown変更について、exact implementationとblast radiusの明示許可が不足するとして拒否した。拒否されたcallは未実行であり、別toolや分割による迂回は行っていない。rootが具体patchと既存ユーザー承認根拠、fixtureを確認し、正規再審査後にLOCAL guest適用を完了した。初回拒否の記録はapproval-context.mdに保持する。

patchはproposal.jsonのbase hashに対するもの。内部headerの追加fields/宣言とflags7定数だけは事前準備として既に存在するが、guestの実stop機構はapplication.jsonのhashへ切り替え済み。host差分は固定p008 strict sourceに対する独立差分で、licenseを保持する。

## ローカル意味検証

```sh
python3 plan/ws014/phase009/renderer-quiesce/tests/run.py --source /tmp/q312-virglrenderer-1.1.0-quiesce --helpers /tmp/q312-stock-build --output /tmp/q312-quiesce-host-evidence
```

各compile/runを120秒以内、逐次実行した。`evidence/results.json`に実source hashと結果を保存し、短いrun logを同梱した。既存Mesa build helperと固定upstream sourceを使用し、実GPUや他processへ作用しない。

context fixtureは実vkr_context.cをlinkし、独立に作った2physical/3native deviceすべてのidle呼出を検査する。GPU_JOB無しraw仕事、停止後decoder/marker拒否、CPU0受信順wrap、後続CPU0、遅延queue callback、DeviceWaitIdle OOM/DEVICE_LOST、ring拒否、実teardown helperのjoinを確認する。proxy fixtureは実strict proxyをprivate socketpairへ接続し、CPU0 verified prefix1のままsocket断しても未確認fence2をretireしないことを確認する。

thread生成ENOMEMのfault injection、paired INIT不一致、実native VkDeviceWaitIdleの挙動はこのfixtureの実測範囲に含まれない。生成失敗はcode上failedを保持してACKを出さず、旧prefix/pairを置き換えずに別のisolated buildとVM受入で統合を確認する予定である。

Guest側は `python3 plan/ws014/phase009/renderer-quiesce/tests/run-guest.py --proposal /tmp/q312-driver-proposal --output /tmp/q312-quiesce-guest-evidence` でtemporary overlayを作り、同じ実transport/venus/share/displayと既存PCI/console collaboratorをlinkする。既存回帰を維持し、profile3/7、exact48byte private request、exact24byte NODATA ACK、他の成功response/長さの拒否、ACK後もposted descriptorを保持、32slot全使用時EAGAINを追加した。metadata-only旧hostclose、未投稿予約のhostACK要求、native送出前dirty公開、未知送出失敗のdirty保持、native proofとdescriptor proofの分離もPASS。`evidence/guest-results.json`参照。最終comment/空白整理はC token列不変を `evidence/comment-only-finalization.json` で確認し、意味fixtureの再実行は不要とした。

現在のguest検証は `python3 plan/ws014/phase009/renderer-quiesce/tests/run-guest.py --output /tmp/q312-quiesce-current-evidence` が作業checkoutの実sourceを既定として使う。`--proposal`は適用前の同一copy/hashを再現するときだけ指定する。sourceの一時overlayはfixtureの追加caseと既存collaboratorを結合するためのもので、production生成物や公開API generatorではない。

## 新isolated host pair

`build-isolated.py`は既存remote q311-strict/sourceのbase hashとMIT licenseを確認し、新しいq312-quiesceおよびq312-quiesce-delayの未存在を要求してcopyを作る。転送したのはレビュー済み8.8KB host patch、2.7KB delay patch、manifest、builderのみ。source treeの一括送出はしていない。patch後hashを確認し、release/Venus/EGL/process workerで各91build stepとinstallがPASSした。既存prefixとhost system package/display/GDM/VFIO/物理GPU resetは変更していない。

| Pair | private install prefix | libvirglrenderer SHA256 | server SHA256 |
| --- | --- | --- | --- |
| Production | `/home/awe/zedbsd-q306-venus/dependencies/q312-quiesce/install` | `38447deeaac67a7c9ff4613115a49b2d60a10acda6c073f2d4c73d0af718fbd4` | `745bef59c17c88975ee567c0ae8c2c689b5b06fca79171d121227d0f5f5f84c2` |
| Delay fixture | `/home/awe/zedbsd-q306-venus/dependencies/q312-quiesce-delay/install` | `32d152dd251711b6f7a956abc199c81fcb59fa9ece263af7b773d78f36c7340f` | `e250106ba75f5592799697616012375af3946440d96cf9b719720eee0ba88444` |

libdirは各prefixの`lib/x86_64-linux-gnu`、serverは`libexec/virgl_render_server`。`evidence/build-{production,delay}.json`にsource/license/artifact hashとprocess構成を保存した。双方の実serverへprivate socketpairでINITを送り、exact7だけが期待した返信を返し、旧3/1、未知15、wrong magic、stock8Bを拒否する6caseがPASS。native GPU contextは0個であり、実GPU停止をこのINIT試験で証明したとは扱わない。

既存の `run-venus-transport-test.sh` / `run-venus-backend-test.sh` / `run-venus-console-test.sh` にも同じ新case/peerを接続し、適用済みproduction sourceを直接linkしたordinary＋ASan/UBSanの6profileがPASS。結果と現source hashは `evidence/current-source-results.json` に保存した。最後のfixture整理はpublic helper/前方宣言の位置と重複licenseコメントだけで、warning-as-error構文確認を追加した。
