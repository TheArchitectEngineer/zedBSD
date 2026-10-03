<!-- awesome-plan project=zedbsd record=queue-q499 -->

# Queue q499: Windows Venusの実画面

Status: finished（2026-09-30）  
期間: 2026-09-29〜2026-09-30

- q499-i01 / ws085-p001: cleared。Windows rendererのIMPORT_RESOURCEのpaddingとinline fd所有権を修正。DLL build、通常make、Files開閉・再起動、Terminal同時起動を確認。配布DLLへ反映し、vendorのcommit/pushはユーザーのreview待ち。
- q499-i02 / ws073-p028の後処理: cleared。旧`userland/noct`をpackage自動発見から除き、正規の`userland/base/noct`だけを使う。通常make PASS、warning 0。
- ユーザーの画面確認でVenus表示は成功。後続の速度・touch bridgeは各WSの計画へ残す。
- GitHubへは未公開。
