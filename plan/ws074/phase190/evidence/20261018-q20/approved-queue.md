<!-- awesome-plan project=zedbsd record=queue -->

# ws074-q20261018-20 / faithful static response headers・scripted413

Status: active（2026-10-18）
Owner: このCodex chat単独。
Approval: [自走指示](autonomy.md)を次の有限scopeへ適用。q19 history/hashとterminal WIPaa0be0367 read-back済み。
Timebox: 最大60分（16:32 JSTまで）。

| Attempt | Phase | Exact scope | State | Dependencies |
| --- | --- | --- | --- | --- |
| ws074-q20261018-20-i01 | [p190](phase190/phase.md) static response headers partial | scripted runnerのHTTP providerへpinned wptserve FileHandler/load_headersに従う普通のstatic .headersを追加。隣接__dir__.headers→file.headersの順、.sub.headers優先は未対応として保ちtemplateを無視しない。case-insensitive header名/重複順/Content-Type default抑制/値/本文bytesを忠実配信し、per-request適用ack/source hashを記録。正しく適用したheaderだけを未対応理由から外す。malformed/substitution/pipe/generated server・testdriver/HTTPS等の未対応は非PASSを維持。独立HTTP roundtrip/primary contract・native capture/旧22 controlsでbody/query/MIME/dup/CT/parent inheritance拒否・error fallback非PASSを確認。prod C/public ABI/既存static/harness/crash/WPT原本は変更しない | in-progress | q19 env242の実record・6 header filesのinventory、pinned handlers.py/response.py、既存strict/native controls |
| ws074-q20261018-20-i02 | [p190](phase190/phase.md) header-correct scripted413 revalidation partial | i01適用output/controls後、q19とbyte同一engine/client/fonts/viewport/budgetで固定413全native attempt。first-ready/footer/nonce/URL/probe_removed/dimensions/pinned wait/graph/fuzzyを保持、old whole82の維持と全edge/source/header/hash/PNG RGBを照合。testdriver/actions/生成templates等は非PASSとして残す。pinned無改変pixel comparatorで実測edgeを照合し、all raw/PPM/PNG/console/checkpointとID partition1974を保存。source不変の既存harness1491/static1561/crash48はq19証拠を再利用し不要な再測定をしない | pending | i01 verified header provider output、q19 source/pairとold82/全413 raw |

Dependency graph: q19 unsupported inventory → i01 header/control → i02 actual413/compare/archive。whole p190/WS100%・native API completionは先取りしない。
Upcoming Work Outlook: harness未attempt292（generated window39/worker16、残りorigin/sub/driver等の分類）、native p252 flex-basis、CSP/testdriver/HTTPS/templates、native module/animation/ViewTransition等。
Generation bounds: plan/ws074/tests/run-interop2025-scripted-reftests.py とtest-interop2025-scripted-reftests.py・WS記録/証拠だけ。native C/SDK/app/source/WPT/他runner/shared board/cache/他WSを変えない。Python専用full standard無しのため既存tool style/Guardrail/FS/検証規則を直接適用。全副生成物を保持し削除/push/make check/QEMUを行わない。
Bounds: 元60分、5000ms/5001 settle calls/process20s。header inputsを再現できない/controls unmet/old82後退ならaffected itemはunclearedとし原因・raw・resumeを記録。条件や期待値を緩めてPASSにせず、native driver actionsを偽装しない。
