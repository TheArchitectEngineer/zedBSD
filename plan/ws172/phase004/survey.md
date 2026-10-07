# ws172-p004: セキュリティチップの調べ（survey）と /dev/securityN の案

版: 第 2 版（2026-10-08、P1。同日、§6 の 2〜5 を一次資料で確かめて直した）。第 1 版を design-reviewer が敵対的にレビューし（blocker 3・major 11・minor 12）、その指摘を全て入れた（§7 に対応表）。
出典の水準: 多くは公開の仕様・文書の要点を記憶から整理した物。第 2 版で一次資料を引き直した物: Linux の `drivers/char/tpm/tpm_crb.c`（start method と Pluton、AMD fTPM の quirk）と `Kconfig`（`TCG_TPM2_HMAC`）、TCG の参照の実装 ms-tpm-20-ref の `DA.c`（DA の意味）、Dell の Latitude 5330 の資料（TPM の種類）、Microsoft Learn の TPM fundamentals（Windows の DA の値）、FreeBSD の tpm2-abrmd の port。他は「（要確認）」を付けたまま。p004b の設計の前に、§6 の確認の一覧を一次資料で確かめる。
ユーザーの判断（§5）の前に必ず確かめる物（第 1 版の B3）: 5330 の TPM の種類と start method（§1.1 の末尾、§6 の 1）。

## 0. 由来と問い

ユーザー（2026-10-05）「TPMは /dev/security0 みたいなインタフェースをきちんと考えて設計、実装したいです。ad hocに/dev/tpm0みたいなのはやりたくないです。世の中のセキュリティチップの、入手可能な大まかな仕様を調べて、どんなインタフェースがあればいいかをサーベイするフェーズをやります。」
同じ日（ws172 の由来）「passkeyコマンドは、将来はセキュリティチップでの認証にも対応する。セキュリティチップはMicrosoft方式もありえるし、独自のプラットフォームの方法も実装できる。」

問い:
1. 世の中のセキュリティチップ（とその OS の口）は、どんな操作を持ち、どこで違うか。
2. zedBSD の汎用の口 `/dev/securityN` は、どの水準で、どこに（kernel か userland の特権の program か）意味を置くか。
3. 最初の利用者（WS172 の passkey の「chip」の方式）に何が要り、何から何を守るのか。

## 1. 対象の要約

### 1.1 TPM 2.0（TCG）

