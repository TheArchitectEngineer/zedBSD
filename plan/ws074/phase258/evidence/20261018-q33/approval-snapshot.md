<!-- awesome-plan project=zedbsd record=queue -->

# ws074-q20261018-33 / realm-aware Proxy GetOwnProperty

Status: active（2026-10-18）
Owner: このCodex chat単独。
Approval: ユーザー自走指示[autonomy](autonomy.md)をexact finite partialへ適用。q32 terminal6be3eb480/history SHA256b58b81fc09d1bb06af7c72a3a4508e073fc77d996300bd79968bbe33985c07ebとarchive55 member read-back済み。standing Queueを再利用。
Timebox: 最大90分、20:10:21→21:40:21 JST。

| Attempt | Phase | Exact scope / criteria | State | Dependencies |
| --- | --- | --- | --- | --- |
| ws074-q20261018-33-i01 | [p258](phase258/phase.md) GetOwnProperty partial | realm-aware vm_get_own_property(status + found)を追加、通常objectは既存heap descriptor hookへ委譲しscript-free vm_get_own_descriptor/native_operationsは不変。Proxyはdynamic GetMethod/getOwnPropertyDescriptor、null fallback、handler this/actual target/string-or-symbol key2 args、object-or-undefined result、post-trap target descriptor→extensibility snapshot→To/Complete→互換性の正規順、欠落/fixed/nonextensible/nonconfigurable/nonwritable/SameValue/accessor invariantを実装。target descriptorとextensibilityはToのgetterに変異されてもsnapshotを保つ。元target/handler/revoke/realm/key/trap-result/両descriptor flagged payloadを実GCから保持、depth正常復帰/有界停止。公開Object.getOwnPropertyDescriptorだけを新VM operationへ接続しown helper forward宣言正規位置へ移す。old93 source prefix保全・genuine Chromeと同一追加条件一致・native post-trap descriptor GC/sole fresh payload/managed realm/実回収・host warning0/target exact access・builtin_object2 warning0・full changed C・focused DOM/CSSOM/VM/factory/producer/observer/error/Acidを検証 | in-progress | q32 shared VM To/From/Complete・q31 Has/Getとnative lifetime・TC39 GetOwnProperty |

Generation bounds: engine vm/access.c/vm.h、js/builtin_object.cのobject_get_own_property_descriptorとown forward declaration、既存access_descriptor_allowedの無script判定をsnapshot対応のpure compatibility helperへ共有する範囲だけ、tests/proxy-read.js/host-proxy-read.c、WS074記録。new module/ABI2/Wayland/runner/WPT/manifest/期待値/SDK/toolchain/sysroot/共有Board/cacheを変更しない。
Semantics bounds: private targetは非callable/非Proxyの既存制約を維持、global Proxy非公開。Define/Set/ownKeys/他Object helpersへの接続は未選定。whole p258はunclearedに保持。script-free heap hooksからJSを実行しない。pure compatibility共有の既存ordinary define動作は変えず、snapshotでTo getter中のtarget変異を正しく区別する。
Standards: Guardrail/C全文/browser component全文直接適用。new/changed Cをformat19/manual/checker、許可cleanup raw件数明記。full target link/runtime/p100無し。host build単独終了確認、final pair/control input固定。
Verification bounds: focused semantic/GC/gatesだけ。全1508 q31・static/scripted/strict q28証拠を明示再利用しq33全件同値と主張しない。fixed3805・held0・未attempt275・既知確認済1482を保持し増分を加算しない。追加gateは具体的後退が出た場合だけ。90分/未達ならunclearedとresume、push/削除/make check/QEMU無し。
Dependency graph: verified shared conversions → realm-aware GetOwn/snapshot-compatible rules → public single descriptor API + native/Chrome/GC/gates → 後続Define/Set（未選定）。
Upcoming Work Outlook: Define → Set → keys/prototype/extent/IsArray/call/construct/global constructor/revocable → actual original WebIDL2・WPT再評価、未attempt275/API/layout/p100/full target。

Primary: https://tc39.es/ecma262/multipage/ordinary-and-exotic-objects-behaviours.html#sec-proxy-object-internal-methods-and-internal-slots-getownproperty-p 。
History: [q32](history/ws074-q20261018-32.md)、[Past Log](history/index.md)。
