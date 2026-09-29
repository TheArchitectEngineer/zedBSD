<!-- awesome-plan project=zedbsd record=queue -->

# Queue q502: 動的に挿入された外部script

<!-- awesome-plan-current:start -->
Status: active（2026-09-30）
Active Queue: q502-i01 / [ws074-p088](ws074/phase088/phase.md)
Last finished Queue: [q501](history/queue-q501.md)（ws074-p089 cleared。percentage heightとgrid `1fr`）
Executor: main
Approval: ユーザー「Amazon.co.jpのトップページがうまくレンダリングできるようになるまで、自走をお願いします。」
<!-- awesome-plan-current:end -->

## q502-i01

DOMへ挿入された外部`script src`を非同期に取得・一度だけ実行し、load/error eventを送る。小さいChromium fixture、host ASan、Amazonの動的比較で確認する。

Upcoming Work Outlook: p088の結果から不足するWeb APIを特定し、p092（fetch・IntersectionObserver・elementsFromPoint）またはp064（XHR）を1 PhaseずつQueueへ入れる。
