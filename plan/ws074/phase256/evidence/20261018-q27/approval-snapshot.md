<!-- awesome-plan project=zedbsd record=queue -->

# ws074-q20261018-27 / genuine native ErrorEvent constructor・Window.onerror dispatch

Status: active（2026-10-18 17:55 JST）
Owner: このCodex chat単独。
Approval: [ユーザー自走指示](autonomy.md)をこのfinite partial scopeへ適用。q26 terminal09e900655/history SHA256aad72442738a30c7e9a2dee17f7246b7eec822eb73c1bdd2f933d26e34a21eddをread-back。
Timebox: 最大90分（2026-10-18 19:25 JSTまで）。

| Attempt | Phase | Exact scope / criteria | State | Dependencies |
| --- | --- | --- | --- | --- |
| ws074-q20261018-27-i01 | [p256](phase256/phase.md) native event prerequisite partial | genuine ErrorEvent private registry/constructor/Event inheritance/subclass/readonly enumerable message, USVString filename, uint32 lineno,colno, any error（default undefined）とnative brand/GCを実装。WebIDL辞書base→derived辞書順/primitive rejection/例外伝播、Event flags/composedを扱う。実Window dispatchでonerror handlerにmessage/source/line/column/errorの5引数、trueだけcancel、通常listenerはevent1引数・false非cancel、nonErrorEventや他EventTargetは通常handler semanticsを保つ。Chrome独立対照/native source-only GCとretired child lifetime、final pair全1508 harness/1561 static/413 scripted/48 strict、DOM/CSSOM/producer/observer/Acid非悪化、host/target exact event/window warning0・変更全文規約を検証しraw/hashを保存 | in-progress | q26実Event/Window/VM/GC出力・pinned HTML IDL・HTML error handler processing |

Dependency graph: verified Event/Window/VM → native ErrorEvent+legacy handler（本partial）→ 次Queueのproduction exception reporter/source muting → p255 official harness negative/held再評価。
Generation bounds: 既存bind/event.c/window.c/internal.h、new tests/dom/error-events.html/.expected、tests/host-error-events.cとWS074記録のみ。new module/publicABI2/Wayland/SDK/sysroot/toolchain/runner/WPT/manifest/期待値/共有Board/cacheは変更しない。C全文/Guardrail/browser component全文を直接適用、format19/full manual+checker。targetはq02 event/window exact commandで-oだけ変更。host build単独、unique frozen pair。
Bounds: 今回は通常reporterをまだ接続しない。callbackの発生元/muted-errors metadataがVMに無いことを確認しており、次finite scopeで独立に設計・実装する。same-origin情報を推測してcross-origin漏えいを導入しない。Promise rejection/syntaxerror/source metadata/reportError API/worker/full target/p100/whole p256・p255/held解除は本partialに含めない。未達/未解消後退/期限到達はunclearedとresumeを保存。consoleからrunner結果を書換えずheld1除外・確認済1481を先取り加算しない。push/削除/make check/QEMU無し。
Upcoming Work Outlook: native event prerequisite後production Window exception/source provenance、p255失敗対照/held再評価、VM yield*、未attempt275、layout/API/target/p100。

