# ws172-p004b review-1（第 1 版の設計の敵対的 review、2026-10-13、Q1 が起動した design-reviewer）

照合: ws.md・survey.md（§3.0・§5・末尾の決定）、guardrail.md、coding-style.md、devfs.c・smartcard.c・file.c・sleep.c・freeze.c・shutdown.c・system-device.c・random.c・acpi-kern.c・acpi-tables.h・uapi/ioctl.h、passkey（main.c・passkey.h・record.c）、libpasskey/crypto.h、sessiond/auth-policy.c、docs/architecture/security.md。Linux の source は読まない。TCG の値で一次資料を確かめていない物は「要確認」。

**判定: 差し戻し（第 2 版が要る）。致命的 2・重大 7・中 10・軽微 11。** 骨格（案 D・claim・allow-list・generation・userland の salted session）は妥当。未解決は node の権限（devfs の規則では 0666）、管理の command と lockoutAuth の bus と disk での保護（S9・S10 の約束が成り立たない）、s2idle の resume が TPM の仕様と合わない、passkey・sessiond の口との不一致。

## 致命的

**C1. /dev/securityN が 0666 になり ADMIN に kernel の権限の検査が無い（§3.1・§3.3）。** src/kern/devfs.c:546-556 は名前で mode を決め、hidraw・smartcard・bluetooth・bridge・input-inject 以外は 0666。設計は「0600 root」だが devfs.c の変更を影響範囲・p008 に挙げていない。§3.3 は ADMIN を「root の中の衛生の境目」とし kernel は CLAIM で資格を見ない。→ 一般の利用者が CLAIM(ADMIN) を取れ、provisioning 前は空の lockoutAuth・ownerAuth で HierarchyChangeAuth により所有を奪え、後も許可された Sign に誤った auth で chip の DA を使い切り全 account の chip の login を止められる（S2 の破れ）。§8 の試験は全部 root で見つからない。直し: devfs.c に security_name() を足して 0600 を p008 の明示の作業に、kernel は CLAIM(ADMIN) で実効 uid 0 を検査（多重の防御）、QEMU の試験に「一般の利用者の open → EACCES」。

**C2. 管理の command と lockoutAuth の保護が無く S9・S10 が崩れる（§6.3・§1・§7.2）。** (a) bus: §6.2 の session の規則は鍵を使う command だけで、HierarchyChangeAuth(LOCKOUT) の newAuth・DictionaryAttackLockReset・DictionaryAttackParameters の lockoutAuth の扱いが無い。password session（TPM_RS_PW）だと lockoutAuth が平文で SPI に流れ、interposer が拾って DictionaryAttackParameters(recoveryTime=0) で DA を無効にし 6 桁の PIN を chip の速さで総当たりできる。(b) disk: /etc/security/chip-<id>.lockout は平文で、disk の暗号が無い間は別の OS を起動した攻撃者が blob（/etc/passkey）と lockoutAuth を読み DA を外して総当たりできる → §1・survey §3.0 の「別の OS の攻撃者に対しても PIN を守る」「盗んだ blob の offline の総当たりから守る」は成り立たない。直し: (a) lockoutAuth を使う・設定する command は全部 salted の HMAC session、newAuth は parameter の暗号化で、§6.3 に明記。(b) は技術判断で閉じられない: S8「root は信じる」の帰結として「物理的に持ち去られた機械では PIN の保護が無い」をユーザーに示し選ばせる（受け入れて文書に／lockoutAuth を保たない（解除は時間か firmware の clear）／紙の recovery code）。survey の S8 の推しは元々「案、要議論」。

## 重大

