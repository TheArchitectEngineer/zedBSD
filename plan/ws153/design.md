<!-- awesome-plan project=zedbsd record=ws153-design -->

# WS153 の設計: third-party の app の repository と Settings の Apps の頁（ws153-p001、第 4 版 2026-10-05 P2 g15、q761）

[WS153](ws.md) の単一目標「Settings に Apps の頁を足し、third-party の app の repository から app を探す・入れる・更新する・消すことができるようにする」の方式の検討。ユーザーの指示（2026-10-04 夜）: 「パッケージシステムは userland/packages ではなくて、サードパーティーアプリのリポジトリのことです。」。code は書かない。方式の案と判断の項目を出す。第 2 版は第 1 版への敵対的レビュー（重大 6・中 13・軽 8、[phase001](phase001/phase.md) に要旨）を、第 3 版は第 2 版への 2 回目のレビュー（重大 2・中 12・軽 11）を、第 4 版は第 3 版への 3 回目のレビュー（重大 1・中 11・軽 16）を反映した。

## 0. 結論（推奨）と判断の項目

推奨: **案 A（自己完結の app の bundle、利用者ごとの導入、repository の鍵で署名した静的な index）**。段 1 は root の口・setuid・sandbox を足さずに作れ、後の段で system 全体の導入（特権の helper）・HTTPS・sandbox を足せる。