- 仕様: TPM 2.0 Library Specification（Part 1〜4）と PC Client Platform TPM Profile（PTP）。TPM 1.2 は command の体系が別で、**対象にしない**（古い機械の TPM 1.2 は使えないと文書にする）。
- 実体: 単体の chip（dTPM: Infineon・Nuvoton・ST、LPC・SPI・I2C の上）と firmware の TPM（Intel PTT は CSME、AMD fTPM は PSP）。Pluton も TPM 2.0 として見せる（§1.2）。
- 接続の口（PTP）と見つけ方: ACPI の `TPM2` table の start method で決まる。Linux の `tpm_crb.c` が扱う値（一次資料で確認）: `COMMAND_BUFFER`（CRB）、`START_METHOD`（ACPI start: 各 command で `_DSM` を評価する）、`COMMAND_BUFFER_WITH_START_METHOD`（CRB＋ACPI start）、`COMMAND_BUFFER_WITH_ARM_SMC`、`CRB_WITH_ARM_FFA`、`COMMAND_BUFFER_WITH_PLUTON`（専用の start と reply の address、doorbell）。FIFO（TIS、MMIO `0xFED40000`、locality）は `tpm_tis` が扱う。arm64 は device tree・FF-A・SMC の口もある。
- 物の考え方:
  - hierarchy: platform・storage（owner）・endorsement・null。各 seed から primary key を決定的に作る（template が同じなら同じ鍵。template は公開なので、raw の TPM を使える者は誰でも同じ primary を作れる）。
  - object: 鍵は親（storage key）で包まれた blob として外に出る（`Create` の private と public）。使う時に `Load`。中に残すのは persistent handle（PTP の最低 `TPM_PT_HR_PERSISTENT_MIN` = 7、要確認）と NV index（数 KB、書き込みの寿命と rate の制限 `TPM_RC_NV_RATE` がある）。transient の slot の最低は 3。
  - 鍵の属性: `fixedTPM`・`fixedParent`（chip の外へ複製できない）、`sensitiveDataOrigin`（chip が作った）、`userWithAuth`（authValue で使える）、`sign`・`decrypt`、`noDA`（DA を数えない。PIN の守りでは**付けない**）。
  - session: HMAC・policy・trial。password session（`TPM_RS_PW`）は authValue を**平文で** bus に流す。HMAC session でも salt と bind が無いと、1 回の transcript から短い秘密を offline で総当たりできる。守るには **salted session**（salt を SRK か EK の公開鍵で暗号化し、その名前を前もって確かめて interposer を防ぐ）と **parameter の暗号化**（AES-CFB）が要る。前例: systemd-cryptenroll の `--tpm2-with-pin`、Linux の `TCG_TPM2_HMAC`（kernel と TPM の間の HMAC と暗号化、「bus snooping and interposer attacks」への備え。Kconfig の既定は **n**（一次資料で確認）、ECDH・AES-CFB・SHA-256 を kernel に引き込む）。
  - 認可: authValue と policy（`PolicyPCR`・`PolicyAuthValue`・`PolicyNV`・`PolicySecret`・`PolicyOR` など）。
  - **総当たりの防御（DA）**: chip 全体で**一つの数**（`failedTries`）。`maxTries` を越えると lockout、`recoveryTime` 秒ごとに 1 戻る（TPM の Reset が挟まらない間）。**`recoveryTime=0` は DA の無効**（参照の実装 `DA.c`: 「if recovery time is 0, DA logic has been disabled. Clear failed tries immediately」、一次資料で確認）。`lockoutAuth` で解く（`DictionaryAttackLockReset`）。lockoutAuth 自身の失敗は 1 回で `lockoutRecovery` の間止まる（`DA.c`: 0 なら「a reboot is required」）。Windows は TPM 2.0 を「32 回の失敗で lock、10 分ごとに 1 つ忘れる」に設定する（Microsoft Learn の TPM fundamentals）。値は `DictionaryAttackParameters`（lockoutAuth が要る）。**TPM2_Clear の後の既定では lockoutAuth は空**で、raw の TPM に触れる者は誰でも DA を戻せる → 所有と provisioning（§3.0 の S8）が無いと短い PIN は守れない。
  - **電源**: `TPM2_Shutdown(STATE)`（S3 の前）・`(CLEAR)`（停止・reboot の前）を送らずに電源が切れると次の起動が disorderly になり、DA が無効でなければ `failedTries` が 1 増える（参照の実装 `DA.c` の `DAStartup`、一次資料で確認）。context（session・object）は TPM の Reset で無効、resume の後は resource manager が扱う（`TPM_PT_CONTEXT_GAP_MAX`）。
  - 測定（PCR、`PCR_Extend`・`PCR_Read`、measured boot）、attestation（`Quote`・`Certify`、EK の証明書: PTT・AMD fTPM では NV に無く製造者の server から取ることがある、要確認）。
  - 暗号: RSA 2048、ECC P-256（多くは P-384）、SHA-1・SHA-256、HMAC、AES。ECDSA・`ECDH_ZGen`・`GetRandom`。
- 性能: 遅い（dTPM で P-256 の署名が数十〜数百 ms のことがある、chip による、要確認）。一度に 1 command。AMD fTPM は乱数の読み出しで system が stutter する問題があり、Linux は AMD fTPM（Pluton を除く）の hwrng を止めている（`tpm_crb.c` の AMD の quirk、一次資料で確認）。
- **5330（実機）**: Dell の Latitude 5330 の資料は discrete の **ST33（STMicroelectronics、SPI、TPM 2.0）** を載せる（一次資料: Dell の spec sheet。機種の構成で Nuvoton もありうる）。そうなら OS から見えるのは FIFO（TIS）で、第 1 版の「CRB（PTT）」は誤り。**実機の ACPI `TPM2` table の start method を読んで確かめる**（§6 の 1）。dTPM なら bus（SPI）の盗聴が現実の脅威で、salted session が必須。

### 1.2 Microsoft Pluton

- CPU の package の中の security processor（AMD Ryzen 6000 以降、Qualcomm の一部、Intel の一部の新しい世代、要確認）。OS には TPM 2.0 として見える。ACPI の start method は Pluton 専用（`COMMAND_BUFFER_WITH_PLUTON`、doorbell の start と reply の address。Linux の `tpm_crb` の対応は 6.3 の頃、`_CRS` の無い機種もある、要確認）。`MSFT0200` という別の device もある（要確認）。
- → 「PTT・fTPM・Pluton は同じ CRB」は誤り（第 1 版）。start method ごとに driver の手が要る。Intel PTT・AMD fTPM の一部は ACPI start（`_DSM` を評価）で、AMD fTPM は command と response の buffer が別の領域になる quirk がある。

### 1.3 Apple Secure Enclave（SEP）

- SoC の中の独立した coprocessor。公開の資料は Apple Platform Security guide。
- app から使える鍵は SEP の中で作られ外に出ない。長く NIST P-256 だけ（ECDSA・ECDH、CryptoKit の `SecureEnclave.P256`）。新しい版で ML-KEM・ML-DSA が加わった可能性（要確認、確かさ低）。鍵は SEP の UID の鍵で包まれた blob として app に返る。
- 鍵ごとの access control（生体・端末の passcode・この端末だけ）を SEP が強制。**app が選んだ秘密を鍵ごとに数える仕組みは無い**（passcode の総当たりの防御は端末の lock の秘密だけ。`.applicationPassword` が回数を数えるかは要確認）。
- raw の command の口は無い。OS の API は操作の水準。attestation は App Attest。

