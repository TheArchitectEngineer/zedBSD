<!-- awesome-plan project=zedbsd record=ws153-design -->

# WS153 の設計: third-party の app の repository と Settings の Apps の頁（ws153-p001、第 2 版 2026-10-05 P2 g15、q761）

[WS153](ws.md) の単一目標「Settings に Apps の頁を足し、third-party の app の repository から app を探す・入れる・更新する・消すことができるようにする」の方式の検討。ユーザーの指示（2026-10-04 夜）: 「パッケージシステムは userland/packages ではなくて、サードパーティーアプリのリポジトリのことです。」。code は書かない。方式の案と判断の項目を出す。第 2 版は第 1 版への敵対的レビュー（重大 6・中 13・軽 8、[phase001](phase001/phase.md) に要旨）を反映した。

## 0. 結論（推奨）と判断の項目

推奨: **案 A（自己完結の app の bundle、利用者ごとの導入、repository の鍵で署名した静的な index）**。段 1 は root の口・setuid・sandbox を足さずに作れ、後の段で system 全体の導入（特権の helper）・HTTPS・sandbox を足せる。

| 番号 | 判断の項目 | 推奨 | 理由 |
| --- | --- | --- | --- |
| U1 | 導入の範囲: 利用者ごと（`$XDG_DATA_HOME/keiland/apps/`）か、system 全体（管理者） | 段 1 は利用者ごと。system 全体は後の段（account-admin と同じ形の小さな特権の helper、別に設計して承認） | 利用者ごとなら root の口が要らない（security.md「The desktop holds no privilege」） |
| U2 | package の形: 自己完結の bundle（案 A）か、共有の依存の package（案 B） | 案 A | 依存の解決・衝突が無く、消すのが dir を消すだけ |
| U3 | 転送: HTTP と署名（段 1）か、HTTPS（tree の OpenSSL の package を appd が使う） | 公式の repository を HTTP のまま出せる置き場所があれば段 1 は HTTP。無ければ（一般の静的な hosting の多くは HTTPS へ redirect する、推測）段 1 から OpenSSL で HTTPS | zedBSD の `fetch` は HTTPS を持たない。改竄は署名で防げるが盗み見は防げない。hosting と組で決める |
| U4 | 署名の方式と実装 | Ed25519（RFC 8032）の**検証**を appd に自前で書く（外部の code を写さない）。署名を作る側は host の既存の道具（OpenSSH の `ssh-keygen -Y sign`、または Python の `cryptography`）で、自前の署名の code は書かない | 検証だけなら小さい。署名の道具は実績のある物を使う |
| U5 | 公式の repository: 運営するか、誰が鍵を持つか、image に既定で入れるか、期限の長さと再署名の手順、hosting | 公式の repository を運営し、ユーザーが鍵を持ち、image に既定で入れる。index の期限は 30 日、ユーザーが毎月再署名（手順は p007 で作る）。鍵は 2 つまで並べられ（鍵の ID つき）、交換は新旧の鍵を並べた期間を置く | 信頼の起点と運用をユーザーが決める |
| U6 | sandbox が無いこと | 段 1 は無し（入れた app は利用者の全ての権限で動く）。Apps の頁と導入の確認でそう示す。sandbox は kernel の仕組みが要るので別の WS | zedBSD の kernel に sandbox の仕組みが無い |
| U7 | package の名前と拡張子 | `.kapp`、MIME `application/x-kei-app` | Keiland の名前を画面に出さない |
| U8 | 開発者向けの SDK | 別の WS。WS153 の試験の app は tree の中で build する | 範囲を保つ |
| U9 | **platform の ABI の安定の方針**: third-party の binary に、libc・libkeiland の ABI をどこまで約束するか | `/etc/keiland/abi` を ABI の名前の正本にし、libkeiland の symbol を消す・libc/rtld の私有 ABI の版を上げる時は ABI の名前の数を上げる（約束はそこまで、凍結はしない）。合わなくなった app は Installed に「この system では動かない（更新を待つ）」と出し、起動しない | 今の libkeiland は呼び出しを消したことがあり（KL_VERSION 16・22）、libc/rtld の私有 ABI は版が変わると再構築が要る（`docs/reference/tls.md`）。凍結はプロジェクト全体の判断 |
| U10 | 手元の `.kapp`（署名の無い file）の導入を許すか | 段 1 は許さない（repository からだけ）。Files で `.kapp` を開くと「repository から入れてください」と出す | 署名の無い app は検めようがない |
| U11 | 更新の確認の自動の問い合わせ（1 日 1 回）を既定で on にするか | 既定 on、Apps の頁で off にできる。問い合わせるのは index だけ（入っている app の一覧は送らない） | privacy |
| U12 | 展開の上限 | package 256 MiB、展開 1 GiB、file 1 万、path 1024 byte・深さ 32 | 利用者の disk を守る |
| U13 | 導入・削除・repository の追加の確認を誰が描くか | compositor が描く確認の dialog（Settings 以外の client が `kl_system_apps` で依頼しても、利用者が compositor の dialog で承認しない限り進まない） | 同じ uid の client なら誰でも拡張を使える（`wayland/settings.c` の peer の検め）ので、Settings の card だけでは確認にならない。後で sandbox を足す時も同じ口のまま使える |