| 番号 | 判断の項目 | 推奨 | 理由 |
| --- | --- | --- | --- |
| U1 | 導入の範囲: 利用者ごと（`$XDG_DATA_HOME/keiland/apps/`）か、system 全体（管理者） | 段 1 は利用者ごと。system 全体は後の段（account-admin と同じ形の小さな特権の helper、別に設計して承認） | 利用者ごとなら root の口が要らない（security.md「The desktop holds no privilege」） |
| U2 | package の形: 自己完結の bundle（案 A）か、共有の依存の package（案 B） | 案 A | 依存の解決・衝突が無く、消すのが dir を消すだけ |
| U3 | 転送（hosting と組） | 3 案の比較: (i) HTTP と署名（外部に依らない。盗み見は防げない。HTTPS へ redirect する hosting（一般の静的な hosting の多く、推測）では使えない）、(ii) HTTPS を libbrowser（`libbrowser/net/tls.c`）と同じく OpenSSL を dlopen して使い、`/etc/ssl/cert.pem` で証明書の鎖を検める（既定の image に openssl と ca-certificates の package を入れる。今は既定で off）、(iii) libcurl。推奨は公式の hosting が HTTP で出せれば (i)、出せなければ (ii)（既存の方式の再利用）。desktop の package は今 amd64 だけなので、arm64・i386 の考慮は要らない | zedBSD の `fetch` は HTTPS を持たない |
| U4 | 署名の検証の実装（U3 と組） | 案 a: Ed25519（RFC 8032、pure）の検証・SHA-512・SHA-256 を appd に自前で書く（U3 が HTTP の時。外部の package に依らない）。案 b: libcrypto の EVP で検証する（U3 が HTTPS で appd がどのみち OpenSSL を使う時）。推奨は U3 に合わせて a か b。署名を作る側は host の Python の `cryptography`（または OpenSSL の `pkeyutl -rawin`）で、署名の対象は index の byte 列そのもの（pure Ed25519。OpenSSH の SSHSIG の形式は使わない） | 自前は検証の誤りの危険があるが外部に依らない。OpenSSL を使うなら自前は重複。tree の既存の code（libc の crypt.c の SHA-512、cksum・libpdf の SHA-256、openssh の package の Ed25519）は license と配置を見て再利用の候補にする |
| U5 | 公式の repository: 運営するか、誰が鍵を持つか、image に既定で入れるか、期限の長さと再署名の手順、hosting、鍵が漏れた時 | 公式の repository を運営し、ユーザーが鍵を持ち、image に既定で入れる。index の期限は 30 日、再署名は 1〜2 週ごと（手順は p008）。鍵は 2 つまで並べられ（鍵の ID つき）、交換は古い鍵で署名した「次の鍵」の記録を index に載せて引き継ぐ（TUF の root の交換の最小形）。段 1 の制限: 鍵が漏れて次の鍵の記録も偽られた時は、system の更新（WS152）まで失効できない | 信頼の起点と運用をユーザーが決める |
| U6 | sandbox が無いこと | 段 1 は無し（入れた app は利用者の全ての権限で動く）。Apps の頁と導入の確認でそう示す。sandbox は kernel の仕組みが要るので別の WS | zedBSD の kernel に sandbox の仕組みが無い |
| U7 | package の名前と拡張子 | `.kapp`、MIME `application/x-kei-app` | Keiland の名前を画面に出さない |
| U8 | 開発者向けの SDK | 別の WS。WS153 の試験の app は tree の中で build する | 範囲を保つ |
| U9 | **platform の ABI の安定の方針**: third-party の binary に、どの library のどんな変更までを互換と約束するか | 案 a（推奨、段 1）: `/etc/keiland/abi` を ABI の名前の正本にし、約束する library を列挙して選ぶ（§3）。次のどれかで ABI の名前の数を上げる: 約束した library の symbol の削除、**公開の構造体・listener の表の大きさや並びの変更**（libkeiland は KL 29 で `kl_network_link` を、KL 7 で titlebar の listener を伸ばした）、libc の公開の ABI（構造体の大きさ・symbol）の変更、libc/rtld の私有 ABI の版、soname の変更。静的に link した program は形式で断る（kernel の UAPI に直に依るため）。合わなくなった app は Installed に「この system では動かない（更新を待つ）」と出し、起動しない。案 b: libkeiland の listener と出力の構造体に大きさの field を持たせ、KL の版で切り替える（構造体が伸びても ABI の名前を上げずに済む。libkeiland 全体の方針の変更） | 凍結や libkeiland の方針の変更はプロジェクト全体の判断。案 a は ABI の名前が上がる度に third-party の app の作り直しが要る |
| U10 | 手元の `.kapp`（署名の無い file）の導入を許すか | 段 1 は許さない（repository からだけ）。Files で `.kapp` を開くと「repository から入れてください」と出す | 署名の無い app は検めようがない |
| U11 | 更新の確認の自動の問い合わせ（1 日 1 回）を既定で on にするか | 既定 on、Apps の頁で off にできる。問い合わせるのは index だけ（入っている app の一覧は送らない） | privacy |
| U12 | 展開の上限と inflate | package 256 MiB、展開 1 GiB、file 1 万、archive の中の path 255 byte（ustar の上限、pax の拡張 header は使わない）・深さ 32。**inflate**: tree の `libz-compat` は stream でなく（入力を全て持ち、一度に展開し直す）、出力の上限も無い（realloc で倍々）ので、展開の上限と「展開しながら検める」が成り立たず、圧縮の爆弾で memory を使い切れる。案: (a) appd に出力の上限を持つ stream の inflate を自前で書く（推奨、p004、fuzz と爆弾の試験つき）、(b) libz-compat を本当の stream に直す（base の library、他の WS への依頼）、(c) 形式を file ごとの圧縮にして大きさを先に宣言させる。icon の PNG も libpng-compat が `uncompress` を使うので、decode の前に IHDR の幅・高さ（256 まで）と IDAT の合計（1 MiB まで）を検め、appd の上限つきの inflate で decode する | 利用者の disk と memory を守る |
| U13 | 導入・削除・repository の追加の確認を誰が描くか | compositor が描く確認の dialog（Settings 以外の client が `kl_system_apps` で依頼しても、利用者が compositor の dialog で承認しない限り進まない。同じ client からの dialog は 1 つずつ、断られたら 10 秒は出さない） | 同じ uid の client なら誰でも拡張を使える（`wayland/settings.c` の peer の検め）ので、Settings の card だけでは確認にならない。ただし同じ uid の process は `repositories.d/`・`apps/` を直に書けるので、dialog が防ぐのは利用者を経る流れ（`.krepo` を開く、Settings 以外からの依頼）だけ（§8 の脅威の模型）。後で sandbox を足す時も同じ口のまま使える |
| U14 | Default apps（MIME ごとの既定の app）の UI を段 1 に入れるか | 段 2（段 1 は Files の Always Open With で third-party の app を既定にできない、§5） | 観点 7 の一部を後へ送る判断 |

判断の期限: U1・U2・U3・U4・U5（hosting を含む）・U6・U7・U8・U9・U12 は p002 の前（docs が全てを要る）、U10 は p005 の前、U13 は p006 の前、U14 は p006 の前、U11 は p007 の前。

WS152（system の更新）とは、取得・検めの code と index の形を共通にできる見込みがあるが、WS152 はベータ4 以降で planning なので、ここでは決めず Q1 と調整する項目にする。段 1 は app の更新を Apps の頁だけで出す（Updates の頁には出さない）。段 1 の制限: session A で入れた app は session B の App Home に、B の次の LIST（Apps の頁を開く、1 日 1 回の確認）まで出ない。WS145（printd）はまだ実装されていないので、backend の子の process の起動と行の約束の基盤は WS153 と WS145 のどちらか先の方が作る（Q1 と調整）。

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

- **archive**: POSIX の ustar を **zlib の wrapper の deflate** で圧縮した物（`libz-compat` は zlib の wrapper と raw deflate を読み、gzip の wrapper は読まない。`.tar.gz` の道具では作れないので p004 の道具が作る）。拡張子 `.kapp`。
- **中身**:
  ```
  manifest            （必須）
  bin/<program>       （manifest の exec が指す regular file）
  lib/                （私的な共有 library、任意）
  share/icon.png      （任意、256×256 まで）
  share/...           （data）
  licenses/...        （必須）
  ```
