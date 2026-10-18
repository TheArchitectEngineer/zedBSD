# p241 / ws074-q20261018-02（2026-10-18）

[集計](results.json)。単一行のstretchでdefiniteになるcross sizeをratio base/minimumへ渡し、rowのcross relayout/配置でminmaxとauto marginを保つ。公開ABIとWayland境界は不変。

- 固定補助試験aspect-ratio257 ID: 149→152 PASS、改善009/045/046、後退0、error/timeout0。**この257件は固定Interop2025の3,805IDに含まれず、全体の達成数に加算しない。**
- 独立40例: 32→40 PASS、Chromium153と39一致、p240で記録した仕様差1例を維持。
- host build `BROWSER_HOST_BUILD=build/ws074-20261018 sh plan/ws074/tests/host-build.sh plain` exit0、warning0。GCC14.2.0。
- amd64のmake dry-runは他WSのtoolchain/sysroot更新を要求したため、共有を変更せず保留。同じtarget compiler/flagsで専用`target-flex.o`のcompileはexit0/warning0。production libraryの再buildはpending。
- `run-wpt-reftests.py --program build/ws074-20261018/q02/after/browser --root css/css-sizing/aspect-ratio --limit 0 --out build/ws074-20261018/q02/aspect-final`。revision/800×600/3fontを前回と揃え、全IDを照合。
- `run-acid-tests.py --program build/ws074-20261018/q02/after/browser --out build/ws074-20261018/q02/acid`: Acid3 score100/差0、Acid2差873。
- `run-flex-ratio-minimum-tests.py --program build/ws074-20261018/q02/after/browser --chromium`、negative-justify5/5、C全文manual review・clang-format19.1.7・diff check。style-check候補は50→50、新helperの候補0。

探索中の計測1回は実行中のlibbrowser.so再linkで120 load errorを生じたため、正式結果に使わない。実行fileとlibraryをafter/へ複写して固定し、上書きされない組で全257件を最初から再測定してerror0を確認した。以後の計測も固定binaryの組を使う。

QEMU/実機/GPUとproduction linkは未実施。p241 wholeはuncleared、host部分のみcleared。次は公式Interop2025の全体censusとrunner coverageを優先する。flex-basis:content/replaced/indefinite cross sizeは残る。共有Master/Queue/GitHubへのprojectionはpending。