### 1.4 Google Titan M・M2、OpenTitan、ChromeOS の GSC

- Titan M・M2（Pixel）: StrongBox（KeyMint の security level）、Weaver（回数の制限つきの秘密の保管、lock screen 用）、verified boot の状態。（細部は要確認）
- ChromeOS: Cr50・Ti50（Google Security Chip、TPM 2.0 と独自の vendor の command）、userland の trunksd（TPM の command の仲介）・tpm_manager（所有と provisioning）・chaps（PKCS#11）・**PinWeaver**（短い PIN を chip の NV の数で鍵ごとに守る仕組み）。
- OpenTitan: open source の root of trust の silicon（Earl Grey など）。OS の口は統合の仕方による（要確認）。
- 教訓: **鍵ごとの回数の制限**（PinWeaver・Weaver）は、chip 全体で一つの数の TPM の DA の弱点（§3 の M3: 一人の誤りが全員を止める）への答え。TPM でも NV counter と `PolicyNV` で近い物を作れる（要確認、NV の書き込みの寿命）。

### 1.5 Arm TrustZone の TEE（GlobalPlatform、OP-TEE）

- normal world と secure world の TEE OS（OP-TEE）。trusted application（TA）が仕事をする。
- GlobalPlatform の TEE Client API: context・TA の UUID で session（login の方式で client の身元を渡す）・`InvokeCommand`（4 つの parameter）・共有 memory。
- Linux: `/dev/tee0`（client）と `/dev/teepriv0`（tee-supplicant）。kernel は transport と共有 memory だけ、意味は TA ごと。`TEE_IOCTL_LOGIN_USER` などで kernel が uid から client の身元を作る（要確認）。`tpm_ftpm_tee` は TEE の TA（firmware TPM）を TPM として OS に見せる層。
- 教訓: 汎用の transport は柔軟だが、意味は TA ごとで、利用者の側に TA ごとの client が要る。

### 1.6 Android Keystore・KeyMint

- app → keystore2（system の daemon、blob の保管と利用者の分離）→ KeyMint の HAL → TEE の TA か StrongBox。
- 操作: `generateKey`（attestation は `ATTESTATION_CHALLENGE` などの parameter で generateKey の中で求める）・`importKey`・`importWrappedKey`・`begin`/`update`/`finish`・`deleteKey`・`upgradeKey`。
- 鍵の性質（authorization tag）を作る時に固定し TEE が強制: 用途、algorithm、利用者の認証への結び付け（`USER_SECURE_ID`・有効時間）、unlock の時だけ、最大の使用回数、rollback resistance、OS の版。**app の鍵は「OS の認証の後に使える」という結び付けで守られ、鍵ごとの秘密の回数ではない**（lock screen は Gatekeeper・Weaver）。
- 教訓: 操作の水準、性質の固定、blob は OS が保管、**分離は OS の daemon（userland の特権の program）**。

### 1.7 スマートカード・secure element（ISO 7816、PIV、OpenPGP）

- ISO 7816-4 の APDU。zedBSD は `/dev/smartcardN`（ws161-p003）を持つ。
- PIV（NIST SP 800-73）: slot 9A・9C・9D・9E と退役の slot、PIN と回数、PUK、管理の鍵、`GENERAL AUTHENTICATE`、card の上での鍵の生成。鍵の取り込み・`GET CHALLENGE` は PIV の規格ではなく YubiKey の拡張・ISO 7816 の物。鍵は slot に住む（blob は外に出ない）。
- OpenPGP card: 3 つの鍵、PW1・PW3 の PIN と回数。
- host の口は PC/SC（APDU）とその上の PKCS#11（操作の水準）。PIN の回数は card が鍵（card）ごとに持つ。

### 1.8 OS の口と前例の比較