判断の期限: U1・U2・U4・U5・U9 は p002 の前、U3・U13 は p003・p004 の前、U10〜U12 は p005 の前。

WS152（system の更新）とは、取得・検めの code と index の形を共通にできる見込みがあるが、WS152 はベータ4 以降で planning なので、ここでは決めず Q1 と調整する項目にする。

## 1. 案の比較

| 案 | 中身 | 利点 | 欠点 |
| --- | --- | --- | --- |
| **A. 自己完結の bundle**（AppImage・macOS の .app・Haiku の hpkg に近い） | 1 つの archive に app の program・私的な library・data・icon・manifest。platform の ABI の library だけを system から使う | 依存の解決が無い。入れる・消すが dir の単位。版の並存（更新の失敗で前の版に戻せる）。利用者ごとに入れられる | 同じ library が app ごとに重複する。私的な library の security の更新は app ごと。platform の ABI が変われば作り直し（U9） |
| B. 共有の依存の package（FreeBSD の pkg・Debian の apt） | package の間の依存を index で解き、共有の library を共有の場所に | 小さい。library の更新が一度で済む | 依存の解決・衝突・部分の失敗の回復が要る。system 全体への導入（root）が前提になりやすい |
| C. sandbox の runtime（Flatpak・Snap） | runtime と app と sandbox | 隔離・権限の管理 | kernel に namespace・mount の隔離などが要る。zedBSD では今は作れない |

推奨は A。

### 1.1 `.nap`（Noct application）の扱い

`.nap` は `noct --compile --app` の Noct の program で、先頭が `#!/usr/bin/noct`、内容の magic で判定され、CPU の ABI に依らない（`plan/ws129/phase012/phase.md`）。

- 案: bundle の `abi` に `noct-1` を許し、`exec` が `.nap` を指す。起動は `/bin/noct` が `.nap` を読む形（argv: `/bin/noct <path>.nap`、shebang に頼らない）。同じ `.kapp` が amd64・i386・arm64 の全てで動く。
- 段 1 に入れるか: **入れない**（noct の app の UI の口（Keiland の窓）がまだ無い）。形式は `abi=noct-1` の余地を残し、noct の app が窓を持てるようになったら足す。判断は不要（形式の予約だけ）。

## 2. package の形式（案 A）

