<!-- awesome-plan project=zedbsd record=ws074-p246 -->

# ws074-p246: Grid intrinsic track breadthの保持とcontribution

Status: uncleared（q11: whole維持・74 callback改善/24後退、native animation未実装）
Disposition: normal
Parent: [WS074](../ws.md)
Primary Milestone: MG006
Queue: ws074-q20261018-11-i01

## Objective・criteria

min-content/max-contentをautoへ潰す既存parse/cascadeでは、min-content columnの90px/710pxが期待40px/760pxにならず、computed trackもminmax/keywordを失う。CSS Grid 2 §7.2.1/12.5のintrinsic contributionとautoだけのstretchを根拠に実際のtrackサイズを修正し、宣言値をused値へ偽装しない。

## First finite scope / design

最大90分、private declared/computed trackへmaximum rule（default/auto、min-content、max-content）をscalarで保持。既存q09のminimum ruleと合わせ、computed fallbackはplain intrinsic keywordとminmaxのmin/max双方をserializeする。fr/length/autoの通常値と公開ABIは維持、fit-content/line namesは未達として保持。

horizontalのplain min-content columnsの非置換single-span in-flow itemについて、既存layout_min_contentとframe/margin/definite preferred/min/maxを用いたcontributionを集める。plain min/max-contentをauto stretchへ含めない。mixed spans/vertical/rows/minmaxの一般sizingは今回全面変更しない。未証明の経路を期待値で偽装せずwhole残りへ記録する。

## Verification / limits

独立min/max/auto、fr、frame、preferred/min/max、repeat/implicit、computed fallback/minmax、span対照をChromiumと比較。固定Grid502をq10 frozen before/afterで全callback比較し旧whole/callback PASS後退0。static Grid529はq09 verified227 PASS基準と比較（q10はscript CSSOMのみでstatic rendering source不変）。DOM/CSSOM host、host/target compile warning0、変更関数C全文/format、Acid3差0/Acid2非増加。全WS conformanceはp100、production link/runtimeは後段。

Guardrail・C全文・browser-component全文を適用。対象sourceの管理commentは技術説明を残して除去。code sourceは独自記述、仕様の事実だけ参照。Queue scopeを超えるminimum algorithm/repeat/line-name/template新grammar/公開ABIは扱わない。

Event ws074-p246-q11-selected-20261018: q10後にユーザー自走指示からfirst horizontal partialを選定。p221のtrack scalar interfaceとp243のcomputed fallbackが変わるため各Phaseへ変更理由/新検証を記録する。q09/q10 historyとexisting acceptanceは保持。共有投影pending。

Event ws074-p246-precode-eligibility-20261018: 既存layout_min_contentはreplaced幅の事前計測を仮定し、そのGrid intrinsic ratioは別の未達である。source生成前に自走契約でfirst sizing範囲を非置換single-spanへ確定し、replacedは既存contributionへ委譲する。metadata/computed fallbackは全trackで保持する。

Event ws074-p246-computed-floor-design-20261018: minmax(20px,10px)のcomputed upperを失わないよう、既存absolute floorはcascadeのstyle書換えからshared grid_fixed_track_lengthへ移す。column/rowのused結果は同じfloorを適用し、q09 fixed-span eligibilityは実際のrow capacityをそのまま読む。公開ABIは不変、q09独立7条件/全502/static529でこの移動も照合する。

Event ws074-p246-q11-scope-reconciled-20261018: 独立例でgrid-autoだけがintrinsic keyword/minmaxを共通parserの前に拒否していると確認。ユーザー自走契約の一般CSS修正権限でq11を、grid-autoのtrack-size grammarを既存values_one_trackへ統一する範囲に改訂した。repeat/line names/new template grammarは不可、fit-contentの既存自動sizingはwhole残り。初期scope snapshot SHA256 260da4c9f31a3d0f4efc7d3f8e6e055db767fe5d16bb58b487fe3c73ebd681eb をevidenceへ保存し、90分timebox/旧PASS非悪化等のcriteriaは維持。新scopeを保存した後にsourceを変更する。

Event ws074-p246-computed-floor-design-correction-20261018: repeat/verticalの既存consumerもclamped sizeを読むため、先のfloor移動案をsource生成中に見直した。既存cascade floorを維持し、private css_track.computed_maximumへunfloored upper lengthをcopyしてcomputed writerがexplicit length minimumのときだけ読む。pointer/公開ABIは不変、repeatやverticalの未選定算法は変更しない。先の移動設計はsuperseded、証拠/旧historyは残す。

Event ws074-p246-q11-uncleared-20261018: [証拠](evidence/20261018-q11/README.md)。独立22/22、Grid whole248維持、74 callback改善/24後退でattempt uncleared。static227→228/後退0、DOM29/29、host/target warning0、Acid非増加。24は以前のlossy serializationが隠したanimation欠落。p248にnative timing/track interpolation計画を追加し、その実装verified output後に新attemptで再開する。一般intrinsic/fit/name等whole残りとq11 exact criterion/historyは維持。共有投影pending。

Event ws074-p250-computed-fit-design-20261018: [p250](../phase250/phase.md)がmaximum_rule FIT_CONTENTと既存sizeにcomputed fit limitを保持する。calcを実際に解析し、computed fallbackで関数をserialize。native autoのfit capは未実装のまま維持するようstretch判定はknown min/maxのみへ明示化。新pointer/公開ABIなし。Grid502/静的529/独立computedとnative不変を確認し、known animation lossを全記録する。既存whole条件/historyは不変。

Event ws074-p251-linear-type-design-20261018: [p251](../phase251/phase.md)がdirect linear calcのpercentage存在を保持する。0係数でもCSS_UNIT_PERCENTとなるためdependent computed/used pathsを全1491/static1561と独立値で検証。nonlinear min/max/clampの既存近似は未変更。p250はこのoutput後にcomputed fitを検証し、p246/q11等history/whole残りは維持。

Event ws074-animation-phase-ownership-reconciled-20261018: q11から追加した[p248](../phase248/phase.md)をnative timing/DOM lifecycleへ限定し、以前からGrid animationを所有する[p224](../phase224/phase.md)へtrack補間・合成を集約。p248 verified primitives → p224 integrated Grid output → p246必要output再検証。q11 uncleared/24 callback lossとcomputed-only q14 scopeは不変。両foreign Phaseへ自身の変更と検証/resumeを記録、共有投影pending。
