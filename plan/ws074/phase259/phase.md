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
