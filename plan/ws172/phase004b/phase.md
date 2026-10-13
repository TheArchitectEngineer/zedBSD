<!-- awesome-plan project=zedbsd record=ws172-p004b -->

# ws172-p004b: `/dev/securityN` の UAPI と TPM 2.0 の driver の設計（設計だけ）

Parent: [WS172](../ws.md)
Status: in-progress（2026-10-13 P2 q960: 設計の第 1 版。design-reviewer の review 待ち（Q1 が回す）。**実装はしない**（ベータ2 は設計まで、実装はベータ3 以降））
Phase disposition: normal
Queue: q960（P2、2026-10-13）。承認: ユーザー 2026-10-13「済みません、ベータ2では設計までです。設計までと言うことなら、推奨でOKです。」→ S1〜S13 は全て [survey](../phase004/survey.md) §5 の推し（survey の末尾に記録）。
依存: [p004](../phase004/phase.md) の survey（第 2 版）とその判断。S3 の前提（5330 の ACPI `TPM2` の start method）は未確認。

## 0. 決まったこと（S1〜S13、2026-10-13 ユーザー「推奨でOK」）と、この設計での写し

| ID | 決定 | この設計で |
| --- | --- | --- |
| S1 | 案 D: kernel は汎用の transport・claim・allow-list・電源・同一性の手掛かり、意味は userland | §2・§3・§4 |
| S2 | 最初は root だけ | node は 0600 root、利用者の program には開かない（§3.1） |
| S3 | 接続は 5330 の確認の後に決める | TIS（FIFO）と CRB の**両方を受ける形**で書き、最初に実装する方は確認の後（§5、§10 の O1） |
| S4 | chip の鍵の署名（PIN を authValue） | passkey-chip（§7） |
| S5 | 測定・封印は後 | 口の枠だけ（`PCR_Extend`・`Unseal` は allow-list に入れない、§4.3） |
| S6 | PIV の card は `/dev/securityN` にしない | `/dev/smartcardN` のまま（§2） |
| S7 | blob は呼び手が保管 | `/etc/passkey` の chip の行（§7.3） |
| S8 | lockoutAuth は乱数で root だけが読める所、DA の値は zedBSD が設定、既に所有された TPM では chip の方式を出さない | securityctl（§6.3） |
| S9 | salted session と parameter の暗号化を必須、userland で | libsecurity（§6.2） |
| S10 | 脅威の模型は survey §3.0、disk の暗号は別 WS | §1 |
| S11 | sleep・shutdown の TPM の hook を入れる | §4.5 |
| S12 | DA の予算から（鍵ごとの回数は後） | passkey-chip（§7.4） |
| S13 | dTPM・PTT の乱数は kernel に混ぜ、AMD fTPM は混ぜない | §4.6 |

## 1. 範囲と範囲の外

- 範囲: (1) UAPI `include/uapi/security.h` と node `/dev/securityN`、(2) kernel の汎用の層（node・claim・allow-list の枠・電源・乱数）と TPM 2.0 の backend（TIS・CRB の transport、kernel が自分で送る最小の command）、(3) userland の `libsecurity`（TPM の marshaling・session の暗号）と root の小さな program（`securityctl`・`passkey-chip`）の口と責務、(4) passkey の chip の方式（`/etc/passkey` の行、手順、理由の語）、(5) 試験と実装の Phase の分け方。
- 範囲の外: 実装（ベータ3 以降）、TPM 1.2、Pluton・ARM SMC・FF-A の start method（後で backend の transport を足す）、TEE・SE の種類（枠だけ予約）、測定（PCR・event log の公開）と封印（S5）、利用者の program への公開（S2: web の passkey の時に daemon か案 B を改めて判断）、disk の暗号（S10）、PIN の鍵ごとの回数（PinWeaver 型、S12）。
- 脅威の模型は survey §3.0 のまま: login の画面の当て推量（sessiond の 5 回と chip の DA）、`/etc/passkey` の blob を盗んだ者の offline の総当たり、bus の盗聴・interposer（salted session と parameter の暗号化）から守る。別の OS を起動する攻撃者からは守れない（disk の暗号が無い間）。root は信じる（lockoutAuth を root が読める、S8）。

## 2. 全体の構成

