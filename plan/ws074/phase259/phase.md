<!-- awesome-plan project=zedbsd record=ws074-p259 -->

# ws074-p259: inline specified numeric state and flex-factor serialization

Status: in-progress
Disposition: normal
Parent: [WS074](../ws.md)
Primary Milestone: MG006
Queue: ws074-q20261019-55-i01
Origin: p189/q54 original fixed WPT CSS numeric serialization failures and native/genuine diagnostics.

Objective / criteria: native inline declaration source and CSSOM serialized text are distinct representations of the same specified state. CSSOM update must preserve exact underlying finite flex-factor float values, trace source lifetime, and reflect canonical flex-grow/flex-shrink Number output in getPropertyValue/member/cssText/style attr/removal. External style mutations reparse, unrelated mutations preserve state, committed notification errors/reentry preserve actual DOM outcomes. Both original valid factor WPTs and invalid/GC/typed native/genuine controls pass; old fixed3530 PASS preserved and full changed source standard compliant. Broader factor math/shorthand/alias/general numeric properties/layout accumulator remains explicit outside this goal.

Design/standards/verification/timebox: exact approved snapshot [q55 Queue](../queue.md)。Private DOM source/updating flag follows CSSOM declaration-block steps; CSS tokenizer owns numeric format; binding owns canonical views; cascade reads original source. No browser public ABI or new timing policy. Full C/browser standards, selected target compile, full3530 comparison and durable evidence required. Near-final WS full-standard p100 remains separate.

Event ws074-p259-created-selected-20261019: new concrete goal introduced from q54 actual residual and precision diagnostic, approved under autonomy.md. p091 legacy broad brief has no specific numeric owner, p110 imported record is selector combinator-only, p191 decoration behavior unchanged. No prior outcome invented or renumbered. WS/Queue projected locally, shared projections pending Q1/master write forbidden.

Checkpoint 2026-10-19 q55 source: inline specified-source/updating flag/native number serializer and getters connected; source full24 functions reviewed (product19/new test5), checker cleanup only. Host final warning0; native initial27/27, same161 JS native/Chromium allPASS, original q54 same161 has actual failures (not161 independent bugs). Target4 literal q02 commands -o only warning0. Initial native fixture18/21 used a stale direct css_engine_compute cache; corrected fixture to call existing css_engine_forget_styles before fresh query, no engine workaround, final27/27. Precommit allocator-failure branch manual review only. Full fixed3530 and final frozen pair remain pending; Phase still in-progress. Evidence pending archival under phase259/evidence/20261019-q55.

Event ws074-p259-q55-clone-design-revision-20261019: genuine Chromium1.234565 boundaryでcloneもused1.23457を保持（attribute1.23456）、initial canonical-attribute reparse仮定を否定。immutable DOM-owned source stringをclone_shallowのattribute copy後に共有、default updating flag0・各mutationはfield置換で独立。native clone期待とGC shared lifetimeを修正し、same-source genuine computed境界を追加。initial frozen final/source05ab82c7d/実行中fullcorpusを保持しfinal2 corrected pairで再測定。外部API/他Phasegoal/依存不変、既存p259内部の設計修正。Queue revision approval-clone-revision.mdとautonomy.mdへ記録、timebox不変、まだclearしない。
