<!-- awesome-plan project=zedbsd record=ws153-p001 -->

# ws153-p001: third-party の app の repository（package の仕組み）の検討

Status: in-progress（設計の第 4.1 版、[design.md](../design.md)。4 回目のレビューで重大なし。ユーザーの判断 U1〜U14 を待つ。2026-10-05 夕、q765〜q768 のため区切った）
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

## 結果（2026-10-05 夜）

- [design.md](../design.md) の第 1 版（推奨: 自己完結の bundle `.kapp`、利用者ごとの導入、repository の鍵で署名した静的な index）。
- 敵対的レビュー（第 1 版）: 重大 6（rtld は `$ORIGIN` を展開する事実誤認と LD_LIBRARY_PATH の害、libkeiland は後ろ向きに互換でない（KL 16・22 で削除）と ABI の方針の欠落、別の repository の同じ id の乗っ取り、展開の hard link と pax の方針の違い、App Home の `sh -c` と 160 byte の command への注入、観点 1（`.nap`）と観点 4（package の鍵・WS148/149）の欠落）、中 13（鍵の交換と期限の運用、HTTP だけの hosting、HTTP client の頑健さ、appd の置き場所と OS の build、約束と競合と回復、版の混ざり、App Home の今の実装との差、open-with を壊す、確認を誰が描くか、catalog の運び方、判断の項目の不足、値の検め、試験の抜け）、軽 8。
- 第 2 版: 全てを反映（`$ORIGIN` の RUNPATH を約束にし soname の重なりを断る、U9 ABI の方針と `/etc/keiland/abi`、出所の固定、展開器を新しく書き hard link を断る、argv の spawn と exec・id の文字集合、`.nap` は `abi=noct-1` の予約、repository の鍵と開発者の鍵の比較、鍵 2 つと期限と単調性、U3 は hosting と組、appd は `userland/desktop/appd/`、確認は compositor の dialog（U13）、Files の関連付けは別の list、catalog は頁送りと shm の icon、docs と暗号と公式の repository の運用の Phase）。判断の項目 U1〜U13。
- 2 回目の敵対的レビュー（第 2 版）: 重大 2（U9 が構造体・listener の layout の変更を数えない、rtld の path の上限 256 byte と形式の上限・導入の場所が両立せず `$ORIGIN` が黙って効かない）、中 12（Phase の番号と判断の期限の食い違い、U4 の代わりの案（libcrypto）と U3 の arm64、出所の固定の穴（名前の衝突・ID・index の repository 行・書く順）、古い版を消す主体、書き手が 2 つと crash の窓、Files の Always Open With、鍵と期限の運用、platform の library の閉包と ELF の検め、Linux・FreeBSD の配置、試験、「data も消す」の範囲、SSHSIG）、軽 11。受け入れ条件は形の上では満たすが、R1・R2・M1 を直すまでは受け入れられないとの判定。
- 第 3 版: 全てを反映（U9 を案 a・b にして layout の変更・libc・静的な link を数える、id 48・exec 32・版は短い通し番号の dir・導入の時に実際の path の長さを検める、U4 を自前と libcrypto の案に、repository の ID と index の検め・`.origin` を版の dir に、古い版は次の login か更新まで、`apps/` は appd だけが書く、Files は kl_system_apps に問い合わせ `kl_system_apps_launch` で起動し段 1 は既定にできない（U14）、次の鍵の記録・再署名 1〜2 週・image の built、platform の閉包と ELF の検め、段 1 は zedBSD だけで Linux・FreeBSD の約束は別、`data=` は語だけ、pure Ed25519 と Python の `cryptography`、Phase の番号と期限）。判断の項目 U1〜U14。
- 3 回目の敵対的レビュー（第 3 版、未反映）: 重大 1（libz-compat の inflate は stream でなく出力の上限も無いので、展開の上限と「展開しながら検める」が成り立たない。icon の PNG も同じ。案: appd に上限つきの stream の inflate を自前で、libz-compat を直す依頼、形式を変える）。中 11（§3 の ABI の上げ方が U9 と不一致、私的な library の symbol の割り込み（export の重なりの検め）、bundle の library を dlopen できない、repository の ID は運営者が決めて `.krepo`・conf・index に、鍵の交換の記録を利用者の state に、`built=` の下限で新しい image が index を断る、判断の期限と Phase の依存の食い違い、`apps/` の書き手の本文の矛盾、「段 1 は zedBSD だけ」と本文の矛盾、U3 の前提（desktop は amd64 だけ・OpenSSL は既定 off・libbrowser の dlopen の方式）、ABI の名前の上げ忘れを止める checker）、軽 16。R2 は解消、R1 は本文に残り。
- 第 4 版: 3 回目の指摘を反映（U12 に inflate の上限の案（appd に上限つきの stream の inflate を自前で、推奨）と PNG の爆弾、§3 の ABI の上げ方を U9 と一致させ checker と `/etc/keiland/abi` の生成を p002 に、symbol の割り込みの検め、dlopen の制限と share/ の見つけ方、repository の ID は運営者が決め 3 か所に、`next-key` と利用者の state の鍵、`built=` から 30 日を引いた下限と image の前の再署名、判断の期限を p002 の前に揃える、`apps/` の書き手の本文、段 1 は zedBSD だけを本文に揃える、U3 を HTTP・libbrowser と同じ dlopen の OpenSSL・libcurl の 3 案に、ELF の検めの追加、id の規則、path の長さの `current`、古い版と `<n>` の通し番号、展開器の 1 段ずつの open、argv の spawn と dialog の作業を p006 に、WS152・WS145 との調整と session 間の制限）。
- 4 回目の敵対的レビュー（第 4 版）: **重大なし**。中 7（inflate の本文の残りと案 c・PNG の decoder・zlib の package の案、argv の spawn で compositor の fd が漏れる、next-key の鍵が system の更新で失効しない、repository の ID の使い回し、判断の期限と Phase の依存、U9 に関数の型と定数、symbol の割り込みの後からの検め）、軽 16。
- 第 4.1 版: 中 7 と主な軽を反映（U12 の案 c は a・b と組む時だけ・案 d、icon は自前の小さな decoder、spawn は fork・closefrom(3)・signal の既定、conf の epoch で state の鍵を捨てる、`.origin` に鍵の指紋、Phase の表の依存を判断の期限と揃え p002a（checker）を足す、U9 に関数の型と定数・enum、LIST での検め直しと私的な依存の静的な link、`expires − generated ≤ 30 日`、state の flock、非 blocking の lock と fsync、launch の絶対 path、WS152 はベータ2 の段）。残りの軽は p002 の docs で決める。
- 第 5 版（2026-10-05 夕）: U1 のユーザーの決定（「アプリはシステム全体で入れましょう。単独ユーザが使うタブレットを想定しているからです。また、ユーザ単位のアプリ管理は、ユーザがホームディレクトリで自由にやればいいと思います。」）を反映: 導入は `/apps/<abi>/`、書くのは特権の helper app-admin（setuid root、account-admin の形、管理者の password、検め直し）で、その口は新しい判断 U15（ユーザーの承認が要る、p005a）。利用者が足した repository は段 1 では system 全体の導入に使えない。U2〜U15 は未決。