```
 sessiond ──spawn──▶ /sbin/passkey ──spawn──▶ /usr/libexec/passkey-chip (root, 短命)
                                                     │  libsecurity（TPM の marshaling、salted session、AES-CFB、KDFa）
 root の管理者 ──▶ /sbin/securityctl ──────────────┤
                                                     ▼
                                     /dev/securityN（0600 root、ioctl、claim は 1 つ）
 kernel ─────────────────────────────────────────────────────────────────────────────
   src/drivers/generic/security.c   node・claim・allow-list の照合・同時実行・電源と乱数の呼び出し（種類に依らない）
   src/drivers/tpm/tpm2.c           TPM 2.0 の種類: command の header の検査、kernel が自分で送る最小の command
   src/drivers/tpm/tpm-tis.c        transport: FIFO（TIS、PTP の FIFO interface、locality 0）
   src/drivers/tpm/tpm-crb.c        transport: CRB（start method 7・8・2）
   src/drivers/tpm/tpm-acpi.c       発見: ACPI の TPM2 table と device（MSFT0101）、_CRS、_DSM（ACPI start）
   src/kern/sleep.c・shutdown.c     弱い参照の hook（drv_security_suspend・_resume・_shutdown）
```

- 汎用の層（`security.c`）は「種類」の vtable（`struct drv_security_kind`: `transmit`・`suspend`・`resume`・`shutdown`・`release_claim`・`check_command`・`identity`）だけを知る。TPM 2.0 は最初の種類。TEE・SE は後で同じ枠の種類として足す（S6 の PIV は足さない）。
- kernel に入れない物（S1・S9）: TPM の構造の解析（command の header の 10 byte と、kernel が自分で送る command の応答を除く）、session の暗号、blob の包み、uid の分離。
- HAL の API（`include/hal/hal.h`）は変えない。MMIO は既存の driver と同じ device mapping と accessor（承認済みの MMIO accessor）を使う。ACPI start の `_DSM` は既存の AML の評価器（`src/drivers/acpi/aml-*.c`）を使う。

## 3. UAPI `include/uapi/security.h`

### 3.1 node と権限

- `/dev/securityN`（N は 0 から、発見の順）。mode 0600、owner root。sessiond の seat の一覧には**入れない**（利用者に渡さない、S2）。
- open は何人でも（root だけ）。`SECURITY_GET_INFO` は claim なしで読める。送信は claim を持つ open の file だけ。
- ioctl の group は `'K'`（使われていない文字。`'S'` は CCID が使う: survey の minor の指摘）。番号と構造は zedBSD 独自。

### 3.2 定義（案）

```c
#define KERN_SECURITY_IOC_GROUP	'K'

/* 種類と接続 */
#define SECURITY_KIND_TPM2		1U	/* 将来: 2 TEE、3 secure element（予約） */
#define SECURITY_LINK_TIS		1U	/* FIFO（PTP の FIFO、ACPI start method 6） */
#define SECURITY_LINK_CRB		2U	/* CRB（7） */
#define SECURITY_LINK_CRB_ACPI_START	3U	/* CRB＋ACPI start（8）・ACPI start だけ（2） */

/* security_info.flags */
#define SECURITY_INFO_CLAIMED		0x0001U	/* この open が claim を持つ */
#define SECURITY_INFO_BUSY		0x0002U	/* 他の open が claim を持つ */
#define SECURITY_INFO_FIRMWARE_TPM	0x0004U	/* firmware の TPM（PTT・fTPM、CRB の時の推定） */
#define SECURITY_INFO_RANDOM_MIXED	0x0008U	/* chip の乱数を kernel の乱数に混ぜている（S13） */
#define SECURITY_INFO_DISORDERLY	0x0010U	/* 予約: 前の電源断が disorderly だった印（§4.5、O4。今は立てない） */

#define SECURITY_TEXT_MAX		64U
#define SECURITY_COMMAND_MAX		4096U	/* TPM の TPM_PT_MAX_COMMAND_SIZE の上限の目安（実際の値は info の max_command） */

struct security_info {
	uint32_t kind;			/* SECURITY_KIND_* */
	uint32_t link;			/* SECURITY_LINK_* */
	uint32_t flags;			/* SECURITY_INFO_* */
	uint32_t generation;		/* chip の context が無効になるたびに増える（resume・reset、§4.5） */
	uint32_t max_command;		/* 送れる command の最大の長さ（TPM_PT_MAX_COMMAND_SIZE） */
	uint32_t max_response;		/* 受ける応答の最大（TPM_PT_MAX_RESPONSE_SIZE） */
	uint32_t manufacturer;		/* TPM_PT_MANUFACTURER（4 文字の ASCII、例 "STM "・"INTC"・"AMD "） */
	uint32_t firmware[2];		/* TPM_PT_FIRMWARE_VERSION_1・_2 */
	uint32_t spec_revision;		/* TPM_PT_REVISION */
	uint32_t reserved[6];
	char location[SECURITY_TEXT_MAX];	/* 起動をまたいで同じ位置の名前（例 "acpi:\_SB.TPM_:MSFT0101:0"） */
	char name[SECURITY_TEXT_MAX];		/* 表示の名前（例 "TPM 2.0 STM (TIS)"） */
};

#define SECURITY_CLAIM_ADMIN		0x0001U	/* 管理の command も許す（securityctl だけが使う、§4.3） */

struct security_claim {
	uint32_t flags;			/* SECURITY_CLAIM_* */
	uint32_t generation;		/* 返す: claim の時の generation */
	uint32_t reserved[2];
};

/*
 * 1 つの command と応答。pointer は 64 bit（どの program も同じ構造）。
 * kernel が埋める: response_size と、chip の応答が来なかった時の errno（ioctl の戻り）。
 * TPM の応答の code（TPM_RC）は応答の byte 列の中にあり、kernel は解釈しない。
 */
struct security_transmit {
	uint64_t command;
	uint64_t response;
	uint32_t command_size;
	uint32_t response_capacity;
	uint32_t response_size;
	uint32_t timeout_ms;		/* 0 は種類の既定（TPM: command の種類ごとの PTP の時間、上限 120000） */
	uint32_t reserved[4];
};

#define SECURITY_GET_INFO	_IOR(KERN_SECURITY_IOC_GROUP, 0, struct security_info)
#define SECURITY_CLAIM		_IOWR(KERN_SECURITY_IOC_GROUP, 1, struct security_claim)
#define SECURITY_RELEASE	_IO(KERN_SECURITY_IOC_GROUP, 2)
#define SECURITY_TRANSMIT	_IOWR(KERN_SECURITY_IOC_GROUP, 3, struct security_transmit)
#define SECURITY_CANCEL		_IO(KERN_SECURITY_IOC_GROUP, 4)	/* 送信中の command の取り消し（TIS の commandCancel・CRB の CRB_CTRL_CANCEL） */
```