| OS・前例 | 口 | 水準 | 分離・方針 | 良い所 | 悪い所 |
| --- | --- | --- | --- | --- | --- |
| Linux `/dev/tpm0`・`/dev/tpmrm0` | command の byte 列（tpmrm は kernel の resource manager で open ごとに仮想化） | raw | node の権限（`tss` group）だけ、選別なし | 薄い kernel、全機能 | 開けた者が DA の lockout・NV の枯渇を起こせる、意味は全て userland、TPM 専用 |
| Linux の trusted・encrypted keys（keyctl） | kernel の key の型、backend は TPM・TEE・CAAM | 操作（封印した対称鍵） | key ring の権限 | **複数の backend を一つの操作の口に**（案 C の先例）、blob は userland が持つ | 範囲は対称鍵の封印だけ、TPM の marshaling と session が kernel に入った |
| Linux `/dev/tee0` | TA の呼び出し | TA ごと | login の方式 | 汎用 | 意味は TA ごと |
| FreeBSD `tpm(4)` | `/dev/tpm0`、resource manager は userland（port の security/tpm2-abrmd が /dev/tpm0 を仲介） | raw | node の権限 | 薄い | Linux の tpm0 と同じ |
| OpenBSD `tpm(4)` | userland の口を出さず、suspend の状態の保存だけ（要確認） | — | — | 電源の扱いの前例 | 機能を出さない |
| Windows TBS・Platform Crypto Provider・Windows Hello の PIN | TBS（service の resource manager、権限の無い呼び手に command の block list）、NCrypt の provider（操作の水準）、Hello は **鍵を TPM が持ち、PIN の試行を TPM の DA で守る**（32 回で lock、10 分ごとに 1 つ忘れる） | 両方 | TBS の block list、provider が利用者ごと | 普通の app は操作の水準、Hello の PIN は passkey の chip の方式の直接の前例 | TPM の lockout の既知の問題（DA の共有） |
| systemd-cryptenroll `--tpm2-with-pin` | userland の program が TPM に直に（salted session、PCR の policy） | userland で意味 | — | session の保護の前例 | — |
| macOS・iOS（SEP） | Security framework・CryptoKit | 操作 | SEP が鍵ごとの access control | 単純 | 機能が狭い |
| Android keystore2・KeyMint | daemon と HAL | 操作（tag） | keystore2 が uid ごと、TEE が tag | 汎用（TEE・SE を同じ口） | 段が多い |
| ChromeOS trunksd・tpm_manager・chaps・PinWeaver | userland の daemon が TPM を仲介 | 操作 | daemon | 鍵ごとの回数（PinWeaver）、所有の管理 | 独自の chip の command に依る所 |

## 2. 共通の操作の表

○ 有る、△ 一部・条件つき、× 無い。

| 操作 | TPM 2.0 | Pluton（TPM として） | SEP | Titan M（StrongBox） | TEE（KeyMint の TA） | PIV card |
| --- | --- | --- | --- | --- | --- | --- |
| 乱数 | ○（AMD fTPM は遅い・stutter） | ○ | △ | ○ | ○ | △（ISO 7816 の `GET CHALLENGE`、PIV の規格には無い） |
| 鍵を chip で作る | ○ | ○ | ○（P-256、PQC は要確認） | ○ | ○ | ○ |
| 鍵の取り込み | ○ | ○ | × | ○（wrapped） | ○ | △（YubiKey の拡張） |
| 鍵の住み処 | 外（blob）＋少しの persistent | 同じ | 外（blob） | 外（blob） | 外（blob） | **中（slot）** |
| ECDSA P-256 | ○ | ○ | ○ | ○ | ○ | ○ |
| ECDH P-256 | ○ | ○ | ○ | ○ | ○ | ○ |
| RSA | ○ | ○ | × | ○ | ○ | ○ |
| 封印 | ○（policy と Unseal） | ○ | △ | △ | △ | × |
| 測定（PCR） | ○ | ○ | × | △ | △ | × |
| **鍵ごと**の、利用者が選んだ秘密の回数 | △（authValue は DA に数えるが、数は chip 全体で一つ） | △（同じ） | × | ×（Weaver は lock screen 専用） | ×（Gatekeeper は lock screen 専用） | ○（card の PIN） |
| 総当たりの防御の数の単位 | **chip 全体で一つ** | 同じ | 端末の passcode | 秘密ごと（Weaver） | 端末 | card ごと |
| 「OS の認証の後」への結び付け | △（policy で近い物） | 同じ | ○ | ○ | ○ | × |
| attestation | ○（Quote・Certify） | ○ | △（App Attest） | ○ | ○ | △（YubiKey の拡張） |
| 一度に使える数 | 少（transient・session） | 同じ | 多 | 少 | 中 | 1（排他） |
| 電源の扱いが要る | ○（Shutdown、context） | ○ | — | — | △ | △（card の電源） |

共通の芯（全てで○）: chip の中で作る P-256 の鍵、その鍵の ECDSA と ECDH。乱数・blob・総当たりの防御は「全てで○」ではない（第 1 版の誤り）。短い秘密を守る仕組みは chip ごとに単位が違い（TPM は chip 全体で一つ、card は card ごと、SEP・KeyMint は端末の lock だけ）、汎用の口では「鍵に PIN を付ける」と「OS の認証に結ぶ」の両方を考える。

## 3. zedBSD の /dev/securityN の案

### 3.0 前提と脅威の模型