- **展開の検め**（展開器は appd に新しく書く。tree の `pax` は先頭の `/` を取り除く・hard link を作る・拡張 header で名前を上書きする・FIFO を作る方針なので使い回さない）: 一時の root の dirfd を基準に、途中の要素も 1 段ずつ `O_DIRECTORY|O_NOFOLLOW` で開き、`openat`・`mkdirat`、`O_NOFOLLOW|O_EXCL` で作る。断る物: 絶対 path・`..`・空の要素、hard link（LNKTYPE）、symlink（`lib/` の中の soname の link で、目標が同じ dir の file の名前（`/`・`..` を含まない）である物だけ許す）、device・FIFO・socket、setuid・setgid・sticky の bit、同じ path の重複の entry、dir と file の入れ替わり、pax の拡張 header（`x`・`g`、自分の道具は作らないので全て断る）、U12 の上限の超え。file の mode は 0644・0755 だけを残す。
- **manifest**（UTF-8、`key=value` の行、未知の key は無視、同じ key の重複は断る）:
  ```
  format=1
  id=org.example.Paint          （英字で始まる英数字と '-' の label を '.' 1 つで区切った 3 つ以上、5〜48 byte。`gtk-3.0` のような他の program の設定の名前と重なる形を避ける）
  name=Paint                    （表示の名前、1〜64 byte）
  version=1.4.2                 （1〜4 個の 0〜65535 の 10 進を '.' で区切る、先頭の 0 と pre-release は断る）
  abi=zedbsd-amd64-1
  requires-keiland=32
  exec=bin/paint                （bin/ の後に [A-Za-z0-9._-] の 1〜32 byte、regular file で mode 0755、動的に link した ELF（静的な link は断る））
  icon=share/icon.png
  summary=A simple paint program.   （1〜200 byte）
  license=MIT                   （SPDX の識別子、64 byte まで）
  keywords=paint draw image     （200 byte まで）
  app-id=org.example.Paint      （窓の xdg の app_id、bar がまとめる名前、id の規則で 48 byte まで（bar の key は 72 byte）。省略なら id）
  open-with=image/png image/jpeg     （type/subtype の literal だけ、glob・TAB・',' は断る）
  data=config cache            （「data も消す」の対象: config・data・cache の語だけで、`$XDG_CONFIG_HOME/<id>`・`$XDG_DATA_HOME/<id>`・`$XDG_CACHE_HOME/<id>` を指す。任意の path は書けない）
  ```
  表示の文字列（name・summary・keywords）は UTF-8 として正しく、C0・C1・DEL・bidi の制御（U+202A〜U+202E・U+2066〜U+2069）を含まない。
- **導入の場所**（U1）: `$XDG_DATA_HOME/keiland/apps/<abi>/<id>/<n>/`（既定 `~/.local/share`。zedBSD と Linux の Keiland で home を共有しても ABI ごとに分かれる。`<n>` は版ごとの短い通し番号で、版の文字列は `<n>/.version` に書く）。`<id>/current` の symlink が今の `<n>` を指す。
- **path の長さ**（zedBSD の rtld は path を 256 byte までしか扱わず、`AT_EXECFN` が 256 以上なら `$ORIGIN` を展開しない、`src/rtld/rtld.h`・`rtld.c`）: appd は導入の時に、実際の導入の path で `<n>/bin/<exec>` と、`lib/` の全ての file の `<n>/bin/../lib/<name>` が 255 byte 以下か（`<n>` と `current` の長い方で数える）を検め、超えれば理由（home の path が長い）を出して断る。home の名前や `XDG_DATA_HOME` が後で変わった時のため、compositor は起動の前に長さを検め直して理由を出す。terminal から相対の path で起動すると `$ORIGIN` が効かないので、起動は compositor の絶対 path に限ると docs に書く（rtld で cwd を基準に解く修正は rtld の WS への依頼の候補）。
- **私的な library**: zedBSD の rtld は `DT_RUNPATH` の `$ORIGIN`・`$ORIGIN/...` を展開する（`src/rtld/rtld.c` の `open_search_list`）。形式の約束: `bin/` の program は `DT_RUNPATH=$ORIGIN/../lib`、`lib/` の library どうしの依存は各 library に `DT_RUNPATH=$ORIGIN`（RUNPATH は推移しない）。`LD_LIBRARY_PATH` は使わない（app が起動する子の process に引き継がれ、system の program に私的な library を読ませるため）。導入の時、`lib/` の file 名が platform の library の soname（`/etc/keiland/abi` に一覧）と重なれば断る（platform の library を覆わせない）。p004 の道具と appd は ELF を検める: DT_RPATH が無い（rtld は RUNPATH の無い requester で親の RPATH を辿るので、platform の library の探索に app の `lib/` が入りうる）、DT_RUNPATH は `$ORIGIN` から始まる物だけ、DT_NEEDED は platform の一覧と `lib/` の file の和に含まれる（platform の外の `/usr/lib` の library に黙って依らない）、e_machine と class が `abi` と一致、ET_DYN（PIE）、PT_INTERP が `/lib/ld.so`、DT_TEXTREL が無い、`lib/` の物に依る library は RUNPATH=`$ORIGIN` を持つ。