- **archive**: POSIX の ustar を **zlib の wrapper の deflate** で圧縮した物（`libz-compat` は zlib の wrapper と raw deflate を読み、gzip の wrapper は読まない。`.tar.gz` の道具では作れないので p002 の道具が作る）。拡張子 `.kapp`。
- **中身**:
  ```
  manifest            （必須）
  bin/<program>       （manifest の exec が指す regular file）
  lib/                （私的な共有 library、任意）
  share/icon.png      （任意、256×256 まで）
  share/...           （data）
  licenses/...        （必須）
  ```
- **展開の検め**（展開器は appd に新しく書く。tree の `pax` は先頭の `/` を取り除く・hard link を作る・拡張 header で名前を上書きする・FIFO を作る方針なので使い回さない）: 一時の root の dirfd を基準に `openat`・`mkdirat`、`O_NOFOLLOW|O_EXCL` で作る。断る物: 絶対 path・`..`・空の要素、hard link（LNKTYPE）、symlink（`lib/` の中の soname の link で、目標が同じ dir の file の名前（`/`・`..` を含まない）である物だけ許す）、device・FIFO・socket、setuid・setgid・sticky の bit、同じ path の重複の entry、dir と file の入れ替わり、4 KiB を超える pax の拡張 header、U12 の上限の超え。file の mode は 0644・0755 だけを残す。
- **manifest**（UTF-8、`key=value` の行、未知の key は無視、同じ key の重複は断る）:
  ```
  format=1
  id=org.example.Paint          （英数字で始まる英数字と '-' の label を '.' 1 つで区切った 2 つ以上、3〜100 byte）
  name=Paint                    （表示の名前、1〜64 byte）
  version=1.4.2                 （1〜4 個の 0〜65535 の 10 進を '.' で区切る、先頭の 0 と pre-release は断る）
  abi=zedbsd-amd64-1
  requires-keiland=32
  exec=bin/paint                （bin/ の後に [A-Za-z0-9._-] の 1〜64 byte、regular file で mode 0755）
  icon=share/icon.png
  summary=A simple paint program.
  license=MIT
  keywords=paint draw image
  app-id=org.example.Paint      （窓の xdg の app_id、bar がまとめる名前。省略なら id）
  open-with=image/png image/jpeg     （type/subtype の literal だけ、glob・TAB・',' は断る）
  data=~/.config/paint               （利用者の data の場所、削除の時の「data も消す」に使う。任意、$HOME の下だけ）
  ```
  表示の文字列（name・summary・keywords）は UTF-8 として正しく、C0・C1・DEL・bidi の制御（U+202A〜U+202E・U+2066〜U+2069）を含まない。
- **導入の場所**（U1）: `$XDG_DATA_HOME/keiland/apps/<abi>/<id>/<version>/`（既定 `~/.local/share`。zedBSD と Linux の Keiland で home を共有しても ABI ごとに分かれる）。`<id>/current` の symlink が今の版の dir 名を指す。
- **私的な library**: zedBSD の rtld は `DT_RUNPATH` の `$ORIGIN`・`$ORIGIN/...` を展開する（`src/rtld/rtld.c` の `open_search_list`）。形式の約束: `bin/` の program は `DT_RUNPATH=$ORIGIN/../lib`、`lib/` の library どうしの依存は各 library に `DT_RUNPATH=$ORIGIN`（RUNPATH は推移しない）。`LD_LIBRARY_PATH` は使わない（app が起動する子の process に引き継がれ、system の program に私的な library を読ませるため）。導入の時、`lib/` の file 名が platform の library の soname（`/etc/keiland/abi` に一覧）と重なれば断る（platform の library を覆わせない）。

## 3. 依存と ABI（U9）