- 原則（docs/architecture/security.md）: desktop は権限を持たない、特権の program は小さく一つの仕事、要求は固定の形。
- 最初の利用者は passkey（root、短命、sessiond が起動、5 秒の期限: security.md の request の節）。次の候補: disk の暗号の鍵の封印、web の passkey（利用者の program）、乱数の追加の源。
- **脅威の模型（判断 S10）**: chip の方式が何から守るか。
  - 守れる: login の画面での PIN の当て推量（sessiond の 5 回と chip の DA）、`/etc/passkey` の blob を盗んだ者の offline の総当たり（blob は chip でしか使えず、PIN の試行は chip が数える。ただし §1.1 の所有と session の保護が前提）、bus の盗聴（salted session と parameter の暗号化が前提）。
  - 守れない（disk の暗号が無い間）: 別の OS を起動する攻撃者は `/etc/shadow`・`/etc/passkey` を書き換えて入れる。この時 chip が守るのは「PIN そのもの」（他所で使い回された PIN の価値）だけ。root も PIN を守る相手か（root は lockoutAuth を持つか）を決める（S8）。
  - 前提: TPM に触れるのは zedBSD の kernel（とその許した特権の program）だけ。別の OS（dual boot の Windows が lockoutAuth・ownerAuth を持つ）と chip を共有する機械では、zedBSD は所有を取れないか、Windows Hello と DA の数を共有する（S8）。

### 3.1 案 A: 種類ごとの command をそのまま運ぶ（Linux の tpmrm 型を全ての chip に）

- `/dev/securityN` に `GET_INFO`（種類・限界）と `TRANSMIT`（種類の command の byte 列）、TPM は kernel の resource manager。意味は userland の library。
- 良い: kernel が薄い、全機能。
- 悪い: 名前が汎用なだけで中身は ad hoc な `/dev/tpm0`。開けた者は DA の lockout・NV の枯渇を起こせる。権限の境目は node の mode だけ。

### 3.2 案 B: 操作の水準を kernel に（KeyMint・SEP 型を kernel に）

- ioctl が共通の操作（`RANDOM`・`KEY_CREATE`（性質の一覧）→ 不透明な blob・`KEY_SIGN`・`KEY_AGREE`・`SEAL`・`UNSEAL`・`MEASURE`・`ATTEST`）。chip の driver が自分の command に写す。
- kernel が持つ物（TPM）: 約 20 の command の marshaling（Startup・Shutdown・GetCapability・GetRandom・CreatePrimary・Create・Load・FlushContext・ContextSave・ContextLoad・StartAuthSession・PolicyAuthValue・Sign・ECDH_ZGen・Unseal・ReadPublic・DA の 2 つ・HierarchyChangeAuth など）、**session の暗号**（salt の P-256 ECDH か RSA-OAEP、KDFa（HMAC-SHA256）、AES-CFB、cpHash・rpHash）、resource manager、電源。今の kernel の暗号は `random-crypto.c` の ChaCha20・BLAKE2s と wlan の物だけで、上は新しく要る。
- 利用者の分離: uid の確かめは TPM に送る前に kernel がする（TPM の authValue に uid を混ぜると、他人の blob を出すだけで chip 全体の DA を進められるので**しない**）。blob の zedBSD の包み（uid・性質）は kernel の MAC で守るが、その鍵を再起動をまたいで持つ所が要る（disk は offline の攻撃者に読める、primary からの派生は raw の TPM を使える者に作り直せる）→ 「TPM に触れるのは zedBSD の kernel だけ」の前提でしか成り立たない。
- 良い: 一つの口、誤用しにくい、利用者に開ける。
- 悪い: kernel の攻撃面（利用者が渡す blob と TPM の構造の解析）。共通の芯に無い機能は出せない。

### 3.3 案 C: 操作の水準の口と root の狭い管理の口を kernel の中で分ける（第 1 版の推し）

- 案 B の普通の口（利用者に開ける形）と、root だけの `SECURITY_ADMIN_*`（所有・DA の値・lockout の解除・event log）。raw の command の口は試験の kernel だけ（出荷の build に混ざらない作り: `CONFIG_SECURITY_TEST_RAW` のような試験の config だけの file、ws161 の loopback と同じ型）。
- 案 B の悪い所をそのまま持つ（session の暗号と marshaling が kernel に入る）。lockoutAuth を kernel が持つなら root は PIN を全数で試せる（S8 で決める）。

### 3.4 案 D: kernel は汎用の transport と管理、意味は userland の特権の program（第 2 版で加えた）

