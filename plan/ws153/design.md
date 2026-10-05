<!-- awesome-plan project=zedbsd record=ws153-design -->

# WS153 の設計: third-party の app の repository と Settings の Apps の頁（ws153-p001、第 1 版 2026-10-05 P2 g15、q761）

[WS153](ws.md) の単一目標「Settings に Apps の頁を足し、third-party の app の repository から app を探す・入れる・更新する・消すことができるようにする」の方式の検討。ユーザーの指示（2026-10-04 夜）: 「パッケージシステムは userland/packages ではなくて、サードパーティーアプリのリポジトリのことです。」。code は書かない。方式の案と判断の項目を出す。

## 0. 結論（推奨）と判断の項目

推奨: **案 A（自己完結の app の bundle、利用者ごとの導入、署名した静的な index の repository）**。段 1 は root の口・setuid・sandbox・外部の package を足さずに作れ、後の段で system 全体の導入（特権の helper）・HTTPS・sandbox を足せる。

| 番号 | 判断の項目 | 推奨 | 理由 |
| --- | --- | --- | --- |
| U1 | 導入の範囲: 利用者ごと（`~/.local/share/keiland/apps/`）か、system 全体（`/apps/`、管理者） | 段 1 は利用者ごと。system 全体は後の段（account-admin と同じ形の小さな特権の helper、別に設計して承認） | 利用者ごとなら root の口が要らない（security.md「The desktop holds no privilege」）。system 全体は root の書き込みが要る |
| U2 | package の形: 自己完結の bundle（案 A）か、共有の依存を解く package（案 B） | 案 A | 依存の解決・衝突が無く、消すのが dir を消すだけ。zedBSD の platform の ABI（libc・libkeiland・libvulkan・libwayland-client）だけに依る |
| U3 | 転送: HTTP と署名の index（段 1）か、HTTPS（TLS の library が要る） | 段 1 は HTTP と署名。HTTPS は後の段（OpenSSL の package を backend が使うか、の判断） | zedBSD の `fetch` は HTTPS を持たない（`userland/base/fetch/main.c`）。改竄は署名と SHA-256 で防げる（盗み見は防げない） |
| U4 | 署名の方式と実装: Ed25519（RFC 8032）を自前で書くか、OpenSSL の libcrypto を使うか | Ed25519 を RFC 8032 から自前で書く（外部の code を写さない、license の監査が要らない）。署名を作る道具は host の Python（`cryptography` の module か自前） | 検証だけなら小さい（SHA-512 と curve25519 の演算）。外部の package に依らない |
| U5 | 公式の repository を誰が運営し、鍵を誰が持つか。利用者が他の repository を足せるか | 公式の repository の公開鍵を image に入れる（鍵はユーザーが持つ）。他の repository は Settings で足せる（URL と公開鍵、足す時に警告） | 信頼の起点を明示する |
| U6 | sandbox | 段 1 は無し（入れた app は利用者の全ての権限で動く）。Apps の頁と導入の確認でそう示す。sandbox は kernel の仕組み（namespace・capability など）が要るので別の WS | zedBSD の kernel に sandbox の仕組みが無い |
| U7 | package の名前と拡張子 | `.kapp`（Kei app）、MIME `application/x-kei-app` | Keiland の名前を画面に出さない規則（WS079 の制約と同じ）に合う |
| U8 | 開発者向けの SDK（sysroot・header・build の手順） | 別の WS（third-party が app を作るのに要る）。WS153 の試験の app は tree の中で build する | 範囲を保つ |

## 1. 案の比較

