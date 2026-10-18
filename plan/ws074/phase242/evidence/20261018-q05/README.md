# q05 replaceChildren childList observer通知

2026-10-18、ユーザーの自走指示を最大90分のobserver partialへ適用。private sequence APIでadded/removed全nodeを1recordへ保持し、従来single-node APIはwrapperとして契約を維持した。replaceChildrenのtemporary fragmentへの移動と単一引数の挿入はsource parent削除を記録し、fragment emptyingも1record、target交換も1recordになる。source sibling boundaryは削除前にsnapshot。native remove/adopt/insertとhost挿入通知を通す。

native pointer配列と別のstable root配列で、削除後のnode全件をobserver record作成まで保持する。全変換のText/Node roots、parent/fragment、source parentとsiblingも保持。成功/失敗両方で登録済みprefixを解除し、部分初期化snapshotの二重releaseは安全。

独立observer24項目がChromium153と一致。source removalの順序/境界、単一既存子再利用（removed snapshotにも含まれる）、Fragmentの集約通知、empty-to-empty無通知、複数observer、文字列化例外、1,000 native nodeの削除後wrapper作成時GC保持を検証した。callbackはcreation index順で既存実装を維持。基本交換39項目を含むDOM全25/25通過。

補助ParentNode WPTはq04の16→18 PASS callback、31件全結果取得、13 FAIL。以前のno_completionからFAILまで進んだ。残りはcreateHTMLDocument未実装11件とHTMLCollection spread2件であり、このmethod自体の既知失敗とは区別する。公式cleanup13件は12 FAIL/1 no_completionを維持、whole PASS増分0。

host build warning0、production amd64 compile commandをmixin/environmentへ実行してwarning0。production linkは共有toolchain/sysroot更新待ち、target runtime未実施。clang-format19.1.7変更範囲、C全文manual review、diff check。style-checkの変更関数内候補は共有cleanupへのgotoのみ（規約で許容する所有解除）；その他の候補は既存領域。Acid3 score100/差0、Acid2差873。

q05 observer部分cleared、p242 wholeはprerequisite/target等が残りuncleared。DOM observer全仕様達成とはしない。次は公式reftest全静的censusへ。Master/共有Queue/GitHub projectionはpending。

仕様確認: [DOM replace-all](https://dom.spec.whatwg.org/#concept-node-replace-all)はoutgoing childrenを先にsnapshot/removeし、insert内でadoptする。[observer通知](https://dom.spec.whatwg.org/#mutation-observers)はcreation index順。途中の案をこの手順へ訂正し、既存子が1 target recordに含まれることを独立検証した。
