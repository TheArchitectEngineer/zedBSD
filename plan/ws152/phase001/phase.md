<!-- awesome-plan project=zedbsd record=ws152-p001 -->

# ws152-p001: system の更新の方式の検討

Status: planning
Disposition: normal
Parent: [WS152](../ws.md)
Queue: なし（ベータ3 の計画の時に入れる）

## 目的

ベータ3 で実装する system の更新の方式を決める。code は書かない。結論と判断の項目をユーザーに出す。

## 検討の観点

1. **更新の単位**: image の全体（A/B の 2 つの partition か、root の file system の差し替え）か、package（base・Keiland・外部 package ごと）か、その組み合わせか。kernel・boot loader・firmware の更新の扱い。
2. **配布**: 更新の置き場所（GitHub の release（WS129 の CI の Prerelease・Latest）か専用の server か）、版の付け方（`1.0.0-betaN`）、差分の配布の要否、帯域。
3. **検証**: 署名（鍵の管理・失効）、hash、改ざんの検出。HTTPS の上でも署名を確かめる。
4. **適用**: 動いている system への適用の手順、再起動の要否、利用者の data（home・設定）を保つこと、設定の file の移行（desktop.conf などの版の差）。
5. **失敗の回復**: 適用の途中の電源断、起動しない新しい版からの自動の戻り（A/B と boot の成功の印）、手動の戻し。
6. **UI**: Settings の Updates の頁（更新の確認・download の進み・適用・再起動の予約・自動の確認の on・off）、通知、libkeiland・compositor の拡張・libkeiland-backend の口（他の設定と同じ経路）。権限（管理者だけ）。
7. **Linux・FreeBSD の Keiland**: OS の package manager（apt・pkg）に任せ、Updates の頁は案内だけにするか、を判定する。
8. 既存の仕組みとの関係: インストーラ（WS019・WS119）、release（WS129）、外部 package（WS032）。

## 成果物

- 方式の案（2〜3 の案と利点・欠点、推奨）、判断の項目、実装の Phase の分け方の案。design-reviewer の review。

## 受け入れ

- 上の観点の全部に案と根拠があり、ユーザーが方式を選べる形になっている。