- kernel の `/dev/securityN`（mode 0600 root）が持つ物:
  - `GET_INFO`: 種類（TPM2・将来の TEE・SE）、接続（start method・locality）、限界（transient・session・persistent の数）、**chip の同一性**（EK か SRK の名前の hash: `/dev/securityN` の N が起動ごとに変わっても、blob と chip を結べる）。
  - 排他の claim（`/dev/smartcardN` と同じ型: `CLAIM` から最後の close まで、他の open の送信は `EBUSY`）、最後の close で後始末（context の flush）。
  - 送信: その種類の command（TPM では command の byte 列）。kernel は **command の allow-list**（TPM: 上の約 20、`Clear`・`ChangeEPS`・`ChangePPS`・vendor の command などは拒む。管理の command は claim の時の flag で root の管理の program だけ）で選別し、blob の中身は解析しない（長さの枠だけ）。
  - resource manager（一度に一つの claim なので、context の save・load は claim の間だけ、簡単）。
  - 電源: suspend の前に `Shutdown(STATE)`、停止・reboot の前に `Shutdown(CLEAR)`、resume の後の `Startup`、claim の中の context の無効化（送信は `ENXIO` などで呼び手に知らせる）。
- userland（base）: `libsecurity`（操作の水準の共通の API: 鍵の作成・署名・ECDH・封印・乱数・情報。TPM の marshaling と session の暗号（OpenSSL の libcrypto、WS172 p005 で独自の暗号へ）は ここ）と、仕事ごとの小さな root の program（passkey の chip の方式は `/usr/libexec/passkey-chip`、passkey-fido2 と同じ型）。所有と provisioning は別の root の program（`securityctl`）。
- 利用者の program に開くのは後（web の passkey）で、その時は root の小さな daemon が uid ごとの分離をする（keystore2・trunksd 型）か、案 B に進む（S2）。
- 良い: kernel の中に暗号と TPM の構造の解析が入らない（攻撃面が小さい）。原則「特権の program は小さく一つの仕事」に合う（passkey-fido2 と同じ形）。「汎用の口」は `libsecurity` の操作の水準の API と、`/dev/securityN` の共通の枠（情報・claim・電源・同一性）で作る。TEE・SE も同じ枠の種類として足せる。
- 悪い: kernel の口は種類ごとの command を運ぶので、ユーザーの「ad hoc な /dev/tpm0 にしない」をどう満たすかの説明が要る（違い: root だけ、claim、allow-list、電源、同一性、種類の枠。tpm0 との差を §3.6 に）。利用者の program に開く時に daemon が要る。

### 3.5 比べ

| | A | B | C | D |
| --- | --- | --- | --- | --- |
| kernel の新しい code（TPM） | transport・RM・電源 | ＋marshaling 約 20・session の暗号（ECDH・KDFa・AES-CFB）・blob の包みの MAC | B ＋管理の口 | transport・claim・allow-list・RM（claim の中）・電源・同一性 |
| 権限の無い者から届く攻撃面 | node を開けた者に全て | 利用者が渡す blob の解析 | 同じ | 無い（root だけ） |
| DA の lockout の DoS | 開けた者が起こせる | kernel の数の上限で抑えられる | 同じ | root の program だけが送る（passkey の予算の確認、M3） |
| 利用者の分離 | 無い | kernel（前提つき） | 同じ | 後で daemon（S2） |
| 種類を足す | client ごと | driver ごと | 同じ | libsecurity と driver の枠 |
| 原則との合い | 弱い | kernel が大きい | 同じ | 合う |

P1 の推し（第 2 版）: **D**。理由: session の保護（§1.1）が必須で、その暗号と TPM の構造の解析を kernel に入れない方が攻撃面が小さく、zedBSD の特権の program の原則（passkey-fido2）と揃う。「汎用の口」は userland の操作の水準の API と kernel の共通の枠で満たす。利用者の program に開く時に B（または daemon）を改めて判断する。C を選ぶ場合も、blob を kernel で解析しない方針にする。

### 3.6 Linux の /dev/tpm0 と D の違い（ユーザーの「ad hoc にしない」への答え）

- 名前と種類の枠: 種類の情報と chip の同一性を返す汎用の node（TEE・SE も同じ node の型）。
- 権限: root だけ、claim の排他、command の allow-list（危険な command と管理の command を分ける）。
- 電源と後始末を kernel が必ずする（Linux の tpm0 では userland の責任の所がある）。
- 意味は一つの library（`libsecurity`）の操作の水準の API に集め、program は chip の command を直に組まない。

### 3.7 passkey の chip の方式（どの案でも要ること）