## 3. 依存と ABI（U9）

- app は platform の library だけを system から使う。それ以外は bundle の `lib/` に入れる。platform の一覧は、約束する library（U9 で選ぶ。案: `libc.so`・`libkeiland.so`・`libvulkan.so`・`libwayland-client.so`）とそれらの DT_NEEDED の推移の閉包（例: libkeiland が依る `libtruetype.so`・`libpng-compat.so`・`libz-compat.so`）。閉包の library（base の program のための内部の library を含む）を third-party の ABI として約束するかも U9 で選ぶ（約束しない物は閉包として soname の重なりの検めにだけ使い、app の DT_NEEDED には許さない）。
- `/etc/keiland/abi`（image が持つ、正本）: `abi=zedbsd-amd64-1`、`keiland=<KL_VERSION>`、`built=<image を作った UTC>`、platform の soname の一覧。appd はこれを読んで manifest の `abi` と `requires-keiland` を比べる（appd は libkeiland を link しないので、libkeiland の版もこの file から）。
- ABI の名前の数を上げる時（U9 の案 a と同じ列挙）: 約束した library の symbol の削除、公開の構造体・listener の表の大きさや並びの変更、libc の公開の ABI（構造体の大きさ・symbol）の変更、libc/rtld の私有 ABI の版、platform の library の soname の変更。U9 で案 b を選べば、構造体と listener の変更は大きさの field と KL の版で吸収し、ABI の名前は上げない。上げ忘れを止めるため、`plan/tools/` に checker を作る（exports.map の一覧、公開の構造体の sizeof・offsetof を生成した C で数えた値、soname の一覧を `/etc/keiland/abi` の記録と比べ、ABI の名前を上げずに変われば build を止める。p002 で作る）。`/etc/keiland/abi` は image の build が生成する（p002）。
- **symbol の割り込み**: zedBSD の rtld は main から読み込みの順に名前だけで symbol を探し、最初に定義した object が勝つ（版の無い名前）。app の私的な library が platform の library と同じ名前の symbol（`wl_*`・`uncompress` など）を export すると、platform の library の呼び出しが app の側に結び付く。導入の検めで、`lib/` の library と main の動的な export が platform の閉包の export（`/etc/keiland/abi` の隣に生成する一覧）と重ならないこと、soname の基の名前（`.so` の後の版を除いた名前）が platform と重ならないことを求める。長期には -Bsymbolic・protected の可視性・symbol の版を U9 の案 b と一緒に検討する。
- **dlopen**: zedBSD の rtld の dlopen は `/lib/` 以外の絶対 path と '/' を含む相対 path を断り、裸の名前でも呼び出し元の RUNPATH を見ない。段 1 の形式では `lib/` は DT_NEEDED で読む物だけで、dlopen の対象にできない（plugin を使う app は作れない）と docs に書く。dlopen の許可は rtld の WS への依頼の候補（U9 に記す）。
- app が自分の `share/` を見つけるには `dladdr` で main の path（rtld は AT_EXECFN を main の名前にする）を取る、と docs に書く。
- 合わない app: Installed に「この system では動かない」と出し、App Home に出さず起動しない。同じ id の今の ABI の build が repository にあれば更新として出す。
- Linux・FreeBSD の Keiland: **段 1 は zedBSD だけ**。Linux・FreeBSD の Keiland は `KEILAND_PREFIX`（既定 `/opt/keiland`、変えられる）に入り、自分の program は `-Wl,-rpath,$(KEILAND_PREFIX)/lib` で libkeiland を見つけ、設定や program の場所も `KEILAND_SYSCONFDIR`・`KEILAND_LIBEXECDIR` になる（`keiland-linux.mk`・`keiland-freebsd.mk`）。`$ORIGIN/../lib` だけでは libkeiland が見つからず、glibc は `LD_LIBRARY_PATH` を RUNPATH より先に見るので、約束は別に作る（launcher が platform の lib の dir を渡す、など）。ABI の名前は `linux-x86_64-glibc<最低の版>-1`・`freebsd-amd64-<major>-1` の形を予約する。本文の `/etc/keiland/...`・`/usr/libexec/...` は zedBSD の場所で、Linux・FreeBSD では `KEILAND_SYSCONFDIR`・`KEILAND_LIBEXECDIR` の下。

## 4. repository