### 3.3 意味と errno

| 呼び | 意味 | errno |
| --- | --- | --- |
| `SECURITY_GET_INFO` | 上の構造。claim は要らない | — |
| `SECURITY_CLAIM` | この open file に排他の claim を与える。ADMIN は管理の command も許す（root の中の衛生の境目で、権限の境目ではない） | `EBUSY`（他が持つ。待たない、呼び手が 5 秒の期限の中で数回やり直す）、`ENODEV`（chip が使えない状態: self test の失敗・failure mode） |
| `SECURITY_RELEASE` | claim を返す。kernel が後始末（§4.4）。最後の close も同じ | `EINVAL`（claim を持たない） |
| `SECURITY_TRANSMIT` | command を 1 つ送り、応答を待つ（一度に 1 つ、chip の性質） | `EPERM`（claim なし）、`EACCES`（allow-list に無い command）、`EINVAL`（header の size と command_size が違う、短すぎる）、`E2BIG`（max_command を越える）、`ENOSPC`（応答が response_capacity に入らない。応答は捨てる）、`ETIMEDOUT`（chip が期限内に答えない。kernel は取り消しを試み、chip を再初期化の候補にする）、`ESTALE`（claim の後に generation が変わった: resume・reset で chip の context が無効。呼び手は claim を返してやり直す）、`EIO`（transport の失敗） |
| `SECURITY_CANCEL` | 別の thread から、送信中の command を取り消す | `EINVAL`（送信中でない） |

- `read`・`write`・`poll` は持たない（`/dev/tpm0` の write/read の型にしない。ioctl の 1 往復で、長さと期限と errno がはっきりする）。
- 応答の中の `TPM_RC_RETRY`・`TPM_RC_TESTING`・`TPM_RC_YIELDED` のやり直しは呼び手（libsecurity）の仕事。kernel は自分の送る command（§4.2）でだけやり直す。

## 4. kernel

### 4.1 発見と attach（`tpm-acpi.c`）

- ACPI の `TPM2` table（rev 4。`control_address`・`start_method`、start method ごとの parameter、log area）を `drv_acpi_firmware_find` で読む。device は ACPI の namespace の `MSFT0101`（と `MSFT0200` の Pluton は後）を探し、`_CRS` の memory の範囲を取る（`acpi-resource.c`）。
- start method で transport を選ぶ:

| start method（Linux の `actbl3.h` で確認） | transport | 最初の版 |
| --- | --- | --- |
| 6 `MEMORY_MAPPED`（FIFO・TIS） | `tpm-tis.c`。base は `_CRS`（無ければ PC の既定 `0xFED40000`）、locality 0 | ○（5330 が dTPM・SPI なら、§10 の O1） |
| 7 `COMMAND_BUFFER` | `tpm-crb.c`（control area は `control_address`、command・response の buffer の address と大きさは control area から） | ○ |
| 8 `COMMAND_BUFFER_WITH_START_METHOD`・2 `START_METHOD` | `tpm-crb.c` ＋ 各 command の start で `_DSM`（TCG の UUID、function 2 の start）を評価 | ○（Intel PTT・AMD fTPM の一部） |
| 11 ARM SMC・13 Pluton・15 ARM FF-A | 無し（attach しない、kernel の log に start method の値を出す） | × |

- AMD fTPM の quirk（command と response の buffer が control area から離れた別の領域）は Linux の `tpm_crb.c` と同じく、control area の address を信じて別々に map する。
- **TIS と CRB を両方受ける**（S3 の未確認のため）: 汎用の層と TPM の種類は transport の vtable（`send`・`receive`・`cancel`・`ready`・`idle`・`locality_request`・`locality_release`）だけを呼ぶ。最初の実装の Phase で両方を書く（swtpm は `tpm-tis` と `tpm-crb` の両方の QEMU device を持ち、どちらも試せる）。どちらを実機の関門にするかは 5330 の確認の後（§10 の O1）。
- attach の手順: transport の初期化（TIS: locality 0 の要求、`STS.commandReady`、burstCount。CRB: `CRB_LOC_CTRL` の requestAccess、`CRB_CTRL_REQ` の goIdle・cmdReady）→ `TPM2_Startup(CLEAR)`（`TPM_RC_INITIALIZE` は firmware が済ませた印で成功とみなす）→ `TPM2_GetTestResult`（`TPM_RC_TESTING` なら `TPM2_SelfTest(NO)` を待つ、上限 2 秒）→ `TPM2_GetCapability(TPM_CAP_TPM_PROPERTIES)` で `MANUFACTURER`・`FIRMWARE_VERSION_*`・`REVISION`・`MAX_COMMAND_SIZE`・`MAX_RESPONSE_SIZE` → node を登録。失敗は node を作らず kernel の log に段と TPM_RC。
- TPM 1.2（`TPM2` table が無く `TCPA` だけ、または TPM2 の command に 1.2 の応答）は attach しない（survey §1.1）。

### 4.2 kernel が自分で送る command（`tpm2.c`）

kernel が組み立て・解析する TPM の構造はこれだけ（全て password session も HMAC session も使わない、authorization の無い command）:

| command | 使う所 | 解析する応答 |
| --- | --- | --- |
| `Startup(CLEAR)`・`Startup(STATE)` | attach・resume | header の RC だけ |
| `Shutdown(CLEAR)`・`Shutdown(STATE)` | shutdown・reboot、suspend | RC だけ |
| `SelfTest(NO)`・`GetTestResult` | attach | RC と testResult |
| `GetCapability(TPM_CAP_TPM_PROPERTIES)` | attach（§4.1） | `TPML_TAGGED_TPM_PROPERTY` |
| `GetCapability(TPM_CAP_HANDLES, 0x80000000・0x02000000・0x03000000)` | claim の後始末（§4.4） | `TPML_HANDLE` |
| `FlushContext(handle)` | 同上 | RC だけ |
| `GetRandom(32)` | 乱数（§4.6） | `TPM2B_DIGEST` |

解析は固定の長さの枠と上限（list の数 ≤ 128、応答 ≤ max_response）で行い、壊れた応答は `EIO` と log（chip を信じきらない）。

### 4.3 allow-list（`tpm2.c` の `check_command`）

- kernel は呼び手の command の header（`tag` 2・`commandSize` 4・`commandCode` 4）だけを見る: `tag` は `TPM_ST_NO_SESSIONS` か `TPM_ST_SESSIONS`、`commandSize` が `command_size` と同じ、`commandCode` が表にあること。handle・parameter・session は解析しない。vendor の command（`commandCode` の bit 29、`TPM_CC_V`）は全て拒む。
- 普通の claim で許す（passkey-chip と libsecurity の情報の読み）:

| command | 用途 |
| --- | --- |
| `GetCapability`・`GetRandom`・`ReadClock`・`TestParms` | 情報・DA の残りの読み（`TPM_PT_LOCKOUT_COUNTER` ほか）・乱数 |
| `StartAuthSession`・`PolicyAuthValue`・`PolicyCommandCode`・`PolicyGetDigest`・`PolicyRestart` | salted session と policy |
| `CreatePrimary`（storage の hierarchy の SRK）・`ReadPublic`・`Create`・`Load`・`LoadExternal`（公開鍵だけ） | 鍵の作成と読み込み |
| `Sign`・`VerifySignature`・`ECDH_ZGen`・`ECDH_KeyGen` | 署名と鍵の合意 |
| `FlushContext`・`ContextSave`・`ContextLoad` | 呼び手の後始末と session の持ち越し（claim の間） |