- **pin の行と両立させない**: chip の方式を有効にした account では、`pin` の行（SHA-512 crypt）を消す（6 桁の crypt hash は offline で一瞬で解け、chip の守りが無意味になる）。
- 鍵: 利用者ごとに P-256 の鍵を chip で作る（`fixedTPM`・`fixedParent`・`sensitiveDataOrigin`・`userWithAuth`・`sign`、`noDA` を付けない）。authValue は PIN（の派生、KDF は要検討）**だけ**。認証は salted session と parameter の暗号化。
- 認証: passkey-chip が challenge を作り、chip の鍵で署名させ、保存した公開鍵で確かめる（S4 の (a)）。(a) の利点: 能動的な interposer が「成功」を偽れない（(b) の Unseal は応答を偽られうる）。
- **DA の予算**: 試す前に認可の要らない `GetCapability`（`TPM_PT_LOCKOUT_COUNTER`・`TPM_PT_MAX_AUTH_FAIL`）を読み、予備を残す閾値を越えていたら chip の方式を断り（新しい理由、例 `chip-locked`）、sessiond の数を増やさず password へ誘導する。sessiond の account ごとの 5 回だけでは、PIN の account が K あると 5K 回まで間違えられ、chip 全体（将来の disk の暗号も）を止められる。鍵ごとの回数（PinWeaver 型の NV counter と `PolicyNV`）は代わりの案として S12 に。
- **PIN の変更**: `ObjectChangeAuth` を使わない（古い blob と古い PIN が永久に有効のまま）。変更のたびに新しい鍵を作り、公開鍵を差し替える（公開鍵の確認で古い blob は無効）。
- **chip が変わった時**: TPM の clear、fTPM の reset（AMD の BIOS の更新で起きる）、board・CPU の交換。blob に chip の同一性を持たせ、`TPM_RC_INTEGRITY` などを wrong PIN と区別して、新しい理由（例 `chip-changed`）で password へ誘導し、再登録を促す。
- **期限**: passkey の 5 秒（security.md）。遅い dTPM、`TPM_RC_RETRY`・`TESTING`、primary（SRK）を毎回作る時間。SRK は TCG の既定の persistent handle（`0x81000001`）を使うか自分で作って保つか（S8）。sessiond の SIGKILL で command の途中に死んだ時の後始末は kernel の claim の close で。
- **行の形**: 実装（`userland/base/passkey/record.c`）は `<name>:<uid>:<kind>:...`。chip の行は `<name>:<uid>:chip:<provider>:<chip id>:<public key>:<blob>:<label>:<date>` の形を p004b で決める（phase001 の l.81 の古い形は uid・公開鍵・blob が無い）。
- 試験: QEMU は swtpm（`tpm-crb`・`tpm-tis`、host の道具、T1 の環境の依存）。host の試験は libtpms を直に link して marshaling と DA の意味を確かめる。swtpm で見つからない物（実機の start method、bus の盗聴、disorderly の DA の加算、fTPM の quirk）は実機の試験の項目に分ける（5330 の start method の確認から）。

## 4. 電源と依存

- kern: TPM（ACPI の device）の suspend・resume・停止の hook が要る。今の sleep（s2idle、`src/kern/sleep.c`）の device の hook は PCI だけ。`sleep.c`・`shutdown.c` に新しい hook（kern の変更、HAL の API は変えない見込み、他の WS の範囲の可能性 → Q1 が調整）。
- ACPI: ACPI start（`_DSM`）の評価（`src/drivers/acpi` の AML）、`TPM2` table の読み。
- 試験の道具: swtpm・libtpms（host）。
- 乱数: TPM の乱数を kernel の乱数の源に混ぜるか（S13、AMD fTPM の問題）。

## 5. ユーザーの判断の点

| ID | 問い | 選択肢 | P1 の推し |
| --- | --- | --- | --- |
| S1 | 口の水準と意味の置き場 | A・B・C・D（§3） | D |
| S2 | 利用者の program に開くか、いつ | (a) root だけ、(b) 利用者にも（daemon か案 B、uid ごとの分離・rate と優先度（login の passkey が期限を越えないよう）） | 最初は (a)。(b) は web の passkey の時に改めて |
| S3 | 最初に対応する接続 | 5330 の実機の start method（FIFO＝TIS の見込み）と QEMU の swtpm（`tpm-tis`・`tpm-crb`）。ACPI start（PTT・fTPM）・Pluton・arm64（FF-A・SMC）は後 | 5330 の確認の後に決める（FIFO なら TIS を最初に） |
| S4 | passkey の chip の方式の形 | (a) chip の鍵の署名（PIN を authValue）、(b) 封印した秘密（PIN で取り出す） | (a)（interposer が成功を偽れない） |
| S5 | 測定と封印を最初の版に | 入れる・後 | 後（口の枠だけ） |
| S6 | PIV の card を /dev/securityN の種類にするか | する・しない（userland の PKCS#11 型） | しない |
| S7 | blob の保管 | 呼び手・chip | 呼び手 |
| S8 | **所有と provisioning** | lockoutAuth を乱数にして捨てる / recovery code（紙）にする / PCR の policy に結ぶ（`SetPrimaryPolicy`）、DA の値（`maxTries`・`recoveryTime`・`lockoutRecovery`）を zedBSD が決めるか、既に所有された TPM（Windows との dual boot）の扱い（使わない・共有する）、SRK（`0x81000001`）の再利用、root も PIN を守る相手か | lockoutAuth は乱数を root だけが読める所に保ち（root は信じる）、DA の値は zedBSD が設定、既に所有された TPM は chip の方式を出さない（案、要議論） |
| S9 | session の保護と暗号の置き場 | salted session と parameter の暗号化を必須に（userland の libsecurity か kernel か） | 必須、userland（D） |
| S10 | 脅威の模型と disk の暗号 | §3.0 の範囲で良いか、disk の暗号を chip の方式の前提にするか | §3.0 で始め、disk の暗号は別の WS |
| S11 | 電源の hook | kern の sleep・shutdown に TPM の hook（他の WS との調整） | 入れる（p004b の依存） |
| S12 | DA の予算と鍵ごとの回数 | chip 全体の DA の残りを見て予備を残す / PinWeaver 型の鍵ごとの回数を作る | 予算から始め、鍵ごとは後 |
| S13 | TPM の乱数を kernel の乱数に混ぜるか | 混ぜる・混ぜない（AMD fTPM の stutter） | dTPM・PTT は混ぜる、AMD fTPM は混ぜない（要確認） |