- **静的な file の server**（U3）:
  ```
  <base>/index、<base>/index.sig、<base>/packages/<id>-<version>-<abi>.kapp、<base>/icons/<id>.png（catalog の表示用）
  ```
- **index**（UTF-8、1 行 1 項目、TAB 区切り、4 MiB まで）: 先頭に `format 1`・`repository <name> <id>`（id は運営者が repository を作った時に決める 128 bit の乱数の 16 進）・`generated <UTC>`・`expires <UTC>`、`next-key <鍵の ID> <公開鍵の base64>`（任意、鍵の交換）、続いて
  ```
  package	<id>	<version>	<abi>	<size>	<sha256>	<name>	<summary>	<license>	<requires-keiland>	<app-id>	<keywords>	<path>	<icon-path>	<icon-size>	<icon-sha256>
  ```
  各値は §2 の規則で検める（id・version・表示の文字列）。path は `packages/`・`icons/` の下の `[A-Za-z0-9._-]` の file 名だけ。
- **署名**（U4・U5）: `index.sig` は `<鍵の ID> <Ed25519 の署名の base64>` の行を 1〜2 行。repository の conf の鍵（2 つまで、鍵の ID つき）のどれかで検めが通れば index を使う。
- **期限と巻き戻し**: `expires` を過ぎた index は使わない。前に受けた `generated` より古い index も使わない（記録は `$XDG_STATE_HOME/keiland/apps/<repository ID>.state`。消えたら、または初めての repository なら、image が持つ「image を作った日時」（`/etc/keiland/abi` の `built=`）から許す期限の最大（30 日）を引いた日時を下限にする（新しい image の初回で、image より少し前に署名した期限の内の index を断らない）。p008 の手順に「image を作る直前に公式の index を再署名する」を入れる）。古い mirror に当たったら次の mirror を試す。時計が狂っていると期限で断るので、その時は「時計を確かめてください」と出す（zedBSD の ntpdate は認証が無いので、時刻を信じ切らず `generated` の単調性を主な防御にする。TUF の freeze 攻撃の考え）。
- **鍵の交換の記録**: 公式の conf（`/etc/keiland/repositories.d/`）は利用者の権限で書けないので、index の `next-key`（古い鍵で署名された index に載る）を受け入れたら、利用者の `<repository ID>.state` に新しい鍵を持つ。以後その鍵も検めに使う。`next-key` の行は、その交換の前に作られた image が使われ得る間ずっと index に載せる（U5・p008 の手順）。
- **package の検め**: index の size を 1 byte でも超えたら転送を止め、SHA-256 と size を検める。展開した manifest の id・version・abi が index の行と一致しなければ断る。
- **package の鍵（開発者の署名）との比較**:

  | 案 | 利点 | 欠点 |
  | --- | --- | --- |
  | **repository の鍵だけ**（推奨） | 単純。mirror は署名した index をそのまま置ける。開発者の鍵の配布・失効が要らない | repository の鍵が漏れると、その repository の全ての app が偽れる。手元の `.kapp` は検められない（U10 で許さない） |
  | 開発者の鍵を足す（package を開発者が署名、index に開発者の鍵の指紋） | repository の server が乗っ取られても、開発者の鍵が無ければ package を偽れない。手元の `.kapp` も検められる | 開発者の鍵の登録・失効・交換の仕組みが要る。段 1 には重い |

- **repository の一覧**: system の `/etc/keiland/repositories.d/<name>.conf`（公式、image に入る。`id=`、`url=` を複数（mirror）、`key=<鍵の ID> <base64>` を 2 つまで）と、利用者の `$XDG_CONFIG_HOME/keiland/repositories.d/`（Settings の依頼で backend の writer の thread が書く。足す時は compositor の確認の dialog（U13）で「この repository の app は利用者の全ての権限で動く」と示す）。公式は消せないが無効にできる。公開鍵の手入力を避けるため、repository の記述の file（`.krepo`: name・url・key）を開いて足す形にする。
- **repository の名前と ID**: repository の名前は system と利用者の conf を通して一意（既にある名前・ID の `.krepo` は足すことを断る）。ID は運営者が repository を作った時に決め、`.krepo`・conf（`id=`）・index の `repository` 行の 3 か所に持つ（利用者が消して足し直しても ID は変わらず、`.origin` の app は出所を失わない）。index の `repository` 行の名前と ID が conf と一致しなければ index を使わない（同じ鍵で署名する別の repository の index を使い回させない）。鍵の交換は conf の中で引き継ぎ（次の鍵の記録）、ID は変わらない。
- **出所の固定**（乗っ取りの防止）: 導入の記録 `<n>/.origin` に repository の ID を持ち（版の dir の中に書いてから rename するので、`current` の切り替えで app と出所が原子的に替わる）、更新はその repository からだけ探す。別の repository の同じ id は「別の app」として出し、置き換えるには明示の確認（compositor の dialog）を求める。system の app（`/etc/keiland/apps.conf` の名前、`/bin` の program）と同じ name・app-id の app、入っている他の third-party の app と同じ name・app-id の app は断る（app は実行の時に xdg の app_id を自由に名乗れるので、これは導入の時の見た目の対策）。index の icon と package の `share/icon.png` は、導入の後は package の物を使う（catalog だけ index の icon）。

