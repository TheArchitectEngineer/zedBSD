<!-- awesome-plan project=zedbsd record=ws136 -->

# WS136: 試験の image を「config.mk ＋個別の file の複写」に揃える

<!-- awesome-plan-current:start -->
Status: planning
Primary Milestone: MG007
Related Milestones: —
Objectives: O2
Parent: [Master](../master.md)
Queue: q655（P1）
Resume point: p001（棚卸しと移行）。
<!-- awesome-plan-current:end -->

## 目標（2026-10-04 ユーザー）

「試験のイメージが特殊なビルドになっているのがよくない気がします。試験ビルドの標準的な方法は、config.mkでのビルド＋個別ファイルコピー、程度にして、過去のbuild/を参照するのはやめましょう。config.mkと個別ファイルをwsのtests/に入れればいいだけです。」（規則は AGENTS.md「検証」）

## 完了の条件

1. 試験の image を作る script（`plan/*/tests/build-*image*.sh`・`plan/tools/*/build-*image*.sh` など、2026-10-04 に 24 本、`build/ws0xx-*` を読む script は 59 本）が、その WS の `tests/` の config.mk での build と、tree の中の file の複写だけで image を作る。
2. `build/ws035-fonts`・`build/ws035-wallpaper`・`build/ws071-fonts` などの過去の build の成果を、image の入力として読まない（font は `userland/desktop/fonts/`、壁紙は `userland/desktop/keiland/wallpapers/` など tree の物を使う）。
3. 共通の手順は 1 つの小さな helper（config.mk と `--file` の一覧を受けて `make disk-image` を呼ぶだけ）にまとめてよいが、試験ごとの特殊な build の段を増やさない。
4. 移した script のうち代表（login・files・ime・settings・criteria・demo）を build し、T1・T2 の boot-test で確かめる。

## Phase

| Phase | 内容 | 状態 | 依存 |
| --- | --- | --- | --- |
| [p001](phase001/phase.md) | 棚卸し（image を作る script と過去の build を読む所の一覧）と移行、代表の build と boot-test の依頼 | cleared（q655、T1-053・054） | — |
| p003 | p001 の残り: 既存の image を写して差し替える道具（hybrid-image.sh・ws073 kernel-image.sh ほか、既定が今は無い /home/awe/zedBSD-rpi4）、vkloop-hw.sh、ws101 の accel の noct（toolchain の許可が要る） | planned | p001 |
| [p002](phase002/phase.md) | login の試験 p095・p102・p104 を 2026-09-29 の既定の image（kei の自動 login、root/root・kei/kei）に合わせる（p103 は変更不要） | cleared（q655、T2-011・T2-012 PASS） | — |