- app は platform の library（`/etc/keiland/abi` の一覧: `libc.so`・`libkeiland.so`・`libvulkan.so`・`libwayland-client.so`・`libtruetype.so`・`libz-compat.so` など）だけを system から使う。それ以外は bundle の `lib/` に入れる。
- `/etc/keiland/abi`（image が持つ、正本）: `abi=zedbsd-amd64-1`、`keiland=<KL_VERSION>`、platform の soname の一覧。appd はこれを読んで manifest の `abi` と `requires-keiland` を比べる（appd は libkeiland を link しないので、libkeiland の版もこの file から）。
- ABI の名前の数を上げる時: libkeiland の symbol を消す時、libc/rtld の私有 ABI の版を上げる時、platform の library の soname を変える時。上げるのはその変更を入れる WS（Guardrail に手順を足す依頼を Q1 へ）。
- 合わない app: Installed に「この system では動かない」と出し、App Home に出さず起動しない。同じ id の今の ABI の build が repository にあれば更新として出す。
- Linux・FreeBSD の Keiland: 同じ形式で ABI の名前が違う（`linux-x86_64-glibc-1`、`freebsd-amd64-1`）。

## 4. repository

- **静的な file の server**（U3）:
  ```
  <base>/index、<base>/index.sig、<base>/packages/<id>-<version>-<abi>.kapp、<base>/icons/<id>.png
  ```
- **index**（UTF-8、1 行 1 項目、TAB 区切り、4 MiB まで）: 先頭に `format 1`・`repository <name>`・`generated <UTC>`・`expires <UTC>`、続いて
  ```
  package	<id>	<version>	<abi>	<size>	<sha256>	<name>	<summary>	<license>	<requires-keiland>	<path>	<icon-path>	<icon-size>	<icon-sha256>
  ```
  各値は §2 の規則で検める（id・version・表示の文字列）。path は `packages/`・`icons/` の下の `[A-Za-z0-9._-]` の file 名だけ。
- **署名**（U4・U5）: `index.sig` は `<鍵の ID> <Ed25519 の署名の base64>` の行を 1〜2 行。repository の conf の鍵（2 つまで、鍵の ID つき）のどれかで検めが通れば index を使う。
- **期限と巻き戻し**: `expires` を過ぎた index は使わない。前に受けた `generated` より古い index も使わない（記録は `$XDG_STATE_HOME/keiland/apps/<repository>.state`。消えたら最初の取得として扱う）。古い mirror に当たったら次の mirror を試す。時計が狂っていると期限で断るので、その時は「時計を確かめてください」と出す（zedBSD の ntpdate は認証が無いので、時刻を信じ切らず `generated` の単調性を主な防御にする。TUF の freeze 攻撃の考え）。
- **package の検め**: index の size を 1 byte でも超えたら転送を止め、SHA-256 と size を検める。展開した manifest の id・version・abi が index の行と一致しなければ断る。
- **package の鍵（開発者の署名）との比較**:

  | 案 | 利点 | 欠点 |
  | --- | --- | --- |
  | **repository の鍵だけ**（推奨） | 単純。mirror は署名した index をそのまま置ける。開発者の鍵の配布・失効が要らない | repository の鍵が漏れると、その repository の全ての app が偽れる。手元の `.kapp` は検められない（U10 で許さない） |
  | 開発者の鍵を足す（package を開発者が署名、index に開発者の鍵の指紋） | repository の server が乗っ取られても、開発者の鍵が無ければ package を偽れない。手元の `.kapp` も検められる | 開発者の鍵の登録・失効・交換の仕組みが要る。段 1 には重い |

- **repository の一覧**: system の `/etc/keiland/repositories.d/<name>.conf`（公式、image に入る。`url=` を複数（mirror）、`key=<ID> <base64>` を 2 つまで）と、利用者の `$XDG_CONFIG_HOME/keiland/repositories.d/`（Settings の依頼で backend の writer の thread が書く。足す時は compositor の確認の dialog（U13）で「この repository の app は利用者の全ての権限で動く」と示す）。公式は消せないが無効にできる。公開鍵の手入力を避けるため、repository の記述の file（`.krepo`: name・url・key）を開いて足す形にする。
- **出所の固定**（乗っ取りの防止）: 導入の記録 `<id>/origin` に repository の名前と鍵の ID を持ち、更新はその repository からだけ探す。別の repository の同じ id は「別の app」として出し、置き換えるには明示の確認（compositor の dialog）を求める。system の app（`/etc/keiland/apps.conf` の名前、`/bin` の program）と同じ name・app-id の app は断る。