## 5. 導入・更新・削除と登録

- **流れ**（利用者ごと、特権なし）: index を取る → 署名・期限・単調性 → package を取る → size・SHA-256 → 一時の dir（`apps/<abi>/.tmp-<pid>-<rand>/`、同じ file system）に、appd の上限つきの stream の inflate（U12）で展開しながら検める（§2）→ manifest を検める（index と一致、abi・requires-keiland・exec が在る、soname の重なりが無い）→ `<n>/.version` と `<n>/.origin` を書く → `<id>/<n>/` に rename（同じ版の入れ直しも新しい `<n>` に置く）→ `current` は `symlink(tmp)` の後 `rename(tmp, current)`（原子的）→ 登録 → 古い版の扱い（下の起動の規則）。
- **書き手と競合と回復**: `apps/<abi>/` の下は appd だけが書き、backend は repository の conf だけを書く。appd は `apps/<abi>/.lock`（flock）を持つ間だけ書く（同じ利用者の 2 つの session）。lock を取った後に `.tmp-*`・`.trash-*` を全て消す（lock を持つのは自分だけなので、残っている物は全て前の crash の残り）。`current` が無い・壊れた `<id>/` は、`.version` の在る最も新しい完全な `<n>` に戻す。削除は `<id>/` を `.trash-…` に rename してから消す。ENOSPC は失敗として前の状態のまま。
- **取り消し**: 取得・展開の間は取り消せ（一時の dir を消す）、`<id>/<n>/` への rename の後は取り消さない（完了まで進めて結果を返す）。
- **起動**: appd が `current` を解いた実の path（`<id>/<n>/bin/<exec>`）を一覧に入れて渡し、compositor はそれを argv の形（shell を通らない `posix_spawn`）で起動する（compositor は disk を読まない。`zwl_spawn` は `/bin/sh -c` で 160 byte の command なので使わない。p006 で compositor に argv の spawn を足す）。動いている process は自分の版の dir を使い続ける。古い版は pid では数えられない（孫の process、別の session、terminal からの起動）ので、**3 版目を入れる時に最も古い版を消す**（最大 2 版。別の session で古い版が動いていれば、その process が `share/` を開く時に失敗しうる、段 1 の制限）。`<n>` は `<id>` の外の通し番号（`apps/<abi>/.serial`）から振り、app を消して入れ直しても使い直さない。
- **登録**: 一覧と icon は appd が検めて（icon は decode して 256×256 の RGBA に切って）backend に渡す。compositor は disk も PNG も読まない（WS135 の「session の間 disk を待たない」）。App Home の一覧は今「最初に開いた時に一度読む」・48 個まで・絵は `icons.c` の名前だけなので、実行中の追加と削除、上限、RGBA の icon の描画を足す。bar（`apps-bar.c`）は manifest の `app-id` で窓をまとめ、その icon を使う。p006 でこれらを足す。
- **Files の Open With**: 利用者の `open-with` は利用者が書く file なので触らない。Files は導入した app の候補を `kl_system_apps` に問い合わせ（MIME で絞った id と名前の一覧、ABI ごとの list の file を Files が読むのではない）、起動は `kl_system_apps_launch(id, path)` で compositor の argv の経路に頼む（Files の `sh -c` を通さない）。候補に出るだけで既定は変わらない。段 1 では Files の Always Open With（`files.open-with.<type>`）で third-party の app を既定にできない（U14、Default apps は段 2）。p006 で Files の側を足す。
- **更新**: Apps の頁を開いた時と 1 日 1 回（U11）、index を取り、入っている app の origin の repository の新しい版を探して Apps の頁に「更新」を出す（自動で入れない）。
- **削除**: `<id>/` を消し、登録を除く。利用者の data（manifest の `data=` の語が指す `$XDG_*_HOME/<id>`）は残し、Apps の頁の「data も消す」を選んだ時だけ、compositor の dialog で実際の path を見せてから消す（symlink を辿らない）。

## 6. 層の配置（Guardrail「配置」「app と設定」）