**M1. s2idle の resume が TPM の仕様と合わず undo と試験も無い（§4.5・§8）。** zedBSD の sleep は S0ix（s2idle）だけ（sleep.c:8-24）で TPM に _TPM_Init がかからない。TPM は _TPM_Init の後しか TPM2_Startup を受けず TPM_RC_INITIALIZE（Part 3 Startup）。§4.5 の「Startup(STATE)、失敗なら Startup(CLEAR)」は S3 の前提で、s2idle では両方 TPM_RC_INITIALIZE。足りない: suspend の hook の後の段（drv_pci_suspend_all・lps0_enter・events_sleep_begin・hal_irq_suspend、sleep.c:299-341）の失敗の undo、偽の起床（SLEEP_SPURIOUS_MAX）、2 秒を越える送信の方針。kern_sleep_supported() は LPS0 と s0idle を要求（sleep.c:121-140）、QEMU q35 に LPS0 が無く §8 の QEMU の suspend の試験は実行できない。直し: resume の判定を Startup(STATE) の結果で TPM_RC_INITIALIZE＝reset 無し（generation を増やさない）・成功＝Resume（増やす）・TPM_RC_VALUE＝state が失われた（Startup(CLEAR)、増やす）の表に、後の段の失敗の undo を sleep.c の順に、suspend の試験は 5330 へ（QEMU は host か kernel の試験の hook で代える）。

**M2. provisioning の順が自己矛盾し途中の失敗で永久に「所有済み」、TOFU も無明記（§6.3）。** file 名の <id> は SRK の名前だが lockoutAuth を先に書き SRK は最後に作る。HierarchyChangeAuth の後・EvictControl の前に止まると次の provision は lockoutAuthSet を見て「既に所有」で止まり二度と使えず、lockoutAuth の誤りは 1 回で 86400 秒止まり試せない。provisioning の時の interposer は偽の SRK を永久に記録させられる。直し: 「SRK を作る（決定的）→ id → lockout の file を書いて fsync → chip に設定 → DA の値 → EvictControl → .srk を書く」、再実行は「自分の lockout の file があり lockoutAuthSet が立つ」なら途中から再開、TOFU を §1 の脅威の模型に（EK の証明書の検証は後の選択肢）。

**M3. parameter の暗号化の属性が command ごとに正しくない（§6.2）。** 最初の parameter が TPM2B でない command・応答に暗号化の属性を付けると TPM_RC_ATTRIBUTES（Part 1）。TPM2_Sign の応答の最初は TPMT_SIGNATURE で ENCRYPT を付けると失敗。直し: command ごとの表（Create ○○、Load ○○、Sign ○×、ECDH_ZGen ○○、HierarchyChangeAuth ○—）。

**M4. passkey・sessiond・greeter の口と合わない（§7）。** 「wrong PIN は fail pin」の語は無く passkey は bad-secret（passkey/main.c:169、security.md の固定の語）、新しい chip-* の語は sessiond の対応表（auth-policy.c:35-61）に無く bad-secret として greeter に伝わる。「chip-locked で失敗の数を増やさない」は security.md の Failure counts（PIN の試みは passkey の起動の前に数え成功でしか消さない）と合わず sessiond の policy の変更が要るが無記載。AUTH name chip・ENROLL name chip は実際の要求の形（小文字の行、enroll-pin・enroll-fido2 など）と違い、enroll-chip・remove-chip・enrolled の chip= の出力が未定。WS200 の methods（passkey.h:74,90-93、既定 password,pin,fido2）に chip が無い。「chip を有効にすると pin の行を消す」は greeter の PIN・Password・Hardware Key の「PIN」を chip に回すかの UI の判断（ユーザー）。直し: §7 に passkey の要求・応答の追加、sessiond の対応表と数えるかの表、methods の bit、greeter に伝える語をまとめ、UI の点はユーザーに。

**M5. 送信中の process の死と claim の解放（§4.4・§3.3）。** claim は struct file * に付き（smartcard.c:66 の型）、解放は最後の close（file.c:2420-2441）。ioctl の実行中は参照が残るので SIGKILL でも TRANSMIT の待ちが割り込めなければ close が走らず、上限 120000 ms claim が残り次の login は chip-busy（security.md の SIGTERM→2 秒後 SIGKILL とも合わない）。足りない: TRANSMIT の待ちを致命の signal で割り込む（cancel を送り chip が idle に戻るまで有限で待ち EINTR）、SECURITY_CANCEL は claim の持ち主の file だけ（EPERM）で TRANSMIT の mutex を取らない作り（smartcard は transmit の間 ops_lock を持つ）、fork で claim が残るので libsecurity は O_CLOEXEC で開き fork しないと明記、close の後始末（GetCapability と FlushContext）が exit の path で TPM の期限ぶん止まる（上限か kernel thread）。

