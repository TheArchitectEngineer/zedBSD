<!-- awesome-plan project=zedbsd record=queue -->

# ws074-q20261018-38 / private original WebIDL2 dependency probe

Status: active
Owner: このCodex chat単独。
Approval: [自走指示](autonomy.md)を以下のfinite investigationへ適用。q37 finished/source0c8033f4b/terminalc2d099affとhistory hashを照合済み。
Timebox: 最大90分、2026-10-18 21:10→22:40 JST。

| Attempt | Phase | Exact scope / criteria | State | Dependencies |
| --- | --- | --- | --- | --- |
| ws074-q20261018-38-i01 | [p258](phase258/phase.md) private original WebIDL2 investigation partial | frozen q37 native helperへprivate JS constructor adapterを与え実VM Proxyを生成し、元pinned WebIDL2/testharness/q29 positive4条件・intentional negative1条件を無変更実行。adapter無しbaselineとgenuine Chromium対照、resource/source hash、原callbackとnegative到達位置を保存。最初の阻害要因を元source/実挙動に結び、次finite scopeを具体化。成功を要求する製品実装ではなく有界dependency調査の完了を判定する | in-progress | q37 private native Proxy Get/Has/GetOwn/Define/Set、q29 yield*と元resource |

Generation bounds: WS074 private probe scripts/fixtures/records only。engine/product source、既存shared controls/native fixture、global product Proxy、WPT/provider/manifest/expectations、SDK/toolchain/sysroot、共有Boards/cacheは変更しない。constructor adapterはmakeReadProxyを呼ぶだけでtrap実装/Reflect shimを代替しない。callable/nested target boundsは保持。元testharnessがhost fixture環境に依存して動かなければ、その差を記録し必要最小限のprivate host environmentだけを明示しChromiumにも同じ入力を使う。
Verification: source/hash固定、native callbackとprocess ownership gatesを区別。negativeは元assertionのmessageまで到達した時だけ認定。必要なら元bundleを改変しない診断入力で最初のblockを局所化。公式3805のPASSへ加算しない。host/target rebuild不要（engine無変更）。all1508はq37、static/scripted/crashはq28原証拠再利用。確認済1482/3805・held0・未attempt275を保持。
Standards: Guardrail/C全文/browser component全文直接適用。C生成無し。no agents/push/削除/make check/QEMU。原記録/生rawを保持しarchive全member read-back。
Dependency graph: q37 native private operations → unchanged original WebIDL2 positive/negative probe → evidence-based next finite product scope（未選定）。
Upcoming Work Outlook: actual parser blockerに基づくProxy IsArray/remaining operations/global API条件 → original WPT/未attempt275/API/layout/p100/full target。whole p258/WS acceptanceは保持。