## 6. 確認の一覧（ユーザーの判断の前に、Q1 経由で）

1. **未確認**: 5330 の TPM の ACPI `TPM2` table の start method と `_HID`、vendor（`TPM_PT_MANUFACTURER`）。5330 が Linux（chaos）で起動している時に Q1 が `/sys/firmware/acpi/tables/TPM2` を読んで知らせる（2026-10-08 Q1）。今の推定は Dell の資料の ST33 の dTPM（SPI、FIFO）。S3 はこの答えの後。
2. 確認済み（2026-10-08、参照の実装 `DA.c`）: `recoveryTime=0` は DA の無効、disorderly の起動で `failedTries` が 1 増える、`lockoutRecovery=0` は reboot まで。未確認のまま: persistent・transient の最低の数（PTP）。
3. 確認済み: `TCG_TPM2_HMAC` の既定は n（Kconfig）。未確認のまま: `tpm_tis` の locality の扱い、`tpmrm` の細部。
4. FreeBSD: `/dev/tpm0` と userland の tpm2-abrmd（port）。OpenBSD の `tpm(4)` の作り（suspend の保存だけ）は未確認。
5. 確認済み: Windows の DA の値（32 回、10 分ごと、Microsoft Learn）。
6. 未確認: SEP の PQC、Titan M・PinWeaver の公開の資料（判断の点には効かない）。

## 7. 第 1 版のレビュー（design-reviewer）の指摘と対応

| 指摘 | 対応 |
| --- | --- |
| B1 lockoutAuth と DA の値（所有）が無い | §1.1 の DA の項、§3.0、S8 |
| B2 PIN を bus に出さない session の設計 | §1.1 の session の項、§3.2・§3.7、S9 |
| B3 未確認の事実で判断を求めている | 冒頭と §6。5330 は一次資料（Dell）で dTPM（ST33）の見込みと分かり、実機の確認を判断の前に |
| M1 5330 は dTPM（TIS）の見込み | §1.1 の末尾、S3 |
| M2 PTT・fTPM・Pluton は同じ CRB ではない | §1.1 の接続の口（tpm_crb.c で確認）、§1.2、S3 |
| M3 DA は chip 全体で一つ、login の画面から DoS | §2 の行、§3.7 の DA の予算、S12 |
| M4 TPM での uid の分離 | §3.2（uid を authValue に混ぜない、前提の明記） |
| M5 userland の daemon・helper の案との比べ | 案 D（§3.4）と比べの表（§3.5）、推しを D に |
| M6 電源と suspend・resume | §1.1、§3.4、§4、S11 |
| M7 共通の芯の表の誤り | §2 を直した（鍵ごとの秘密の回数、鍵の住み処） |
| M8 passkey の chip の方式の穴（pin の行、ObjectChangeAuth、chip の変更、脅威の模型、(a) の理由、行の形、期限） | §3.0・§3.7 |
| M9 抜けた前例（Windows Hello、systemd-cryptenroll、trusted keys、ChromeOS、tpm_ftpm_tee、TPM 1.2、NetBSD） | §1.1・§1.4・§1.5・§1.8（NetBSD は §6 の 4 で要確認に含めず、OpenBSD を足した） |
| M10 判断の点の抜け | S8〜S13、S2 の条件 |
| M11 Phase と WS の分け方 | phase.md の「次」: 汎用の `/dev/securityN` は別の WS として立てることを Q1 に提案 |
| minor 1〜12 | §1.1（persistent の数、DA の詳細、EK の証明書、NV の rate、鍵の属性）、§1.3（PQC）、§1.6（attestation）、§1.7（PIV）、§3.3（試験の raw の口の build の分け）、§3.4（ioctl の group は `'S'` を避ける: p004b）、§3.4（同一性で N の不安定を吸収）、§3.7（試験の計画） |
