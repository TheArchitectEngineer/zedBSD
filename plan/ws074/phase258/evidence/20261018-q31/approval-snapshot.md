<!-- awesome-plan project=zedbsd record=queue -->

# ws074-q20261018-31 / Proxy HasProperty・descriptor conversion ordering

Status: active（2026-10-18）
Owner: このCodex chat単独。
Approval: [ユーザー自走指示](autonomy.md)をこのfinite partial scopeへ適用。q30 terminal b2a2030e7/history SHA2566c78a350b5587d65b05b8c847243c22c3899f18d7317078c33288c018206bb29 read-back。precode independent native/Chromeでinvalid getter時のsetter読出し順の差を実再現済み。
Timebox: 最大90分、19:39:14 JST→21:09:14 JST。

| Attempt | Phase | Exact scope / criteria | State | Dependencies |
| --- | --- | --- | --- | --- |
| ws074-q20261018-31-i01 | [p258](phase258/phase.md) HasProperty/descriptor prereq partial | realm-aware vm_has_propertyと通常inを接続し、ordinary/prototype Proxyのactual HasProperty、has GetMethod/handler this/target・string/symbol key2引数・ToBoolean、null fallback、非configurable own/nonextensible ownをfalseにしないinvariant、trap/getter中revoke後の元target/handler・GC保持、bounded recursion/property depth正常回収を実装。Object.defineProperty既存ToPropertyDescriptorはenum→config→value→writable→get（直後にcallability検証）→set（直後検証）→mixed検証へ揃え、全fallible callを個別検査、元descriptor payload/input rootsを保持。shared native/Chrome Has・descriptor presence/get順/invalid getterがsetter Has/Getを観測しない・throw優先/GC/元read32 controlsを確認。final full changed C/host warning0・target exact access/builtin_object2source warning0、全1508 old whole/PASS callback保持とDOM/VM/factory/producer/observer/error/Acidを検証しraw/hash保存 | in-progress | q30 managed read/get receiver/revoke actual output、script-free hooks契約、TC39 HasProperty/ToPropertyDescriptor、precode Chrome差 |

Dependency graph: q30 read/GC → HasProperty → existing ToPropertyDescriptorのProxy descriptor入力と即時検証 → 後続realm-aware getOwn/define/Set（後続は未承認）→ global Proxy readiness。
Generation bounds: 既存engine vm/access.c/vm.h、js/builtin_object.cのobject_to_descriptorだけ、tests/proxy-read.js（元32を保持してHas/descriptor対照を追加）、tests/host-proxy-read.c（Has/descriptor GCとprivate bridge/lifetimeの必要追従）、WS074記録のみ。既存object_descriptor_fieldはvm_in経由のため新Hasをそのまま使う。FromPropertyDescriptorのpartial field出力やVM conversion APIへの移動、GetOwn/Define/Set/IsArray/callability/JS constructor/revocableの公開は今回は変更せずwhole p258へ残す。new module/ABI2/Wayland/native_operations契約/runner/WPT/manifest/既存期待値/SDK/sysroot/toolchain/共有Board/cacheを変更しない。
Standards: Guardrail/C全文/browser component全文を直接適用、簡約版無し。new helper/prototypeの通常file block、full manual/format19/checkerと許可cleanupのraw flags明示。targetはq02 exact既存2 single-source commandの-oだけ変更。host buildは完了確認して逐次、final sourceからunique pair固定後に全1508をsettle5000/process20で測定。画像/static/scripted/strictはq28証拠の再利用・限界を記録し、新pair全capture一致を主張しない。
Bounds: targetはq30のnoncallable/nonProxy制限を維持しglobal Proxyを未公開に保つ。new Hasをheap-only hooksへ混入せず、元target/handlerをget trap読出し前にsnapshot/root。誤ったfalse、getter順/例外/GC/旧PASS未解消または90分でunclearedとresume。固定3805/未attempt275/held0を変えず、原WebIDL2 parse完了を先取りしない。push/削除/make check/QEMU/full target/p100無し。
Upcoming Work Outlook: native realm-aware getOwn/Define、Set/descriptor/ownkeys/prototype/extent/IsArray/callability・global公開、original WebIDL2再評価、layout/API/未attempt275/target/p100。