| 案 | 中身 | 利点 | 欠点 |
| --- | --- | --- | --- |
| **A. 自己完結の bundle**（AppImage・macOS の .app・Haiku の hpkg に近い） | 1 つの archive に app の program・私的な library・data・icon・manifest。platform の ABI の library だけを system から使う | 依存の解決が無い。入れる・消すが dir の単位で原子的。版の並存（更新の失敗で前の版に戻せる）。利用者ごとに入れられる | 同じ library が app ごとに重複する。私的な library の security の更新は app ごと |
| B. 共有の依存の package（FreeBSD の pkg・Debian の apt） | package の間の依存を index で解き、共有の library を `/usr/local` に | 小さい。library の更新が一度で済む | 依存の解決・衝突・部分の失敗の回復が要る。third-party の ABI の管理が難しい。system 全体への導入（root）が前提になりやすい |
| C. sandbox の runtime（Flatpak・Snap） | runtime（共有の基盤）と app と sandbox | 隔離・権限の管理 | kernel に namespace・mount の隔離などが要る。最も重い。zedBSD では今は作れない |

推奨は A。B は system の更新（WS152）と重なり、C は kernel の仕組みを待つ。

## 2. package の形式（案 A）

- **archive**: POSIX の ustar（tree の `pax` が読み書きできる）を zlib（deflate）で圧縮した物（`libz-compat` は inflate を持つ。圧縮は host の道具だけが行う）。拡張子 `.kapp`。
- **中身**:
  ```
  manifest            （必須、下の形）
  bin/<program>       （manifest の exec が指す）
  lib/                （私的な共有 library、任意）
  share/icon.png      （任意、App Home・Apps の頁の絵、256×256 まで）
  share/...           （data）
  licenses/...        （必須、app と私的な library の license の文）
  ```
  archive の中の path は相対で、`..`・絶対 path・symlink の外向き・device・FIFO・setuid/setgid の bit を含めない（導入の時に検めて断る）。file の mode は 0644・0755 だけを残す。
- **manifest**（UTF-8、`key=value` の行、未知の key は無視）:
  ```
  format=1
  id=org.example.Paint          （逆の DNS の名前、[A-Za-z0-9.-]、3〜127 byte）
  name=Paint
  version=1.4.2                 （点で区切った数、比べられる）
  abi=zedbsd-amd64-1            （platform の ABI の名前、§3）
  requires-keiland=32           （最低の KL_VERSION）
  exec=bin/paint
  icon=share/icon.png
  summary=A simple paint program.
  license=MIT
  keywords=paint draw image
  open-with=image/png image/jpeg     （Files の Open With に出す MIME、任意）
  ```
- **導入の場所**（U1 の段 1）: `~/.local/share/keiland/apps/<id>/<version>/`。`current` の symlink が今の版を指す（更新は新しい版の dir を作って検めてから symlink を差し替える = 原子的、失敗したら前の版のまま）。古い版は次の更新で消す。
- **私的な library**: zedBSD の rtld は `DT_RUNPATH` を読むが `$ORIGIN` を展開しない（`src/rtld/rtld.c`）。導入の場所が利用者ごとに違うので、起動は `LD_LIBRARY_PATH=<dir>/lib` を付けて行う（起動の仕組みが付ける。§5）。`$ORIGIN` の展開を rtld に足すのは別の判断（kernel・rtld の WS）。

## 3. 依存と ABI

- app は platform の library（`libc.so`・`libkeiland.so`・`libvulkan.so`・`libwayland-client.so`・`libtruetype.so`・`libz-compat.so` など、image の `/lib` の物）だけを system から使う。それ以外は bundle の `lib/` に入れる。
- `abi` の名前は platform の ABI の版: `zedbsd-amd64-1`（libc/rtld の私有 ABI の版が変わる（今は version 5、`docs/reference/tls.md`）か、`/lib` の library の soname が変わったら数を上げる）。index は 1 つの id に ABI ごとの build を持てる。
- libkeiland は KL_VERSION で後ろ向きに互換（呼び出しを足すだけ）。`requires-keiland` が入っている libkeiland の版より新しければ導入を断る。
- Linux・FreeBSD の Keiland: 同じ形式で ABI の名前が違う（`linux-x86_64-glibc-1`、`freebsd-amd64-1`）。同じ repository に並べられる。

## 4. repository

- **静的な file の server**（HTTP、U3）:
  ```
  <base>/index                 （UTF-8 の行の形、下）
  <base>/index.sig             （index の Ed25519 の署名、64 byte）
  <base>/packages/<id>-<version>-<abi>.kapp
  <base>/icons/<id>.png
  ```