- Settings は libkeiland の `kl_system_apps_*`（拡張 `kl_system_manager_v1` の新しい object `kl_system_apps_v1`）だけを使う。
- **compositor**: 入っている app の一覧（App Home・bar に要る、appd が解いた実の path つき）、`kl_system_apps_launch(id, path)`（Files・App Home の起動）、catalog の検索と頁送り（compositor の側で検索して頁ごとに送る。Wayland の message の大きさの上限のため catalog を丸ごと送らない）、icon は shm の fd で渡す、導入・更新・削除・repository の追加の確認の dialog（U13）、進みと結果。
- **libkeiland-backend の共通の code**（`libkeiland-backend/apps/`、OS に依らない）: appd の起動（WS145 の printd と同じ `posix_spawn`・socketpair・行の約束・寿命の形）、利用者の repository の conf の書き込み（writer の thread、flock）、appd の `LIST` の受け取り。`apps/<abi>/` の下（導入の記録を含む）は appd だけが書く。
- **keiland-appd**（`userland/desktop/appd/`、`/usr/libexec/keiland-appd`、Makefile を持つ（段 1 は zedBSD だけ。Makefile.linux・freebsd は後の段）。WS145 の printd と同じ置き方）: HTTP の client（自前、接続と読みの timeout（進みの無い 60 秒）・取り消し・header 16 KiB・index 4 MiB・package は index の size まで・redirect は同じ scheme で 3 回まで。U3 で HTTPS なら OpenSSL か libcurl を比べて選ぶ）、署名の検証（U4 の案 a なら Ed25519・SHA-512・SHA-256 を appd の私的な file に自前で、案 b なら libcrypto）、inflate（libz-compat、3 つの OS の build に在る）、ustar の展開器（新しく書く）、icon の PNG の decode（libpng-compat）。
- **backend と appd の約束**（p002 の docs と p005 で WS145 §5.1 の形の表にする）: `CATALOG`・`INSTALL <request> <repo-id> <id> <version>`・`REMOVE`・`CANCEL`・`PROGRESS`・`RESULT`・`LIST`（入っている app と実の path と icon の fd）・`IDLE n`/`BYE n`・`FATAL`。行は 1024 byte まで（manifest の表示の文字列の上限はこれに収まる）。appd は待ち受けの socket を持たず setuid でない。log は syslog（URL・id・結果、利用者の file の名前は残さない）。
- KL_VERSION と protocol version は、WS145 の予約（version 10・KL 34）の後になる（順は Q1 と調整）。

## 7. Settings の Apps の頁

- **Installed**: system の app（「built in」、消せない）と利用者が入れた app（版・大きさ・repository、ABI が合わない物はその旨）。Update・Remove（「data も消す」の選択）。
- **Browse**: catalog（icon・名前・要約・license・大きさ・repository）、検索。Install（compositor の確認の dialog: 「この app は利用者の全ての権限で動きます」と repository の名前）。進みの bar と取り消し。
- **Repositories**: 一覧（公式は無効にできる）と、`.krepo` の file から Add・Remove。`.krepo`（`application/x-kei-repository`）と `.kapp` を Files で開くと Settings の Apps の頁が開く（`.kapp` は U10 で「repository から入れてください」）。
- **更新の確認**: 自動の問い合わせの on/off（U11）。
- **Default apps**（段 2）。
- WS148（Privacy）・WS149（Security）との関係: Security の頁に「入れた app の出所（repository の一覧）」と「app は sandbox されない」の表示、Privacy の頁に「更新の自動の問い合わせ」の on/off を出す（その WS の頁の設計に依頼）。

## 8. 安全（まとめ）

- 署名・期限・単調性で index の改竄・巻き戻しを防ぐ。package は size・SHA-256、manifest と index の一致で検める。
- 出所の固定で別の repository の同じ id の乗っ取りを防ぎ、system の app の名前を名乗る app を断る。
- 展開の検め（§2）で path の外への書き込み・link・特殊な file・setuid を防ぐ。
- 起動は argv（shell を通らない）、exec と id の文字集合の制限で注入を防ぐ。私的な library は `$ORIGIN` の RUNPATH で、platform の soname と重なる名前を断る。
- 確認は compositor の dialog（U13）。脅威の模型: 同じ uid の悪意の process は元から利用者の file を書け、`repositories.d/`・`apps/` も直に書けるので、段 1 が防ぐのは「利用者を経る流れ」（悪意の `.krepo`・`.kapp` を開かせる、Settings 以外の app が導入を頼む）と network の改竄と巻き戻しだけ。
- 段 1 は sandbox が無い（U6）。repository の信頼がそのまま app の信頼。

## 9. 試験

