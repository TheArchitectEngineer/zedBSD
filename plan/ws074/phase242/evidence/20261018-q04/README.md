# q04 ParentNode.replaceChildren 基本交換

2026-10-18。ユーザーの自走指示を最大90分のbasic partialに適用。Element/DocumentFragment/Documentへmethodを登録し、全string変換後のfragment assembly、重複移動、交換前validation、native削除/挿入を実装。rootは各変換済みnode/親/fragment/削除nodeを所有し、例外cleanupで解除。Document replacementのvalidationは既存子を除外し、通常insertは従前のexclusionなしを維持。

独立39項目（GC中の先行Text保持、例外、順序、Document/root/doctype、同一heapの別Documentからadopt、receiver種別）とDOM全24/24通過。Chromium153は共通24項目通過、Document既存root置換をHierarchyRequestErrorにする。固定WPT/DOM仕様の更新されたexclusion-list条件を採用し、この差を保存。Chromium全39一致とはしない。

公式cleanup13 IDは以前全harness_error、今回12 FAILと1 no_completion。whole PASS増分0。anchor-invalid-fallbackは1 FAIL callbackのみ→33 PASS/6 FAIL、scope-evaluationは9 PASS/17 FAILなど、後続検査を実行できた。CSS本体は未実装のためwhole失敗継続。補助ParentNode WPTは0→16 PASS callback、29 callback受信、no_completion。createHTMLDocument不足、HTMLCollection spread、observerのbatch/source removalが別の残りであり、未実行をPASSにしない。

host build warning0、amd64の実際のproduction compile commandを5 sourceへ実行しwarning0。production linkは共有sysroot更新が必要なためpending（q02と同じ制約）。clang-format19.1.7変更範囲、C全文manual review、diff check。Acid3 score100/差0、Acid2差873で非増加。QEMU/実機は未実施。

q04基本部分cleared、p242 whole uncleared。次はobserver batch/source removalを独立の有限Queueで処理する。Master/共有Queue/GitHub projectionはpending。raw callbackは各directory、詳細はsummary.json。