## 5. 導入・更新・削除と登録

- **流れ**（利用者ごと、特権なし）: index を取る → 署名・期限・単調性 → package を取る → size・SHA-256 → 一時の dir（`apps/<abi>/.tmp-<pid>-<rand>/`、同じ file system）に展開しながら検める（§2）→ manifest を検める（index と一致、abi・requires-keiland・exec が在る、soname の重なりが無い）→ `<id>/<version>/` に rename（同じ版が在れば先に `.trash-…` に rename して退ける）→ `current` は `symlink(tmp)` の後 `rename(tmp, current)`（原子的）→ origin を書く → 登録 → 古い版を消す（§5 の起動の規則で動いている版は消さない）。
- **競合と回復**: apps の dir の lock file（`apps/<abi>/.lock`、flock）を持つ間だけ書く（同じ利用者の 2 つの session）。起動の時、持ち主のいない `.tmp-*`・`.trash-*` を消す。削除は `<id>/` を `.trash-…` に rename してから消す（途中で crash しても半端な app が残らない）。ENOSPC は失敗として前の状態のまま。
- **取り消し**: 取得・展開の間は取り消せ（一時の dir を消す）、`<id>/<version>/` への rename の後は取り消さない（完了まで進めて結果を返す）。
- **起動**: compositor が `current` を解いた実の path（`<id>/<version>/bin/<program>`）を argv の形（shell を通らない `posix_spawn`）で起動する（`zwl_spawn` は `/bin/sh -c` で 160 byte の command なので使わない。p004 で compositor に argv の spawn を足す）。動いている process は自分の版の dir を使い続ける。古い版は、その版から起動した process が無い（compositor が起動した子の pid を版ごとに数える）時に消し、そうでなければ次の機会まで残す（最大 2 版）。
- **登録**: 一覧と icon は appd が検めて（icon は decode して 256×256 の RGBA に切って）backend に渡す。compositor は disk も PNG も読まない（WS135 の「session の間 disk を待たない」）。App Home の一覧は今「最初に開いた時に一度読む」・48 個まで・絵は `icons.c` の名前だけなので、p004 で実行中の追加と削除、上限、RGBA の icon の描画を足す。bar（`apps-bar.c`）は manifest の `app-id` で窓をまとめ、その icon を使う。
- **Files の Open With**: 利用者の `open-with` は利用者が書く file なので触らない。appd が導入した app の関連付けの list（`$XDG_DATA_HOME/keiland/apps/open-with`）を作り、Files はそれを system の list の後に読む（候補に出るだけで、既定は変わらない）。既定にするのは利用者の明示の選択（Default apps、段 2）だけ。Files の起動の経路（`sh -c`）にはこの list の command を通さず、appd の list は id だけを持ち、起動は compositor の argv の経路に頼む（p004）。
- **更新**: Apps の頁を開いた時と 1 日 1 回（U11）、index を取り、入っている app の origin の repository の新しい版を探して Apps の頁に「更新」を出す（自動で入れない）。
- **削除**: `<id>/` を消し、登録を除く。利用者の data（manifest の `data=`）は残し、Apps の頁の「data も消す」を選んだ時だけ消す（`$HOME` の下だけ、symlink を辿らない）。

## 6. 層の配置（Guardrail「配置」「app と設定」）