| 層 | 試験 |
| --- | --- |
| 暗号 | host: Ed25519 の RFC 8032 の vector と拒否の vector（S ≥ L、非正規の点、小さい位数の点、長さの違う署名、鍵の ID の違い）、SHA-512・SHA-256 の既知の値 |
| 形式 | host: manifest・index の解析（id・version・exec・MIME・表示の文字列の境界）、ELF の検め（DT_RPATH、RUNPATH、NEEDED、静的な link、export の重なり、soname の基の名前、e_machine・PIE・PT_INTERP・TEXTREL）、上限つきの stream の inflate（fuzz、圧縮の爆弾で memory の上限を超えない）、PNG の icon の爆弾、展開の検めの fuzz の archive（`..`・絶対 path・hard link・symlink の外向き・device・FIFO・setuid・重複の entry・dir と file の入れ替わり・pax の拡張 header・path の長さと深さ・上限の超え）、soname の重なり |
| keiland-appd | host: Python の静的な HTTP の server の repository（署名の正しい・誤った・期限切れ・巻き戻し・鍵の交換の index、SHA-256 の合わない・size を超える package、https への redirect、止まる server（timeout）、途中で切れる転送、別の repository の同じ id）、導入・更新（失敗で前の版のまま）・削除、2 つの session の同時の導入、各段（rename と symlink の間、削除の途中）での kill の後の回復、時計の狂い、state の無い初回（新しい image）、`next-key` での鍵の交換、ABI の合わない app |
| compositor・libkeiland | host: `plan/ws131/tests/host-system.sh` の形で kl_system_apps_*、catalog の頁送り、確認の dialog を経ない依頼が進まない、command の長さと shell の特殊文字が起動に影響しない |
| QEMU | T1: 試験の repository（host の HTTP、user-net の 10.0.2.2）から試験の app（`lib/` の私的な library と library から library への依存を持つ）を Settings で入れ、App Home に出て起動（実行中の追加、私的な library が読まれる）、長い id と長い `XDG_DATA_HOME`（path の長さの検めで断る、境界の内なら起動する）、ABI の名前が合わない app を起動しない、`.krepo` の名前の衝突を断る、更新、削除（実行中の削除）、Files の既定が変わらないこと。PNG |
| Linux・FreeBSD | 段 1 の範囲の外（後の段で約束を作ってから） |

## 10. Phase の分け方（案）

| Phase | 内容 | 依存 |
| --- | --- | --- |
| p002 | docs: 形式の仕様（`docs/reference/` に manifest・index・`.kapp`・`/etc/keiland/abi`）と `docs/architecture/security.md` の app の導入の節（目標の設計を先に書く） | p001（U1・U2・U5・U9） |
| p003 | 暗号: Ed25519 の検証・SHA-512・SHA-256（appd の私的な code）と host 試験、独立の review | p002（U4） |
| p004 | 道具と形式: host の道具 `tools/kapp/`（package を作る・index を作る・Python の `cryptography` で pure Ed25519 の署名）、manifest・index・展開器・ELF の検め、host 試験 | p002（U12）。p003 と並行できる |
| p005 | keiland-appd: HTTP（U3）・検め・展開・導入・更新・削除・回復、backend との約束、host 試験 | p004 |
| p006 | backend の apps・compositor の拡張 `kl_system_apps_v1`（`kl_system_apps_launch` を含む）・確認の dialog（U13、compositor に新しく作る UI。後の sandbox では input method の経路から dialog に入力を注入させない）・argv の spawn（`zwl_spawn` と同じく setsid・fd を閉じる・stdin を /dev/null・XDG_RUNTIME_DIR と WAYLAND_DISPLAY・app-id で activation の token、`POSIX_SPAWN_SETSID`）・App Home と bar の動的な一覧と RGBA の icon・Files の候補と起動・libkeiland の口 | p005（約束の表が決まれば compositor の部分は p005 と並行できる） |
| p007 | Settings の Apps の頁（Installed・Browse・Repositories・更新の確認） | p006（U10・U11・U14） |
| p008 | 公式の repository の運用: 鍵の生成と保管、署名・再署名の手順、hosting（ユーザーの関与） | p004（U5） |
| p009 | 試験の repository と試験の app、QEMU（T1）の確認 | p007 |
| p010 | 全文の規約の確認と回帰 | p002〜p009 |
| 後の段 | system 全体の導入（U1）、HTTPS（U3 で段 1 にしない時）、sandbox（U6）、SDK（U8）、開発者の鍵、Default apps、自動の更新、`abi=noct-1`、ABI を上げた時の移行と鍵の交換の経路（WS152 との接続） | — |

## 11. 既存の仕組みの調べ（方式だけ、code は写さない）

- AppImage: 1 file の自己完結（squashfs を FUSE で mount）。zedBSD に FUSE・squashfs は無いので展開して置く。
- macOS の .app・Haiku の hpkg: dir の bundle と manifest。Haiku は package を mount するが、zedBSD では展開する。
- Flatpak・Snap: runtime と sandbox（案 C）。
- FreeBSD の pkg・Debian の apt: 共有の依存と署名した index（案 B）。apt の `Valid-Until` の考えを期限に使う。
- TUF（The Update Framework）: 期限・鍵の交換・freeze と巻き戻しの攻撃の考え。段 1 は TUF の一部（期限・単調性・鍵 2 つ）だけを取り、役割ごとの鍵は後の段。
