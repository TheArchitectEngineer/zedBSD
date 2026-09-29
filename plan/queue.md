<!-- awesome-plan project=zedbsd record=queue -->

# Queue q501: Amazonトップのpercentage height

<!-- awesome-plan-current:start -->
Status: active（2026-09-30）
Active Queue: q501-i01 / [ws074-p089](ws074/phase089/phase.md)
Last finished Queue: [q500](history/queue-q500.md)（ws074-p094 cleared。Chromiumとの固定比較を1 command化）
Executor: main
Approval: ユーザー「Amazon.co.jpのトップページがうまくレンダリングできるようになるまで、自走をお願いします。」
<!-- awesome-plan-current:end -->

## q501-i01

Amazonトップの2番目のcardがChromiumの約490 pxに対して約200 pxで終わる差を、percentage heightの確定性をlayoutへ渡して修正する。小さいfixture、ASan、p094の固定captureで確認し、topの画素・ink一致率を低下させない。

Upcoming Work Outlook: p089の後は固定比較で最大の残差を選び、既存のp090（合成太字）、p091（flex/CSSOM）、p088（動的script）、p092（DOM）から1 PhaseずつQueueへ入れる。
