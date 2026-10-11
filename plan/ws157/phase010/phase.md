# ws157-p010: Photosのメディア削除

Parent: [../ws.md](../ws.md)
Status: cleared
Disposition: normal
Queue: [photos-delete-20261010](../codex-queue.md)
Approval: 最新ユーザー「Photos appでメディアの削除」「libkeiland経由でコンポジタ、コンポジタがmediastorageを呼んで削除、mediastorageがコンポジタに通知、コンポジタがPhotosに通知」。以前のmain統合承認を保持。
Scope / Criteria: 選択mediaの削除操作、public API/Wayland extension/backend CLI spawn/pipe、stable ID listでmetadataとmanaged filesを削除、CLI mutation通知とPhotos更新、背景workerでUI非blocking。取り込み元は対象外。CLI保存失敗は削除前のfileを保持。近接host試験/named build warning0/全文規約/main統合。実データ削除試験は行わずprivate fixtureだけ。
Prerequisites: main 39ebb1b06、p006 API/JSON/notify、p008 worker。
Decision: 完全削除＋確認dialogとtrash方式を任意のasync質問にした。回答はまだ無く、待機後に「確認dialog付き完全削除、取り込み元は保持」の前提をユーザーへ明示して実装した。方式の明示承認とは主張しない。実データの削除はしていない。
Design: media interface v2（manager31）にremove request、older compositorにはENOTSUP。CLI remove-list stdinにIDs、本体はpath経由でCLIのみが扱う。mutex workerにremove job、UIはFD snapshotをinstallし通知をcoalesce。full standardはp011。
Standards: C全文/coding-style.md、Guardrail、standards/automation.md。Keiland apps/CLIのcompositor通信はlibkeiland Waylandのみ、database処理はmediastorage、static backendはposix_spawn/pipe。HAL/UAPI/toolchain/shared recordsは変更しない。
Verification bounds: finite private host fixture/wire checksとnamed mediastorage/libkeiland/wayland/photos build、共通Linux affected build。QEMU・aggregate make check・負荷/耐久・実機再起動無し。

## 2026-10-10 photos-delete-i01 outcome

cleared。公開API/remove request/CLI stable-ID削除/背景worker/PhotoメニューとDeleteキー/確認dialogを実装。mediastorageのcommit後通知を既存libkeiland→compositor watchへ接続し、PhotosはUI threadで最新snapshotをinstallする。private hostで画像・動画、元source保持、album参照削除、未知JSON field保持、malformed/duplicate ID、stale更新、空library、metadata commit失敗時のfile復元を確認。production client/server/backend/CLIのwireと旧manager29 ENOTSUPも確認。[証拠](../tests/photos-delete-verification-20261010.md)。最終全文規約/main read-backはp011。
