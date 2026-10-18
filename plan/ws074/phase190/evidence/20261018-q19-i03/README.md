# q19 scripted/variant reftest413 actual / terminal

[summary](summary.json)・[raw report](report.json.gz)。固定manifestから旧static1561のomitted413（script402/ref-script5/variant6）を全native attemptし、staticとdisjoint union1974を検証。82 PASS・73 pixel FAIL・242 unsupported_environment・16 readiness_missing、未attempt0。これはnative API全対応/全PASSを意味せず、非PASSを分母から除外しない。

元test/reference sourceはpinned git blob byte一致を確認、同origin resource/queryを保持した。nonce/URL/readyState・official fonts/2RAF/reftest-wait・probe_removed ack・native footer ACK1・viewport800x600を双方に要求し、first-ready frameだけを比較。probeを原script前に除去してDOMへ計測nodeを残さない。per-reference relation付きfuzzy上下限/zero例外・mixed match/mismatchの成功leaf・cycle/depth boundを使用。headers/生成server(.sub/.py/.asis)/HTTPS/testdriver remote actions等を検出した場合は未対応を保存して非PASSにする。原WPT/期待値やprod APIを変えず、全contextを通せたとは主張しない。

[全568 captures](all-native-captures.tar.gz)は元PPM/PNG・stdout/stderr textとbytes・JSON・instrumented bytes/nonce/source hashesを保存。[capture index](capture-index.json.gz)と[checkpoint](progress.jsonl.gz)を照合し、全tar memberを元bytesとread-back。PNG RGBは保存時のread-back一致・PPM/PNG/hashの全件一致を確認。runner source fdfaf3e0…は開始時copyとend hashで不変、program bcfcf31f…/libbrowser aa030e40…は[source proof](source-proof.json)に記録。

22 native/metadata/pixel/graph controlsを通過。さらにpinned WPT base.pyの**無改変check_pass/get_differences**をtest fixtureとしてruntime ASTロードし、実測155 edgeの全verdictが一致した。[結果](pinned-pixel-verification.json)・[verification tool](verify-pinned-pixels.py)。logger/infoとsolid-color診断だけをno-opにし、画素・判定本体を変えていない。upstream sourceは製品/checked-in toolへ転載せずruntimeの固定git blobを使用。Python専用full standardは無いため既存tool/Guardrail/権限・FS・テスト規則を直接適用。

全1491 harnessは491 PASS・callback11531 PASS/20226 FAIL/14 NOTRUNを維持、whole/callback後退・追加・欠損・順序変更0。全static1561は839 PASS/722 FAIL・全reference pixel metrics unchanged。strict48は39 PASS/9 readiness_missingを全ID対応で維持。scripted413は全native attempt/568 capturesで82 PASS・73 pixel FAIL・242 unsupported_environment・16 readiness_missing。確認済み1451/3805（38.134034%）、新規計測PASS82増。harness未attempt292、環境/API/worker等の未達は分母に保持。

q19-i03 measurement partial cleared、p190 whole uncleared。native APIや自動操作/headers/生成resource等は残す。old globalの証拠は[p196](../../../phase196/evidence/20261018-q19/README.md)。shared projection pending、push無し。