- ADMIN の claim でだけ許す（securityctl の所有と provisioning、§6.3）: `HierarchyChangeAuth`・`DictionaryAttackParameters`・`DictionaryAttackLockReset`・`EvictControl`（SRK を `0x81000001` に置く）・`HierarchyControl`（endorsement・storage の有効の確かめ、無効にはしない）・`SetPrimaryPolicy`（後、S8 の PCR の policy は使わない）。
- 常に拒む: `Clear`・`ClearControl`・`ChangeEPS`・`ChangePPS`・`FieldUpgradeStart`・`FieldUpgradeData`・`FirmwareRead`・`NV_*`（S5・S12 の鍵ごとの回数まで。NV の枯渇と書き込みの寿命を守る）・`PCR_Extend`・`PCR_Event`・`PCR_Reset`・`PCR_Allocate`（S5 まで。測定は kernel だけが行う形にする予定）・`Unseal`（S5 まで）・`Startup`・`Shutdown`（kernel だけ）・`SetCommandCodeAuditStatus`・vendor。
- 表は `tpm2.c` の定数の表一つ（拒むのが既定）。足す時は Phase の記録で理由を書く。

### 4.4 claim と後始末

- claim は一度に 1 つ（`/dev/smartcardN` の型。mutex の下で claim の持ち主の `struct file *`）。待ちの queue は作らない（`EBUSY` を返す。passkey の 5 秒の中で libsecurity が 50 ms おきに数回やり直す）。
- claim の返却（`RELEASE`・最後の close・process の死）で kernel が後始末: `GetCapability(TPM_CAP_HANDLES)` で transient の object（`0x80xxxxxx`）と loaded・saved の session（`0x02xxxxxx`・`0x03xxxxxx`）を列挙し、全てを `FlushContext`（失敗は log、次の claim の前にもう一度）。これで resource manager が要らない（一度に 1 つの claim、claim の間は呼び手が全てを持つ）。sessiond が passkey を SIGKILL した時も同じ。
- 送信中に claim の持ち主が死んだ時: 応答を待ち切ってから（期限まで）後始末。`CANCEL` を先に送る。

### 4.5 電源（S11）

- suspend（`src/kern/sleep.c` の device の段の前に、弱い参照の `drv_security_suspend`）: claim があっても送信の終わりを待ち（上限 2 秒）、`Shutdown(STATE)`。TIS は locality を返し、CRB は goIdle。
- resume（device の段の後に `drv_security_resume`）: `Startup(STATE)`、失敗（`TPM_RC_VALUE` など、firmware が CLEAR した）なら `Startup(CLEAR)`。どちらでも transient の object と session は無効なので `generation` を 1 増やす（claim の次の送信は `ESTALE`）。
- shutdown・reboot（`src/kern/shutdown.c` の `system_shutdown_prepare` の device の段の前に `drv_security_shutdown`）: `Shutdown(CLEAR)`。これが無いと次の起動が disorderly になり、DA が有効なら `failedTries` が 1 増える（survey §1.1、参照の実装 `DA.c`）。kernel の panic・電源の喪失では送れない（既知の制限、DA の予算に 1 の余裕を持つ: §7.4）。
- disorderly の印（`SECURITY_INFO_DISORDERLY`）: TPM は前の起動が disorderly だったかを直接は言わない。kernel は判断しない（flag は予約で、今は立てない）。securityctl・passkey-chip は DA の予算（§7.4）に 1 の余裕を持たせて吸収する。推定の方式（`TPM_PT_LOCKOUT_COUNTER` の起動ごとの増え方の記録など）は §10 の O4。
- `sleep.c`・`shutdown.c` への弱い参照の追加は kern の変更（HAL の API は変えない）。他の WS（電源）の範囲と重なるので、実装の Phase の前に Q1 が調整する。

### 4.6 乱数（S13）

- attach の後と resume の後に `GetRandom(32)` を 1 回読み `kern_random_add` に混ぜる（`random.c` の pool。混ぜるだけで、chip の乱数だけを信じない）。
- AMD の fTPM（`manufacturer` が `"AMD "` かつ CRB）は混ぜない（Linux の `tpm_crb.c` の AMD の quirk: 読みで system が stutter）。Pluton は attach しない（§4.1）。`SECURITY_INFO_RANDOM_MIXED` で混ぜたかを示す。

## 5. TPM と接続の違いの扱い（S3）