- Settings は libkeiland の `kl_system_apps_*`（拡張 `kl_system_manager_v1` の新しい object `kl_system_apps_v1`）だけを使う。
- **compositor**: 入っている app の一覧（App Home・bar に要る）、catalog の検索と頁送り（compositor の側で検索して頁ごとに送る。Wayland の message の大きさの上限のため catalog を丸ごと送らない）、icon は shm の fd で渡す、導入・更新・削除・repository の追加の確認の dialog（U13）、進みと結果。
- **libkeiland-backend の共通の code**（`libkeiland-backend/apps/`、OS に依らない）: appd の起動（WS145 の printd と同じ `posix_spawn`・socketpair・行の約束・寿命の形）と、repository の conf・導入の記録の読み書き（writer の thread、flock）。
- **keiland-appd**（`userland/desktop/appd/`、`/usr/libexec/keiland-appd`、Makefile・Makefile.linux・Makefile.freebsd を持つ。WS145 の printd と同じ置き方）: HTTP の client（自前、接続と読みの timeout・取り消し・header と本体の上限・redirect は http だけで 3 回まで、WS145 の printd の HTTP の上限と揃える。U3 で HTTPS なら OpenSSL）、Ed25519 の検証（RFC 8032、appd の私的な file、SHA-512 も自前（zedBSD の libc の sha2 は Linux・FreeBSD の build に無い））、SHA-256（同じく appd の私的な file。base との共有は後で判断）、inflate（libz-compat、3 つの OS の build に在る）、ustar の展開器（新しく書く）、icon の PNG の decode（libpng-compat）。
- **backend と appd の約束**（p003 で WS145 §5.1 の形の表にする）: `CATALOG`・`INSTALL <request> <repo> <id> <version>`・`REMOVE`・`CANCEL`・`PROGRESS`・`RESULT`・`IDLE n`/`BYE n`・`SPOOL`/`FATAL`。appd は待ち受けの socket を持たず setuid でない。log は syslog（URL・id・結果、利用者の file の名前は残さない）。
- KL_VERSION と protocol version は、WS145 の予約（version 10・KL 34）の後になる（順は Q1 と調整）。

## 7. Settings の Apps の頁

- **Installed**: system の app（「built in」、消せない）と利用者が入れた app（版・大きさ・repository、ABI が合わない物はその旨）。Update・Remove（「data も消す」の選択）。
- **Browse**: catalog（icon・名前・要約・license・大きさ・repository）、検索。Install（compositor の確認の dialog: 「この app は利用者の全ての権限で動きます」と repository の名前）。進みの bar と取り消し。
- **Repositories**: 一覧（公式は無効にできる）と、`.krepo` の file から Add・Remove。
- **更新の確認**: 自動の問い合わせの on/off（U11）。
- **Default apps**（段 2）。
- WS148（Privacy）・WS149（Security）との関係: Security の頁に「入れた app の出所（repository の一覧）」と「app は sandbox されない」の表示、Privacy の頁に「更新の自動の問い合わせ」の on/off を出す（その WS の頁の設計に依頼）。

## 8. 安全（まとめ）

- 署名・期限・単調性で index の改竄・巻き戻しを防ぐ。package は size・SHA-256、manifest と index の一致で検める。
- 出所の固定で別の repository の同じ id の乗っ取りを防ぎ、system の app の名前を名乗る app を断る。
- 展開の検め（§2）で path の外への書き込み・link・特殊な file・setuid を防ぐ。
- 起動は argv（shell を通らない）、exec と id の文字集合の制限で注入を防ぐ。私的な library は `$ORIGIN` の RUNPATH で、platform の soname と重なる名前を断る。
- 確認は compositor の dialog（U13）。
- 段 1 は sandbox が無い（U6）。repository の信頼がそのまま app の信頼。

## 9. 試験