**M6. OpenSSL の例外の範囲の外で crypto.h にも要る物が無い（§6.1）。** Guardrail の例外（guardrail.md:223）は passkey-fido2・libpasskey・fidoctl と「FIDO2 の暗号」だけ（fidoctl を含めるにもユーザーのクリックが要った）。libsecurity・securityctl・passkey-chip が OpenSSL を使うには例外を広げるユーザーの判断が要るが判断の点にしていない。libpasskey/crypto.h:35-44 に AES-128-CFB・KDFa・ECC の salt の KDFe（SP 800-56A）が無い。直し: 例外を広げるかをユーザーの判断の点に、crypto.h に足す関数を列挙し p005（独自の暗号）の範囲に。

**M7. root の process が interposer の制御する応答を解析する（§6.1・§7.1）。** WS172 の決定「機器の data の解析は権限の無い子の process」（ws.md）と security.md:239-245 の passkey-fido2 の device helper（_passkey の権限、空の root）に対し、脅威の模型に interposer を含むのに passkey-chip・securityctl（root）が TPM の応答を全部 unmarshal する理由が無い。直し: 応答の HMAC（rpHash）を上限つきの最小の解析（header と parameterSize）で先に確かめ合った物だけを解析（HMAC の無い応答は RC だけ）、か fido2 と同じく解析を権限の無い子に。

## 中

1. authValue の末尾の 0（§7.2、要確認）: TPM は HMAC に使う時に末尾の 0 を取り除く（Part 1、MemoryRemoveTrailingZeros）。HMAC-SHA256 の 32 byte は約 1/256 の PIN で末尾が 0x00 になり libsecurity で取り除かないと正しい PIN でも AUTH_FAIL で DA も増える。§7.2 と host の試験（末尾が 0 になる PIN）に。
2. ACPI の口（§4.1）: (a) drv_acpi_firmware_find は firmware の record を取るが record は acpi-kern.c の static で外からは drv_acpi_os_table（acpi-kern.c:550-566）だけ、TPM2 table を読む口を新しく出すことを p008 に。(b) ACPI start の _DSM は「function 2」だが TCG の start の _DSM（UUID 6BBF6CAB-…）の start は function 1 のはず（要確認）。(c) 各 command で AML を評価するので lock の順（TPM の ops → AML）と sleep の段の中で評価してよいか。
3. AMD の判定（§4.6・§3.2、要確認）: TCG の Vendor ID Registry では "AMD\0"（0x414D4400）のはず。manufacturer は TPM の big-endian の UINT32 を数値にした物と UAPI に（sparc64 など）。
4. ACPI start（2・8）は QEMU でも 5330 でも試せない（swtpm の tpm-tis は 6、tpm-crb は 7、5330 は TIS の見込み）。機種が手に入るまで 2・8 は attach しない（7 だけ）か「未試験」の印。
5. chip の状態の機械（attach 中・使える・失敗・suspend 中・shutdown 済み）と遷移、各状態の CLAIM・TRANSMIT の errno が無い。Shutdown(CLEAR) の後に shutdown の準備が失敗して続行する場合（shutdown.c:54 の retry）、ETIMEDOUT の後の「再初期化の候補」。
6. Clear の path が無い: 常に拒むので zedBSD の所有を外す手段が firmware の UI だけ。後から入れた Windows は lockoutAuth が設定済みの TPM を見つけ、firmware の clear で zedBSD の chip の鍵が全部 chip-changed。securityctl clear（ADMIN と lockoutAuth）を設けるか文書にするかはユーザーの判断。
7. 同一性の定義の揺れ: 「0x81000001 が無ければ毎回 CreatePrimary」は persistent handle の ReadPublic で定める定義・O2・chip-unprovisioned と矛盾。名前の 16 進の先頭 32 文字は nameAlg の 000b を含み実際は 112 bit、digest の部分を使う。§2 の vtable の identity は「同一性は libsecurity が決める」と矛盾。
8. 出典の license: start method の値と AMD の quirk の出典が Linux の actbl3.h・tpm_crb.c。Guardrail の GPL の例外は WS141・WS037・WS214 だけで WS172 には及ばない。p008 の前に TCG の PTP・ACPI の仕様、FreeBSD の sys/dev/tpm、ACPICA の upstream（dual license）に置き換えることを Phase の作業に。
9. Phase の分け方: p008 は大きすぎる → p008a（UAPI・security.c・tpm2.c・TIS、swtpm の tpm-tis で試す）・p008b（CRB）・p008c（電源の hook、O5 と WS052（S0ix の持ち主）の調整、5330 で試す）。p009 は host の libtpms で UAPI の header ができれば p008 と並べられる。p010 の依存に WS200（methods）・ws199 の passkey の口・sessiond の policy の変更。p008 の依存「O1」は QEMU の作業を止めない。
10. S1〜S13 の外の技術判断でユーザーの判断が要る物: DA の予備 8、pin の行を自動で消すこと、/etc/security/ の新設、「測定は kernel だけが行う形にする予定」（§4.3）、Clear を出さないこと、C2(b)、M6。「推奨でOK」は S1〜S13 の推しへの答えなので別に示す。