- 5330 は Dell の資料で discrete の ST33（SPI）、OS から FIFO（TIS、start method 6）の見込み（survey §1.1）。PTT の機種（Intel の firmware TPM）は CRB＋ACPI start（8）。どちらでも上の口と allow-list は同じで、違いは transport の vtable の中だけ。
- 期限: TIS・CRB とも PTP の timeout（A・B・C・D: 750 ms・2 s・200 ms・30 s の目安）を command の種類で使い分ける（`CreatePrimary`・`Create` は長い、`GetRandom` は短い）。`timeout_ms` が 0 なら種類の既定、指定はそれを越えない。
- bus の盗聴: dTPM は SPI の盗聴・interposer が現実の脅威。kernel は対策しない（S9: session の保護は userland で必須）。

## 6. userland

### 6.1 置き場と依存

- `userland/base/libsecurity/`（Zlib、base）: 暗号は `libpasskey` と同じく `crypto.h` の口の後ろ（当面 OpenSSL の libcrypto、WS172 p005 で独自の暗号へ）。`/dev/securityN` の ioctl を直接使う唯一の library。
- `userland/base/securityctl/`（`/sbin/securityctl`、root）と `userland/base/passkey-chip/`（`/usr/libexec/passkey-chip`、root、passkey-fido2 と同じ型）。

### 6.2 libsecurity の口（操作の水準、案）

```c
struct sec_device;		/* 開いた /dev/securityN と claim */
struct sec_key;			/* 読み込んだ鍵（transient の handle と公開鍵） */

int sec_open(unsigned index, struct sec_device **device);            /* 0 から。claim も取る（EBUSY のやり直しを含む） */
int sec_open_identity(const char *identity, struct sec_device **device); /* chip の同一性で選ぶ（N の不安定を吸収） */
void sec_close(struct sec_device *device);
int sec_info(struct sec_device *device, struct sec_chip_info *info);  /* 種類・製造者・同一性・DA の残り（§7.4） */
int sec_identity(struct sec_device *device, char *text, size_t size); /* chip の同一性（下） */
int sec_random(struct sec_device *device, void *buffer, size_t size);
int sec_key_create(struct sec_device *device, const struct sec_secret *auth, struct sec_blob *blob, uint8_t public_key[65]);
int sec_key_load(struct sec_device *device, const struct sec_blob *blob, struct sec_key **key);
int sec_key_sign(struct sec_device *device, struct sec_key *key, const struct sec_secret *auth, const uint8_t digest[32], uint8_t signature[64]);
int sec_key_agree(struct sec_device *device, struct sec_key *key, const struct sec_secret *auth, const uint8_t peer[65], uint8_t shared[32]);
void sec_key_unload(struct sec_device *device, struct sec_key *key);
```

- 鍵は P-256 の ECDSA（共通の芯、survey §2）。属性は `fixedTPM | fixedParent | sensitiveDataOrigin | userWithAuth | sign`（`noDA` は付けない）。親は SRK（storage の primary、TCG の既定の template の ECC P-256、`0x81000001` に persistent。無ければ毎回 `CreatePrimary`）。
- **session（S9）**: 鍵を使う command は全て HMAC session（`StartAuthSession` の salt を SRK の公開鍵へ ECDH で暗号化、`bind` なし、`TPMA_SESSION_DECRYPT | ENCRYPT` で parameter を AES-128-CFB で暗号化、`continueSession` なし）。SRK の公開鍵は**前もって確かめた物**（securityctl が provisioning の時に記録した SRK の名前 `/etc/security/chip-<id>.srk`、§6.3）とだけ salt を組む（interposer が偽の SRK を返すのを防ぐ）。KDFa（SP 800-108 の HMAC-SHA256）、cpHash・rpHash の確かめ（応答の HMAC が合わなければ失敗、`chip-tampered` の理由）。
- chip の同一性: storage の hierarchy の SRK の名前（`ReadPublic(0x81000001)` の `name`、nameAlg SHA-256）の 16 進の先頭 32 文字。SRK は owner の seed から決定的なので、`TPM2_Clear` か seed の変更（fTPM の reset）で変わる＝chip が変わった印になる（survey §3.7）。kernel の `location` は「どの node か」の手掛かり、同一性は libsecurity が決める（kernel に SHA-256 と TPM の構造の解析を入れない）。
- 応答の `TPM_RC_RETRY`・`TPM_RC_TESTING`・`TPM_RC_YIELDED` は 3 回まで 20 ms おきにやり直す（期限の中で）。`ESTALE` は claim を取り直して始めから。

### 6.3 securityctl（所有と provisioning、S8）