- **index**（1 行 1 package、TAB 区切り、先頭に `format 1` と `generated <UTC>` と `expires <UTC>`）:
  ```
  package	<id>	<version>	<abi>	<size>	<sha256>	<name>	<summary>	<license>	<requires-keiland>	<path>	<icon-path>	<icon-sha256>
  ```
- **信頼**（U4・U5）: repository ごとに Ed25519 の公開鍵。index を取ったら署名を検め（合わなければ使わない）、`expires` を過ぎた index は使わない（古い index で古い弱い版を入れさせる攻撃を防ぐ）、前に見た `generated` より古い index も使わない。package は index の大きさと SHA-256 で検める（package 自身の署名は要らない）。
- **repository の一覧**: system の `/etc/keiland/repositories.d/<name>.conf`（公式、image に入る: `url=`・`key=` の base64）と、利用者の `~/.config/keiland/repositories.d/`（Settings で足す。足す時に「この repository の app は利用者の全ての権限で動く」と示す）。
- mirror: 同じ index と署名を複数の URL に置けば、client は順に試す（`url=` を複数）。

## 5. 導入・更新・削除と登録

- **流れ**（利用者ごと、特権なし）: index を取る → 署名・期限を検める → package を取る → 大きさ・SHA-256 を検める → 一時の dir に展開しながら path・種類・mode を検める（§2）→ manifest を検める（id・abi・requires-keiland・exec が在る）→ `<id>/<version>/` に rename → `current` を差し替える → 登録 → 古い版を消す。途中の失敗は一時の dir を消して前の状態のまま。
- **登録**: App Home の一覧は今 `/etc/keiland/apps.conf` だけを読む（`wayland/home.c`）。利用者の app は `~/.local/share/keiland/apps/<id>/current/manifest` から compositor が読み、App Home に足す（apps.conf の `name|command|keywords|color|picture` に当たる物を manifest から作る。絵は `icon.png`）。Files の Open With は `$XDG_CONFIG_HOME/keiland/open-with`（`files/apps.c`）に `open-with` の MIME を足す（消す時に除く）。
- **起動**: App Home・Files は manifest の exec を `LD_LIBRARY_PATH=<current>/lib <current>/bin/<program>` で起動する（compositor の `zwl_spawn`、XDG_ACTIVATION_TOKEN も付く）。
- **更新**: Apps の頁を開いた時と 1 日 1 回（compositor が動いている間）、index を取って入っている app の新しい版を探し、Apps の頁に「更新」を出す（自動で入れない。段 2 で選べるように）。system の更新（WS152）とは分けるが、取得・署名・検めの code は共通にする（WS152 が同じ index の形を使えるように）。
- **削除**: `<id>/` を消し、登録を除く。利用者の data（`~/.config/<app>` など、app が決める場所）は残す（Apps の頁に「data も消す」の選択は段 2、app が data の場所を manifest に書く形）。

## 6. 層の配置（Guardrail「配置」「app と設定」）

- Settings は OS の口を持たないので、Apps の頁は libkeiland の `kl_system_apps_*`（拡張 `kl_system_manager_v1` の新しい object `kl_system_apps_v1`）だけを使う。
- **compositor**: 入っている app の一覧（App Home の登録に要る）、repository の catalog、導入・更新・削除の依頼と進み・結果を object に送る。
- **libkeiland-backend の共通の code**（`libkeiland-backend/apps/`、OS に依らない）: 取得・検め・展開を利用者の権限の helper の process `keiland-appd`（`/usr/libexec/keiland-appd`、WS145 の printd と同じく backend が `posix_spawn` で起動し、socketpair で行の約束、仕事が無くなれば終わる）に任せる。compositor の event loop で network・disk を待たない。
- **keiland-appd**: HTTP の client（zedBSD の `fetch` の code を library の形にして使うか、小さく書く）、Ed25519 の検証（U4、`userland/base/common/` に置き、WS152 と共有）、SHA-256（`userland/base/common/sha256.c` が在る）、inflate（libz-compat）、ustar の展開（pax の code の一部を library に）。
- Linux・FreeBSD の Keiland も同じ keiland-appd（ABI の名前だけが違う）。