| 層 | 試験 |
| --- | --- |
| 暗号 | host: Ed25519 の RFC 8032 の vector と拒否の vector（S ≥ L、非正規の点、小さい位数の点、長さの違う署名、鍵の ID の違い）、SHA-512・SHA-256 の既知の値 |
| 形式 | host: manifest・index の解析（id・version・exec・MIME・表示の文字列の境界）、展開の検めの fuzz の archive（`..`・絶対 path・hard link・symlink の外向き・device・FIFO・setuid・重複の entry・dir と file の入れ替わり・pax の拡張 header・path の長さと深さ・上限の超え）、soname の重なり |
| keiland-appd | host: Python の静的な HTTP の server の repository（署名の正しい・誤った・期限切れ・巻き戻し・鍵の交換の index、SHA-256 の合わない・size を超える package、https への redirect、止まる server（timeout）、途中で切れる転送、別の repository の同じ id）、導入・更新（失敗で前の版のまま）・削除、2 つの session の同時の導入、各段（rename と symlink の間、削除の途中）での kill の後の回復、時計の狂い、ABI の合わない app |
| compositor・libkeiland | host: `plan/ws131/tests/host-system.sh` の形で kl_system_apps_*、catalog の頁送り、確認の dialog を経ない依頼が進まない、command の長さと shell の特殊文字が起動に影響しない |
| QEMU | T1: 試験の repository（host の HTTP、user-net の 10.0.2.2）から試験の app を Settings で入れ、App Home に出て起動（実行中の追加）、更新、削除（実行中の削除）、Files の既定が変わらないこと。PNG |
| Linux・FreeBSD | Debian の QEMU+KVM と FreeBSD 15 の guest で同じ |

## 10. Phase の分け方（案）

| Phase | 内容 | 依存 |
| --- | --- | --- |
| p002 | docs: 形式の仕様（`docs/reference/` に manifest・index・`.kapp`・`/etc/keiland/abi`）と `docs/architecture/security.md` の app の導入の節（目標の設計を先に書く） | p001（U1・U2・U5・U9） |
| p003 | 暗号: Ed25519 の検証・SHA-512・SHA-256（appd の私的な code）と host 試験、独立の review | p002（U4） |
| p004 | 道具と形式: host の道具 `tools/kapp/`（package を作る・index を作る・既存の道具で署名する）、manifest・index・展開器、host 試験 | p003 |
| p005 | keiland-appd: HTTP（U3）・検め・展開・導入・更新・削除・回復、backend との約束、host 試験 | p004 |
| p006 | backend の apps・compositor の拡張 `kl_system_apps_v1`・確認の dialog（U13）・argv の spawn・App Home と bar の動的な一覧と RGBA の icon・Files の関連付けの list・libkeiland の口 | p005 |
| p007 | Settings の Apps の頁（Installed・Browse・Repositories・更新の確認） | p006（U10〜U12） |
| p008 | 公式の repository の運用: 鍵の生成と保管、署名・再署名の手順、hosting（ユーザーの関与） | p004（U5） |
| p009 | 試験の repository と試験の app、QEMU（T1）と Linux・FreeBSD の確認 | p007 |
| p010 | 全文の規約の確認と回帰 | p002〜p009 |
| 後の段 | system 全体の導入（U1）、HTTPS（U3 で段 1 にしない時）、sandbox（U6）、SDK（U8）、開発者の鍵、Default apps、自動の更新、`abi=noct-1`、ABI を上げた時の移行と鍵の交換の経路（WS152 との接続） | — |

## 11. 既存の仕組みの調べ（方式だけ、code は写さない）

- AppImage: 1 file の自己完結（squashfs を FUSE で mount）。zedBSD に FUSE・squashfs は無いので展開して置く。
- macOS の .app・Haiku の hpkg: dir の bundle と manifest。Haiku は package を mount するが、zedBSD では展開する。
- Flatpak・Snap: runtime と sandbox（案 C）。
- FreeBSD の pkg・Debian の apt: 共有の依存と署名した index（案 B）。apt の `Valid-Until` の考えを期限に使う。
- TUF（The Update Framework）: 期限・鍵の交換・freeze と巻き戻しの攻撃の考え。段 1 は TUF の一部（期限・単調性・鍵 2 つ）だけを取り、役割ごとの鍵は後の段。