- `securityctl status [N]`: 種類・製造者・同一性・所有の状態（`TPM_PT_PERMANENT` の `ownerAuthSet`・`lockoutAuthSet`・`endorsementAuthSet`）・DA の値と残り（`TPM_PT_LOCKOUT_COUNTER`・`MAX_AUTH_FAIL`・`LOCKOUT_INTERVAL`・`LOCKOUT_RECOVERY`）。
- `securityctl provision [N]`（root、ADMIN の claim）:
  - **既に所有された chip**（`lockoutAuthSet` か `ownerAuthSet` が立つ: Windows との dual boot など）では何もせず止まり、chip の方式を出さない（S8）。`status` がその理由を言う。
  - そうでなければ: lockoutAuth を 32 byte の乱数にして `HierarchyChangeAuth(TPM_RH_LOCKOUT)`、`/etc/security/chip-<id>.lockout`（0600 root、root だけが読める所、S8）に保つ。ownerAuth は空のまま（SRK の作成に要るため。owner の hierarchy の鍵は全て自分の authValue で守る）。DA の値を `DictionaryAttackParameters`（`maxTries` 32・`recoveryTime` 600 秒・`lockoutRecovery` 86400 秒: Windows と同じ水準、survey §1.1、§10 の O3）で設定。SRK を `CreatePrimary` して `EvictControl` で `0x81000001` に置き、その名前（＝chip の同一性、§6.2）を `/etc/security/chip-<id>.srk` に記録。
  - salt に使う SRK の公開鍵の確かめは、この記録（と passkey の chip の行の同一性）と `ReadPublic` の返す名前の一致で行う。interposer が別の鍵を返すと名前が合わず `chip-tampered` で止まる。
  - 書く順: 乱数の lockoutAuth を先に file に書いて fsync してから chip に設定（途中で電源が切れても lockoutAuth を失わない）。
- `securityctl unlock [N]`（root）: lockoutAuth で `DictionaryAttackLockReset`（PIN の lockout を root が解く）。lockoutAuth の失敗は 1 回で `lockoutRecovery` の間止まるので、file が無い・壊れている時は送らない。
- `securityctl` は `/etc/passkey` に触れない（passkey の仕事）。

## 7. passkey の chip の方式（S4・S7・S12）

### 7.1 流れ

- sessiond → `/sbin/passkey`（`AUTH name chip`、PIN は pipe）→ `/usr/libexec/passkey-chip`（root、短命、5 秒の期限: docs/architecture/security.md の request）。passkey-fido2 と同じ形で passkey が spawn し、PIN を pipe で渡す。
- passkey-chip: `/etc/passkey` の chip の行（§7.3）を読み、chip の同一性で `/dev/securityN` を選び、DA の予算を確かめ（§7.4）、32 byte の challenge を乱数で作り、blob を `Load`、PIN の派生（§7.2）を authValue に salted session で `Sign`、行の公開鍵で署名を確かめる（S4 の (a): interposer は成功を偽れない）。結果は passkey の語（`ok`・`fail <reason>`）。
- 登録（Settings の鍵の頁 → sessiond → passkey `ENROLL name chip`）: PIN を 2 回、`sec_key_create`、行を足す。**pin の行と両立させない**: chip の方式を有効にした account の `pin` の行は消す（survey §3.7）。
- PIN の変更: `ObjectChangeAuth` を使わず、新しい鍵を作って行を差し替える（古い blob と古い PIN は公開鍵の不一致で無効）。

### 7.2 PIN の派生

- authValue = HMAC-SHA256(key = chip の同一性の bytes, message = "zedBSD passkey chip 1" ‖ uid（4 byte、big endian）‖ PIN の UTF-8) の 32 byte。uid を混ぜるのは同じ PIN の別の account の blob を区別するため（TPM の DA は chip 全体なので、混ぜても総当たりの数は増えない: survey §3.2 の注意は「uid を TPM の分離に使う」ことで、ここでは使っていない）。KDF を重くしない理由: 試行は chip が数える（offline の総当たりは blob が chip の外で使えないので不可能）。

### 7.3 `/etc/passkey` の行（版 1 のまま、新しい kind）

```
<name>:<uid>:chip:tpm2:<chip identity>:<public key>:<blob>:<label>:<date>
```

- `<chip identity>`: §6.2 の 32 文字。`<public key>`: P-256 の非圧縮点の base64url。`<blob>`: `TPM2B_PUBLIC ‖ TPM2B_PRIVATE` の base64url（呼び手が保管、S7）。`<label>`・`<date>`: fido2 の行と同じ。
- `record.c` の既存の規則（name と uid が一致する行だけ、知らない kind の行は保つ）に従う。passkey の `styles` に `chip` を足すのは実装の Phase。

