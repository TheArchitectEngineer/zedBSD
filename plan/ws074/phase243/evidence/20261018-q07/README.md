# q07 Grid used track CSSOM / uncleared

2026-10-18、ユーザーの自走指示を最大90分のprimary horizontal partialへ適用。実装WIP a46bfd5ac。existing solverのfinal columns/rowsをarena-owned double snapshotに保持し、private host queryがcaller-owned vectorへcopyしてresolved template軸へ渡す。public browser ABI、通常geometryのcopy契約、solver計算式は変更していない。child/vertical/named line/subgridはwholeの残り。

独立14条件はChromium153に4/14→14/14一致。fixed/%/fr/implicit/repeat/auto-fit/empty/border-box/inline/live/display変更を確認。DOM26/26、native CSSOM12/12、mutation12/12、host build warning0、amd64 compile4 source warning0。Acid3 score100/差0、Acid2差873。旧記録のhost-grid/host-cssというbinary名ではexit127だったため、それらの成功は主張しない。実在する上記CSSOM/DOMと公式Gridを検証した。production link/target runtimeは共有sysroot等のため未実施。

公式Grid502 IDを4 shardで同条件全測定。**245→247 whole PASS、257→255 FAIL**、旧whole PASS後退0。callback名/重複順/ID/checkpoint/rawを照合し、全callback名と順序が一致、欠損/追加0。**433 callback改善・1 callback後退**、PASS callback5,978→6,410（純増432）。summary.jsonに全遷移を保存。

このためq07-i01は**uncleared**。未達はgrid-intrinsic-track-sizes-001の、minmax(auto,10px)を2行またぐcase。旧computed-only serializerは宣言上の10px 10pxを返してcallback PASSだったが、実solverのrowsは10px 30pxであり、新しい正しいsnapshotでその既存計算不備を露出した。期待は10px 10px。grid_size_rowsはspanの不足分を固定trackを含む末尾行へ無条件加算している。出力を宣言値へ戻して問題を隠さない。

q07はsolverの別欠陥をscope外と明示していたため、ここで有限attemptを終了する。次のp244で固定行だけのspan growthを修正し、その出力で新しいp243 partial attemptが502全体を再照合する。q07のunclearedという歴史を後から書き換えない。wholeの残りとp100最終conformance/target関門も保持する。

検証runnerのTemporaryDirectoryを、Q1が後で整理できる保持directoryへ変更した。既知3 reftestは2 PASS/1 FAILを維持し、終了後scratch/PPMが残ることを確認。画素判定や入力は変更していない。Python compile/diff check。clang-format19.1.7、新関数/追加paragraphをC全文review。style候補170→175は新しい共有cleanup goto5件のみ（規約で許容する所有解除）。既存領域の候補はp100の全体conformanceに残る。

Master/共有Queue/GitHub projectionはpending。全3,805件100%未達。
