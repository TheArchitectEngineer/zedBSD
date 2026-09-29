<!-- awesome-plan project=zedbsd record=queue-q502 -->

# Queue q502: 動的に挿入された外部script

Status: finished（2026-09-30）
承認: ユーザー「Amazon.co.jpのトップページがうまくレンダリングできるようになるまで、自走をお願いします。」

- q502-i01 / [ws074-p088](../ws074/phase088/phase.md): cleared。DOM挿入の外部scriptを非同期取得して一度だけ実行し、load/error eventを送る。dynamic inline、parser/innerHTMLのalready-started条件と`HTMLScriptElement.src`も実装した。
- Chromium fixture、DOM 20/20、ASan、style-check 0。固定比較は不変。
- dynamic topはAUI後続scriptへ進み69.23%/ink 61.20%、Uncaught 9。次の不足はWeb API。searchはtimer settleが180秒を超えた。GitHubへは未公開。