### 7.4 DA の予算（S12）と理由の語

- 試す前に `GetCapability(TPM_PT_LOCKOUT_COUNTER・TPM_PT_MAX_AUTH_FAIL)`（認可は要らない）を読み、残り（`MAX_AUTH_FAIL − LOCKOUT_COUNTER`）が予備（**8**: 他の account・disorderly の停止の加算・将来の disk の暗号の分）以下なら試さず `fail chip-locked`（sessiond は失敗の数を増やさず、password へ誘導）。
- 理由の語（passkey の `fail <reason>` に足す）: `chip-locked`（予算・lockout）、`chip-changed`（同一性の不一致、`TPM_RC_INTEGRITY`・`TPM_RC_BINDING` で blob が読めない: 再登録を促す）、`chip-missing`（node が無い）、`chip-busy`（claim が取れない）、`chip-tampered`（応答の HMAC の不一致）、`chip-unprovisioned`（SRK の記録が無い: securityctl provision を促す）。wrong PIN は今の `fail pin`（`TPM_RC_AUTH_FAIL`・`TPM_RC_BAD_AUTH`）。
- sessiond の account ごとの 5 回は今のまま（memory の中）。

## 8. 試験（実装の Phase で）

| 層 | 試験 |
| --- | --- |
| host | libsecurity を libtpms（host の package）に直に link して marshaling・salted session・parameter の暗号化・DA の数（wrong PIN で LOCKOUT_COUNTER が増える、予算の閾値で `chip-locked`）・PIN の変更・chip の clear で `chip-changed`。allow-list の表と header の検査は `tpm2.c` を host で単独に（拒む command・vendor・size の不一致） |
| QEMU（T1） | swtpm の `-device tpm-tis` と `-device tpm-crb` の 2 つの image で: attach（info の link・manufacturer）、securityctl provision・status、passkey の chip の登録と login・誤った PIN・lockout、suspend・resume で `ESTALE` と再試行、claim の持ち主の SIGKILL の後に transient が残らない（`GetCapability`）、shutdown で `Shutdown(CLEAR)`（swtpm の log）。boot-test |
| 実機（5330、UAT） | start method の確認（§10 の O1）、passkey の chip の login、reboot・suspend の後の DA の数の増え方、乱数の混ぜ |

## 9. 実装の Phase の案（ベータ3 以降、Q1 が ws.md に投影）

| 案 | 内容 | 依存 |
| --- | --- | --- |
| p008 | kernel: UAPI `security.h`、`security.c`（node・claim・allow-list の枠・後始末）、`tpm2.c`（§4.2・§4.3）、`tpm-acpi.c`・`tpm-tis.c`・`tpm-crb.c`、電源と乱数の hook。host の allow-list の試験、T1 の swtpm（TIS・CRB） | この設計の review、O1、電源の WS との調整 |
| p009 | libsecurity（marshaling・session・crypto.h）と securityctl。host の libtpms の試験 | p008 |
| p010 | passkey-chip、passkey・sessiond・Settings の chip の方式、`/etc/passkey` の chip の行。T1 と 5330 の UAT | p009、WS172 p002・p007 |
| p005（既存） | OpenSSL を独自の暗号へ（libsecurity も含む） | p010 |

## 10. 残る点（実装の前に決める）

| ID | 点 | 今の扱い |
| --- | --- | --- |
| O1 | 5330 の ACPI `TPM2` の start method と `_HID`・vendor（survey §6 の 1） | TIS と CRB の両方を設計した。5330 が Linux で起動した時に Q1 が `/sys/firmware/acpi/tables/TPM2` を読む。結果で p008 の実機の関門の transport を決める |
| O2 | SRK を `0x81000001` に persistent で置くか毎回作るか | persistent（`EvictControl`、TCG の既定の handle）。Windows と共有の chip では使わない（S8 で chip の方式を出さない）ので衝突しない |
| O3 | DA の値（32 回・600 秒・86400 秒） | Windows と同じ水準の案。実装の Phase で ユーザーに数を見せる |
| O4 | disorderly の停止の検出（`SECURITY_INFO_DISORDERLY`） | 推定の方式は p008 で決める。決まらなければ flag を外す |
| O5 | 電源の hook の置き場（`sleep.c`・`shutdown.c` は電源の WS の範囲） | Q1 が p008 の前に調整 |
| O6 | Pluton・ARM の start method | 範囲の外（attach しない）。機種が出た時に transport を足す |
| O7 | docs（`docs/architecture/security.md` の chip の節、`docs/reference` の `/dev/securityN`） | 実装の Phase の最初に、この設計から書く |