## 軽微

1. §5 の PTP の TIMEOUT_D は 30 ms のはず（30 s ではない、要確認）、interface の timeout（A〜D）と command の所要時間の区分が混ざっている。
2. SECURITY_INFO_FIRMWARE_TPM を「CRB なら firmware」と推定するのは不確か（CRB の dTPM もある）、PTP の TPM_INTERFACE_ID を読む。
3. UAPI の reserved は 0 を要求し 0 でなければ EINVAL と明記。
4. kern_random_add は crediting せず次の reseed（最大 5 分、random.c:43,197）まで出力に効かない、attach の直後の効果を期待するなら reseed を促す。
5. ADMIN の表の SetPrimaryPolicy（後）は外す、HierarchyControl は不要（TPM_PT_STARTUP_CLEAR の GetCapability で読める）。
6. 応答の RC は format-one（session の番号の bit）を mask して比べる、TPM_RC_LOCKOUT を chip-locked に。
7. O4 の候補に TPM2_ReadClock の TPMS_CLOCK_INFO.safe（disorderly の後に NO、要確認）。
8. §8 の「swtpm の log で Shutdown(CLEAR) を確かめる」は log で判定しない方針に近い、次の起動の LOCKOUT_COUNTER の不変か safe の flag で。
9. attach の時にも firmware が残した transient・session を flush。
10. ws.md の p004 の Status が planning のまま。
11. O7 の docs は「docs は目標の設計を先に書く」に合わせ実装の前の設計の段で。

## 問題が無かった点

ioctl の group 'K' は未使用。struct security_transmit（48 byte、u64 が先頭）と security_info（192 byte）は padding が無く i386・m68k・sparc64 でも同じ layout、size は ioctl の 13 bit に収まる。allow-list の vendor bit（bit 29）と commandSize と command_size の一致の検査で command の密輸はできない。NV・PolicySecret・Clear を普通の claim で拒むので lockout の hierarchy の DA と NV の枯渇は普通の claim から起こせない。suspend の hook を PCI の suspend の前・resume の hook を PCI の resume の後に置く順は SPI・LPC の dTPM に正しい。S4 (a) と毎回の乱数の challenge で interposer は成功の偽造も replay もできない。/etc/passkey の chip の行は PASSKEY_FIELD_MAX（1024）に収まる見込み。

確かめられなかった（推測）: TCG の値（_DSM の function、TIMEOUT_D、AMD の vendor ID、authValue の末尾の 0、ReadClock の safe、ENCRYPT と TPM_RC_ATTRIBUTES の細部）、Windows の provisioning の挙動。