## 7. Settings の Apps の頁

- **Installed**: 入っている app（system の app は「built in」で消せない、利用者が入れた app は版・大きさ・repository）。選んで Update・Remove。
- **Browse**: repository の catalog（icon・名前・要約・license・大きさ）、検索（名前と keywords）。選んで Install（確認の card: 「この app は利用者の全ての権限で動きます」と repository の名前）。進みの bar と取り消し。
- **Repositories**: 一覧と Add（URL と公開鍵）・Remove（公式は消せない）。
- **Default apps**（段 2）: MIME ごとの既定の app（Files の open-with の既定と同じ store）。

## 8. 安全

- 署名と期限で index の改竄・巻き戻しを防ぐ。package は SHA-256 で検める。
- 展開の検め（§2）で path の外への書き込み・symlink・特殊な file・setuid を防ぐ。展開の大きさの上限（package 256 MiB、展開 1 GiB、file 数 10000）。
- 段 1 は sandbox が無いので、入れた app は利用者の全ての file と network に触れる（U6）。repository の信頼がそのまま app の信頼。Apps の頁と導入の確認で明示する。
- keiland-appd は待ち受けの socket を持たず、setuid でない。log に URL・id・結果を残す（利用者の file の名前は残さない）。

## 9. 試験

| 層 | 試験 |
| --- | --- |
| 形式・署名 | host: Ed25519 の検証を RFC 8032 の試験の vector で、manifest・index の解析、展開の検め（`..`・絶対 path・symlink・device・setuid・大きさの上限の fuzz の archive） |
| keiland-appd | host: Python の静的な HTTP の server の repository（署名の正しい・誤った・期限切れ・巻き戻しの index、SHA-256 の合わない package、途中で切れる転送）で導入・更新（失敗で前の版のまま）・削除 |
| compositor・libkeiland | host: `plan/ws131/tests/host-system.sh` の形で kl_system_apps_*、App Home の登録 |
| QEMU | T1: 試験の repository（host の HTTP、user-net の 10.0.2.2）から tree の中で build した試験の app を Settings で入れ、App Home に出て起動し、更新・削除。PNG |
| Linux | Debian の QEMU+KVM の guest で同じ |

## 10. Phase の分け方（案）

| Phase | 内容 | 依存 |
| --- | --- | --- |
| p002 | 形式と道具: manifest・index の仕様の確定、Ed25519 の検証（自前、RFC 8032）、host の道具 `tools/kapp/`（package を作る・index を作り署名する、Python）、host 試験 | p001（U2・U4・U7） |
| p003 | keiland-appd: HTTP の取得・署名と SHA-256 の検め・展開と検め・導入の場所・更新・削除、host 試験 | p002（U1・U3） |
| p004 | backend の apps・compositor の拡張 `kl_system_apps_v1`・libkeiland の口・App Home と Files の登録 | p003 |
| p005 | Settings の Apps の頁（Installed・Browse・Repositories） | p004 |
| p006 | 試験の repository と試験の app、QEMU（T1）と Linux の確認 | p005 |
| p007 | 全文の規約の確認と回帰 | p002〜p006 |
| 後の段（別の Phase か WS） | system 全体の導入（特権の helper、U1）、HTTPS（U3）、sandbox（U6、kernel）、SDK（U8）、Default apps・data の削除の選択、自動の更新 | — |

## 11. 既存の仕組みの調べ（方式だけ、code は写さない）

- AppImage: 1 file の自己完結（squashfs を FUSE で mount）。zedBSD に FUSE・squashfs は無いので、展開して置く形にした。
- macOS の .app・Haiku の hpkg: dir の bundle と manifest。Haiku は package を mount する（展開しない）が、zedBSD では展開する。
- Flatpak・Snap: runtime と sandbox。kernel の隔離が要る（案 C）。
- FreeBSD の pkg・Debian の apt: 共有の依存と署名した index（案 B）。index の署名と期限（apt の `Valid-Until`）の考えは案 A でも使う。
