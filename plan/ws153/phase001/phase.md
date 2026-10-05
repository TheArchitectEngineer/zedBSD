<!-- awesome-plan project=zedbsd record=ws153-p001 -->

# ws153-p001: third-party の app の repository（package の仕組み）の検討

Status: in-progress（設計の第 1 版、[design.md](../design.md)。敵対的レビュー中）
Disposition: normal
Parent: [WS153](../ws.md)
Queue: q761（Q1、2026-10-05、P2 g15）

## 目的

third-party の app を配布・導入する package の仕組みの方式を決める（`userland/packages/` の build の仕組みとは別）。code は書かない。結論と判断の項目をユーザーに出す。

## 検討の観点

1. **package の形式**: archive の形式・metadata（名前・版・依存・権限・icon・説明・license）・app を置く場所（system の外の領域、例えば `/opt` か利用者ごとの領域）・既存の `.nap`（remacs の起動の仕組み、ws129-p012）の扱い。
2. **依存と ABI**: zedBSD の libc・libkeiland の版と ABI の互換、依存の解決（bundle にするか共有 library にするか）、Linux の Keiland の app との共通化の余地。
3. **repository**: index の形式・置き場所（静的な HTTPS の server で足りるか）・複数の repository・mirror。
4. **信頼と安全**: 署名（repository の鍵と package の鍵）、検証、第三者の app の権限（sandbox の要否、Privacy・Security の頁（WS148・WS149）との関係）、悪意の package への対策。
5. **導入・更新・削除**: 管理者だけか利用者ごとの導入か、更新の確認（Updates の頁（WS152）と分けるか共通にするか）、削除で利用者の data を残すか。
6. **app の登録**: 入れた app を App Home（apps.conf）・Files の関連付け・desktop の起動の一覧に載せる仕組み。
7. **Settings の Apps の頁**: 入っている app の一覧・探す・入れる・更新・削除・既定の app（関連付け）の UI。経路は他の設定と同じ（libkeiland・compositor の拡張・libkeiland-backend）。
8. **既存の仕組みの調べ**: Flatpak・Snap・AppImage・FreeBSD の pkg・Debian の apt・Haiku の hpkg などの方式の比較（code は写さない、license の境界）。

## 成果物

- 方式の案（2〜3 の案と利点・欠点、推奨）、判断の項目、p002 以降の Phase の分け方の案。design-reviewer の review。

## 受け入れ

- 上の観点の全部に案と根拠があり、ユーザーが方式を選べる形になっている。
