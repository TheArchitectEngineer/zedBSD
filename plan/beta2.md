<!-- awesome-plan project=zedbsd record=beta2-triage -->

# ベータ2 の残り作業（2026-10-13 夜 Q1 更新）

公開は 10/17（OSC 当日、朝から会場）なので、**10/16 中にリリースの準備を終える**。
体制（2026-10-13 ユーザー「N=3で、P2を追加し、実装を担当してもらいます。」）: Q1＋P1・P2・P3（ベータ2 の残りの実装）＋T1（試験）＋ユーザー（UAT・判断）。WS074（ブラウザ）は Codex の別のセッションが main の checkout で作業（ユーザーの依頼、ベータ2 まで）。**ベータ2 には機能を足さない**（2026-10-13 ユーザー「これ以上ベータ2に機能追加しないつもりなので、実装を終わらせましょう。」）。この表は計画で、Queue の承認ではない。
見積もりは LW（1 LW ≈ 実時間 15〜25 分（2026-10-14 ユーザーの較正「8 LWは、だいたい実時間で2～3時間です。十分行けます。」、2026-10-15 ユーザーがこの換算を正と承認））。完了した項目は消す。Q1 は進むたびに更新する。

## ユーザーが今できる試験（Q1 が随時更新、ユーザーの都合の良い時に。2026-10-15 ユーザー「私にテストを依頼しないでください…取り組むことのできるテストの一覧を示していくスタイルにしましょう！」）

| # | 機種 | image | 何をする | 見る所 |
| --- | --- | --- | --- | --- |
| U1 | SV7・5330 | `build/uat/main-amd64/hdd-image.img`（main 2026-10-15 01:57、sha256 c76538263025b37d…） | USB に書いて起動 | /boot/boot0・/boot/efi が見える（BUG-316）、Windows・macOS で ZEDBSD の区画が見える（WS238）、SV7 は desktop（WS237） |
| U2 | SV7・5330 | 同上 | browser で yahoo.co.jp | feed の画像の枠が無い、画像の大きさが Chrome と同じ（ws074-p233・p234） |
| U4 | Pi 4（OLED の touch） | T1 が main の kernel と compositor を Pi に入れた後（Q1 が知らせる） | tap・中央の swipe・上端・下端の swipe | 指の下に反応する、中央の 1 本指の swipe で desktop が変わらない（BUG-315） |
| U8 | RTX 機（10.0.30.5、i5-13400F＋RTX 2070） | `build/ws037-nvrtx/rak/hdd-image.img`（main a3cf77c12、WS037 RAk: main の既定＋`nvrtx.vk=1`、sha256 6a96a228…） | USB に書いて素で起動（BIOS: CSM off、primary display は PEG、monitor は RTX） | logo → GSP の起動の間に少し黒くなってよい → RTX の monitor に Keiland の desktop。出なければ最後の画面の写真（診断の RAd の image を Q1 が作る） |
| U9 | 5330（素の起動） | `build/uat/ws245-amd64/hdd-image.img`（U10 と同じ image） | AX211 で WiFi の AP に接続（Settings か `wifi wlan0 connect`） | 接続・IP の取得・通信ができる（AX211 の driver の出典と規約の整理の後。T1-700 の VFIO では整理の前も後も接続が失敗したので、素の起動での確かめが整理の完了の条件。2026-10-15 ユーザー「実機で接続を確かめてから完了」） |
| U10 | 5330（素の起動） | `build/uat/ws245-amd64/hdd-image.img`（main 1716c14bc、config/ci/config-amd64.mk、2,485,125,120 byte、sha256 590efabeed28ecb8…） | USB に書いて起動 | i915 の display の書き直しの途中のコード（土台・VBT/EDID の解析、まだ呼ばれない）と Gen9 の移設が入った main で、今までと同じに起動・内蔵 LCD・backlight・desktop が動く（2026-10-15 夜 ユーザー「ベアメタルにしましょう。merge後のコードでUATイメージをビルドしてパスを教えてください。」。passthrough では VBT が無く panel が点かない）  **合格**（2026-10-15 夜 ユーザー「問題なく使えてます。」。Q1 が ssh で確かめた: 普通の起動で session・compositor・LCD・backlight brightness=100、GPU の投入 2〜4.6 ms、device lost 0） |
| U11 | 5330（素の起動） | main 3e1e0c687 以降の amd64 の image | 普通に起動して desktop を使う | 内蔵 LCD に desktop が正しく映る、backlight が点く（i915 の display の書き直しの最初の段 p047 の後。ssh で取った register の証拠は合格、目視も 2026-10-16 15:00 ユーザーが合格「5330はデスクトップが起動しました。OKです。」。起動に約 1 分かかる件は WS253） |
| U12 | SV7（Gen9、10.0.10.27） | main 9244ac507 以降の amd64 の image | 普通に起動して desktop を使う | 今までと同じに映り desktop が動く。dmesg に `i915: xd: no description of the device …` の 1 行が出て、OpRegion の行が前と同じ（i915 の表示の書き直し p016: OpRegion の読みの置き換えの後） |
| U13 | RDNA 機（RX 5600 XT、10.0.10.3） | main f2ed268e4 以降の amd64 の image | 普通に起動し、dock から app を開く・閉じる、window を動かす | desktop の色が正しい、Terminal・Music・Calendar・Files が開く（Notes は BUG-329 で開かない）、乱れ・ちらつきが無い **合格**（2026-10-16 夜 ユーザー「お、RDNA機で多数のアプリが起動しました！目視。」） |
| U14 | RDNA 機（RX 5600 XT、10.0.10.3） | P3 が知らせる BUG-331 の試験の kernel（X: console の map を UC、Y: WC） | App Home の開閉を 10 回ほどと app の開閉 | 固まらない、mouse が止まらない（BUG-331 の切り分け。P3 が自動で回すので任意） |
| U15 | RDNA 機（RX 5600 XT、10.0.10.3） | P3 の p015 の kernel（agent/p3-ws039 の 7fac15fed 以降） | zedBSD を起動し、`amdrdnactl advance A11` | 約 0.3 秒暗くなった後、同じ画面がそのまま戻る（画面の点け直しの目視。OTG の CRC は計算されていない見込みで証拠にならない、frame counter と underflow は ok） |
| U16 | RDNA 機（RX 5600 XT、10.0.10.3） | P3 の p015 の kernel（agent/p3-ws039 の 471f1e395 以降） | zedBSD を起動し、`amdrdnactl advance A12` | 画面が 1280x720 に変わって約 3 秒映り、1920x1080 に戻って同じ画面が映る（mode の切り替えの目視） |
| U17 | RDNA 機（RX 5600 XT、10.0.10.3） | P3 の p015 の kernel（agent/p3-ws039 の bf4fb396f 以降） | desktop が出ている時に monitor の cable を抜いて数秒後に挿す | 挿した後に画面が戻る（本物の HPD。amdrdnactl hotplug off/on の模擬は ok 済み） |
| U18 | RDNA 機（RX 5600 XT、10.0.10.3） | main 0c2a5c5e1 以降の amd64 の image | text の login で `amdrdnactl reset`、また `amdrdnactl present 1280x720`・`800x600` | reset の間に画面が消えて戻る、1280x720・800x600 の絵が映り 1920x1080 に戻る（目視） |
| U19 | 5330 | main の WS245 の merge（2026-10-17）以降の amd64 の image | 蓋を閉じて suspend し、開けて戻す | desktop と backlight の明るさが戻る（i915 の書き換えの suspend の戻り） |
| U20 | 5330 | main の WS245 の merge（2026-10-17）以降の amd64 の image | desktop で HDMI の monitor を挿し、抜く／USB-C の DP の monitor を挿し、抜く | 2 画面になり、抜くと 1 画面に戻る |
| U21 | 5330 | main の WS245 の merge（2026-10-17）以降の amd64 の image | HDMI の monitor を挿したまま蓋を閉じて起動 | 外部の画面に desktop が出る |
| U22 | SV7（Gen9） | main の WS245 の merge（2026-10-17）以降の amd64 の image | 起動して backlight を変える | desktop が出て明るさが変わる |
| U23 | 5320（Tiger Lake） | main の WS245 の merge（2026-10-17）以降の amd64 の image | 起動、backlight、suspend と戻り | 今までと同じに動く（Tiger Lake の Type-C も新しい code で動かすようにした） |
| U24 | SV7（Gen9、10.0.10.27） | T1 が main 7f48ed5b2 以降で作る（T1-710） | USB の zedBSD で起動して電源を入れておく（ssh が届くように） | T1 が Gen9 の display の書き直し（WS260）の回帰を自動で流す。画面は撮れないので、desktop が内蔵の画面に出るかの目視があれば |
| U25 | 5330（10.0.30.3） | 同上（T1-710） | ZEDBSD の区画の USB 記憶を挿しておく（今は外れている） | T1 が display 13 の回帰を自動で流す（WS260 で xd に display 9 を足した後に壊れていないか） |
| U26 | RG350（OpenDingux の microSD） | `build/uat/rg350/UZIMAGE.BAK`（main 7adce0d6d、WS273 p003、6,133,704 byte、sha256 12a5d7b9c34bc8f4…） | 先に card の第 1 区画が FAT32 か、既存の `UZIMAGE.BAK`・`VMLINUZ.BAK` があれば退避。`UZIMAGE.BAK` を第 1 区画に置き、**Y ボタン（PE28）を押しながら**電源を入れる（Select は押さない）。X で OpenDingux が起動したら X も試す | backlight が点き、PB30 の LED が 1・2・3 回の点滅を順に繰り返す（その後は止まったままで正常、timer・LCD は未実装）。点滅の間隔、長い 1 回＋短い n 回の点滅（例外の印、n を数える）が出たらその n。OpenDingux が普通に起動したら zedBSD は読まれていない。写真か動画で返す（2026-10-19 ユーザー D7「承認する」） |
| U27 | VisionFive 2（QSPI の U-Boot、boot の switch は 00 のまま、HDMI と USB keyboard） | `build/uat/vf2/native.img`（main 993f9355d、gpu-bars 入り、`build/uat/vf2/config-vf2-uat.mk` = config/ci/config-visionfive2.mk から powervr-firmware を外した物（license の同意を Q1 が代わりにしないため）、404,750,336 byte、sha256 bc97e7c991c1bf0d…、GPT: p1 EFI・p2 zedBSD-root・p3 scratch） | SD に `sudo dd if=build/uat/vf2/native.img of=/dev/sdX bs=4M conv=fsync status=progress`（device を取り違えない）、SD から起動して画面を見る。root/root で login し `mount`、`echo p015 > /root/p015.txt; sync`、reboot して `cat /root/p015.txt` | 画面: WS256 p011 の framebuffer の console、p014 の SD の行（`dw-mmc: mmcblk0 …`、`visionfive2: SD card is mmcblk0`）、`vfs: rootpart … /dev/mmcblk0p2`、`powervr: BXE 36.50.54.182 at <rate> Hz, ALIAS OK`（WS269 p004、ALIAS MISMATCH・UNAVAILABLE や BVNC の違いならその行をそのまま）、`login:`。USB keyboard で login できる（p020）。出てはいけない行は plan/ws256/phase015/phase.md の実機の手順 5.8。画面の写真で返す |
| U28 | VisionFive 2 | U27 と同じ image | login の後 `gpu-bars`（3 秒ずつ RGBA・BGRA で帯）、次に `gpu-bars --hold` を出して Ctrl-C | 赤・緑・青・白の縦の帯が 2 回とも同じ色で出て console に戻り、`GPU-BARS PASS rgba=1 bgra=1 WxH`。`--hold` は Ctrl-C で console に戻る（PASS の行は出ない）。FAIL なら `GPU-BARS FAIL step=… error=…` の行（WS269 p007） |

## 日程

| 日 | やること |
| --- | --- |
| **10/14 18 時** | コードフリーズ（2026-10-14 ユーザー）。Gen9（WS229、既定で有効）・AHCI（WS231）・menuconfig（WS232）は入る（2026-10-14 の T1 で確かめた） |
| 10/14〜 | 規約の見直し、T1 の確かめ、release notes などの文書、build の素振り |
| **WS074 の merge の後** | 最終の build（2026-10-14 ユーザー: Codex が yahoo.co.jp を Chrome とゼロ差分で描けるまで実装する見込み。その成果を merge して初めて最終 build）。最終回帰（WS129 p006）と公開の準備（WS129 p008、公開はユーザーの指示で） |

使用量: リリース完了までに Claude Max 20x の約 1.7 週間分（2026-10-14 ユーザー）。

## 今の状況（2026-10-13 夜）

- 2026-10-13 のユーザーの判断: UAT 待ちは全部 cleared。WS049・WS100・WS127・WS128・WS142・WS182・WS183・WS200・WS120・WS168・WS178・WS187 は completed、WS014・WS044・WS048・WS118・WS139 は close、WS195 は cancel。WS004・WS009・WS046・WS074・WS095・WS155・WS157・WS171・WS172・WS196・WS217 はベータ2 へ。WS197 の HFP はベータ3。WS172 はベータ2 では設計まで。BUG-165・287・288・290・291・293・295・296・298 は resolved（実機 OK）。
- 2026-10-15 ブラウザ（B1、merge d1fbf4fbd）: ws074-p233 yahoo の feed の画像の枠を解消（`transform: scale`・`transform-origin`）、p234 `aspect-ratio`・`object-fit`・`object-position` を実装（aspect-ratio の reftest 73→138 PASS）。host のみ、QEMU・実機は未実施。
- 2026-10-15 ユーザーの決定: (1) U1・U3 は main に入れ main で試せるように、U1〜U3 を 1 つの USB の image で試せたら知らせる。(2)「nvrtxを既定でONにしてください。どうでNVIDIA GPUが動かなかったらデスクトップも動かないので…OFFにする必要性がない」→ pci.drivers・config-amd64.mk・config.mk で y（Q1 647d43d37）、runtime の段の既定 on と方法 A（USB から素で起動）は P1。(3)「Pi4のビデオでコードも既定でON。i915のGen9、Gen9.5, Gen9のディスプレイ制御も既定でON」→ arm64.drivers の VCHIQ・VIDEO を y、`I915_GEN9_DISPLAY_DEFAULT` を FULL（Q1）。(4)「止まっている作業は後回しにして、できる作業に移ってください。」
- 動いている実装: P1 WS217 p003（回転に合わせた入力の座標）、P2 WS145 p005（印刷の Linux・FreeBSD）→ WS161 の build → WS172 p004b の設計、P3 WS156 p006（Linux の D-Bus の通知）→ WS051 p005b → WS081 → WS033 p001。
- T1 の待ち（QEMU）: T1-583 画面の回転（WS217 p002b）、T1-584 Photos の大きな JPEG（WS157 p015・p017）、T1-585 TCP の損失の時と 60 秒を超える転送（WS220 p005）、T1-586 画面 keyboard の遅れの計測（低い優先度）。5330 が届けば T1-582（BUG-300 の切り分け）。
- main に入って試験待ちの物: WS216 Notes の描く道具（p004〜p006 cleared、p007 は 5330 のユーザーの確かめ）、WS220 TCP（p003・p004 cleared、p005 は T1-585）、BUG-300 の直し（終わりの判定、50% の原因は 5330 の log 待ち）。
- 残る既知の Bug は USB LAN の遅さ（BUG-222）ほか（下の「既知の問題に書いて出す」）。

## 必須

| 項目 | 状態 | LW | 担当 |
| --- | --- | --- | --- |
| [WS202](ws202/ws.md) 動画再生（libmedia の H.264・AAC を Video Player・Music で使う） | p013 の UAT OK。残り: p012 の T1 の全項目、BUG-300（スライダー約 50%、5330 の CLOCK の log 待ち） | 0.5 | T1 |
| [WS197](ws197/ws.md) Bluetooth のスマホ連携（SMS の MAP・連絡先の PBAP） | UAT は済み（2026-10-13 ユーザー）。HFP（p006・p007）はベータ3（既知の問題に書いた） | 0 | — |
| [WS129](ws129/ws.md) p005・p013 release notes・既知の問題・利用の手引き | 2026-10-14 P3 が 10/13〜14 の追加（vi・useradd など・menuconfig・Retro・BUG-302・303・Linux の Bluetooth・回転）を書き足した。Gen9・AHCI は〔判定待ち〕の印（18 時の判定で残すか消す）。HFP の行あり（DSDT は BUG-165 の resolved で既知の問題は不要）。browser の節は WS074 の merge の後。Notes の描く道具の節は未 | 0.5 | Q1 |
| [WS129](ws129/ws.md) p006 最終回帰（release の image） | 10/14〜15 | 3 | T1 |
| [WS129](ws129/ws.md) p008 公開の準備（tag・CI・配布物） | 手順は用意済み。10/16、公開はユーザーの指示 | 0.5 | Q1 |
| [WS196](ws196/ws.md) useradd・usermod・userdel（account-admin を呼ぶ前段）と account-admin の欠陥の直し | completed（2026-10-14、QEMU の T1-603 PASS） | 0 | — |
| [WS227](ws227/ws.md) menuconfig の表示名・画像と PDF の library を Desktop へ・Linux/FreeBSD に独自の base の命令 | completed（2026-10-14） | 0 | — |
| [WS228](ws228/ws.md) 独自実装の簡単な vi（**ベータ2 の最後の機能**、2026-10-14 ユーザー「ここまでがベータ2ですね。」） | completed（2026-10-14、QEMU の serial と console で PASS） | 0 | — |
| [BUG-302](bugs/BUG-302.md) emacs の SKK の辞書が見つからない（UAT） | resolved（T1-594 QEMU PASS）、実機の UAT | — | ユーザー |
| [BUG-307](bugs/BUG-307.md) UFS の fsync が volume 全体を書き出す | resolved（2026-10-14、T1-615 QEMU PASS、修正 f9f7acba9・bec395969）。負荷ありの最初の fsync の約 19 s はベータ3 の候補 | — | — |
| [BUG-309](bugs/BUG-309.md) PC-9821 Ra43 が pc98 の disk を起動の disk と認識しない | resolved（2026-10-14、ユーザーの実機 UAT OK、ZuluIDE） | — | — |
| [WS225](ws225/ws.md) Vulkan の header の書き直し | blocked（2026-10-13 ユーザー「まだ着手せず、ブロッキング」） | — | — |

## 既知の問題に書いて出す（ベータ3 以降）

| 項目 | 理由 |
| --- | --- |
| BUG-255（蓋を閉じた間の HDMI）、BUG-159（電池で 5 fps）、BUG-145（AX211 の DHCP）、BUG-222（USB LAN の遅さ） | 調査が長い・実機の時間が要る |
| WS197 の HFP（スマホの通話） | 2026-10-13 ユーザー「HFP 全体をベータ3」 |
| WS201（/home の暗号化）、WS198（self-build）、[WS001 p045](ws001/phase045/phase.md) POSIX の header、[WS126](ws126/ws.md) Python、WS172 の実装 | ベータ3 の列 |
| 規約の全文の見直しの Phase（各 WS） | ユーザーの決定でベータ3 |

## ベータ2 の残りの工数（2026-10-13 夜 Q1 更新、±50 %）

各 ws.md の残りの Phase からの見積もり（2026-10-13 の 2 つの担当の見積もりに、その後の完了と決定を反映）。

- 実装・設計の残り: 空き時間だけの WS031・WS075 と blocked の WS225 を除いて **約 19 LW**。今の担当: P1 WS217、P2 WS145 → WS161 → WS172 の設計、P3 WS156 → WS051 → WS081 → WS033。
- 待つだけ（T1・判断・完了の記録）の WS は表に入れない。待ちの後に出うる直しは数えていない。ベータ3 に回った規約の全文の見直しなど約 32 LW は別。

| WS | 残りの実装 | LW | 担当 |
| --- | --- | --- | --- |
| [WS217](ws217/ws.md) | 画面の回転: p003 入力の座標（p002b は T1-583 待ち） | 3（推測） | P1 |
| [WS196](ws196/ws.md) | useradd・usermod・userdel（先に判断 E1〜E6、WS195 の cancel で前提 D7 の見直し） | 2.3 | 未割当（判断待ち） |
| [WS143](ws143/ws.md) | Bluetooth の 5330 の firmware の load と scan の後の直し、Linux の BlueZ の backend | 2.2（推測） | 未割当 |
| [WS004](ws004/ws.md) | WLAN の driver の共通化（p039、任意）、SN740 の実機の手順（5330 の NVMe に書き込んでよい） | 2 | 未割当 |
| [WS113](ws113/ws.md) | Linux・FreeBSD の KMS の互換（p010、後回し） | 2（推測） | 未割当 |
| [WS171](ws171/ws.md) | hal.h の契約の comment（優先度低め、やることが無い時） | 1.5 | 空き時間 |
| [WS172](ws172/ws.md) | p004b の設計だけ（S1〜S13 は推奨、実装はベータ3） | 1.5 | P2 |
| [WS009](ws009/ws.md) | /dev/gpu の文書（DOC-54、保留の解除の後） | 1 | 未割当 |
| [WS051](ws051/ws.md) | USB-C の抜けの停止（p005b） | 1 | P3 |
| [WS081](ws081/ws.md) | touch の L3 の計測と係数 | 1（推測） | P3 |
| [WS090](ws090/ws.md) | p026 の層 3（5330 で測ってから） | 1（条件つき） | 未割当 |
| [WS095](ws095/ws.md) | IME の Browser の欄（p009、WS074 次第）と回帰（p011）、優先度低め | 1 | 未割当 |
| [WS145](ws145/ws.md) | 印刷の Linux・FreeBSD の build・install と guest（p005） | 1 | P2 |
| [WS156](ws156/ws.md) | 通知の Linux の D-Bus（p006） | 0.7 | P3 |
| [WS033](ws033/ws.md) | USB LAN の後挿し（p001） | 0.5 | P3 |
| [WS129](ws129/ws.md) | release notes・既知の問題・手引き | 0.5 | Q1 |
| [WS161](ws161/ws.md) | YubiKey の Linux・FreeBSD の build | 0.5 | P2 |
| [WS202](ws202/ws.md) | BUG-300 の残り（5330 の log の後） | 0.5（推測） | T1 → 直しは未割当 |
| [WS089](ws089/ws.md)・[WS193](ws193/ws.md)・[WS191](ws191/ws.md) | 小さな照合・残り | 0.3・0.3・0.1 | 未割当 |
| [WS031](ws031/ws.md)・[WS075](ws075/ws.md) | Vulkan の実行器と compiler・i915 の高度化（空き時間だけ） | 7・4（推測） | 空き時間 |
| [WS225](ws225/ws.md) | Vulkan の header の書き直し（blocked、WS226 の後） | 8（推測） | — |

2026-10-13 夜に表から外した（済み・決定）: WS157 p015・p017（main、T1-584 待ち）、WS046（p007・p014 cleared）、WS102 p011（計測を入れ、判定は 5330 の実機、直しは足さない）、WS216 Notes の描く道具（p004〜p007 の実装は main、p007 は 5330 のユーザーの確かめ）、WS220 TCP（p003〜p005 は main、p005 は T1-585）。
記録のずれ（phase.md が正しく ws.md の表が古い）: WS095 p006・p007、WS190 p002・p003、WS194 p001。

## 終わっていない WS（2026-10-13 Q1、114 件）

2026-10-13 ユーザー「終わっていないWSとその概要、何がブロッキングしているかを、表にして、plan/beta2.mdに入れてください。」→ 各 ws.md の Status・Resume point・Phase の表から作った（記録に無い事は「不明」）。WS は completed でない物（incomplete・planning・planned）。区分は ws.md・この文書・master.md に根拠がある時だけ断定し、master の「ベータ1」の段は fg019 の決定（最初のベータはベータ2）でベータ2 に読み替えた。
注: 2026-10-13 ユーザー「UATの結果待ちのものは、すべてテスト済み…」で WS049・WS100・WS127・WS128・WS142・WS182・WS183・WS200 を completed にして表から外し、WS050・WS083・WS130・WS154・WS155・WS159・WS164・WS165・WS170 はベータ2 の範囲は完了（残りは規約＝ベータ3）。2026-10-13 ユーザーの指示で WS014・WS044・WS048・WS118・WS139 は close、WS195 は cancel して表から外した。WS004・WS009・WS046・WS095・WS155・WS157・WS171・WS172・WS196 はベータ2 へ移した（WS095・WS171 は優先度低め、WS171 はやることが無い時）。WS120・WS168・WS178・WS187 は 2026-10-13 ユーザーの指示で completed にして表から外した。WS172 は ws.md と phase.md の状態が食い違う。WS123 は canceled。

### ベータ2 の物（54 件）

| WS | 概要 | 状態 | 止めている物 | 区分 |
| --- | --- | --- | --- | --- |
| [WS031](ws031/ws.md) | i915 の native Vulkan の実行器と compiler | incomplete。p011 big-bang 待ち、p024・p025 blocked、p021〜p048 の多くが planning | UAT 待ち（p024・p025）、空き時間だけの WS（Focus） | ベータ2（空き時間だけ） |
| [WS033](ws033/ws.md) | 有線 LAN（USB LAN）の常駐管理 | incomplete。p001 in-progress（L2 は p002 へ）、p002 planning、p003 planned | 実機の確かめ（5330、ユーザーの時期）、規約の全文の見直し p003 が未着手 | ベータ2（旧ベータ1 の段） |
| [WS050](ws050/ws.md) | USB-C の UCSI（role・Alt Mode） | ベータ2 の範囲は完了（2026-10-13、UAT はユーザー OK） | なし（残りは規約＝ベータ3） | ベータ2 |
| [WS051](ws051/ws.md) | USB-C の DP Alt Mode（i915 の Type-C） | incomplete。p004c・p005b in-progress、p004a・p004b・p005a は Q1 の判定待ち、p006 planned | 実機の確かめ（5330 の GOP が USB-C の起動・TC1）、BUG-266、規約はベータ3 | ベータ2 |
| [WS075](ws075/ws.md) | i915 の高度化（実行器・compiler・描画の速さ） | incomplete。p006・p007・p032 in-progress、p017〜p031 の一部 uncleared、p010 planning | 描画の高速化はユーザーの再開の指示待ち、空き時間だけの WS（Focus）、p032 は実機 | ベータ2（空き時間だけ） |
| [WS079](ws079/ws.md) | Notes・PDF Viewer | incomplete。実装の残りなし、p017 は Q1 の判定待ち | 実機の確かめ（5330 の mouse・Windows の QEMU の touch で S8・S9、ユーザー） | ベータ2 |
| [WS081](ws081/ws.md) | touch の質（遅れ・報告の率） | incomplete。p007・p017 planned、p009 planning | Windows の機械での touch の計測（ユーザー） | ベータ2（旧ベータ1 の段） |
| [WS083](ws083/ws.md) | Vulkan Video（H.264、Intel Xe-LP） | ベータ2 の範囲は完了（2026-10-13、UAT はユーザー OK） | なし（残りは規約＝ベータ3） | ベータ2 |
| [WS084](ws084/ws.md) | i915 の firmware の画面の引き継ぎ | incomplete。p003 cleared（2026-10-13 ユーザーの実機の確かめ OK、素の起動 10 回）、p004 in-progress | p004 は Q1 の判断待ち（今は何もしない） | ベータ2 |
| [WS089](ws089/ws.md) | Settings の app | incomplete。p011・p014 planned・planning（実機）、p018（規約）planning | 実機の確かめ（5330 の passthrough、p014 は Wi-Fi の driver が要る）、規約はベータ3 | ベータ2 |
| [WS090](ws090/ws.md) | widget の library（libkeiland）と Mahora の font | incomplete。p009・p018 は Q1 の判定待ち、p020・p021 test-wait、p026 in-progress、p012 planning | ユーザーの判断（p020・p021 の PNG の外観）、5330 の UAT と測定（p026）、規約はベータ3 | ベータ2（旧ベータ1 の段） |
| [WS094](ws094/ws.md) | desktop の file の icon | incomplete。実装の残りなし、p007・p009・p012 が残り | 実機の確かめ（5330、p012）、規約はベータ3、p009 はベータ3 の性能の Phase へ | ベータ2（旧ベータ1 の段） |
| [WS099](ws099/ws.md) | compositor（窓の管理・遷移） | incomplete。p012 planned（実機）、p022 planning（規約と回帰）、p006 は記録だけ | 実機の確かめ（5330 の C1 の目視、ユーザー）、規約の全文の見直し p022 が未着手 | ベータ2（旧ベータ1 の段） |
| [WS102](ws102/ws.md) | スクリーンキーボード | incomplete。p010・p026 in-progress、p011〜p014 planned | T1 の再試験待ち（p026）と p010 の基準値、p012 は WS095 の続き、p013 は WS085 | ベータ2 |
| [WS113](ws113/ws.md) | 外部・複数 display と Settings の Display | incomplete。p012・p013 in-progress、p014 test-wait、p008・p009・p010・p011a が残り | BUG-266、実機の確かめ（5330 の全経路 p008）、蓋 p011a と規約 p009 はベータ3、p010 は後回し | ベータ2 |
| [WS129](ws129/ws.md) | ベータ2 の release 作業 | incomplete。p002 planned、p005・p013 は下書き済み、p006・p007・p008 planning | ユーザーの review（release notes・既知の問題・手引き）、最終回帰 p006 は 10/14、公開はユーザーの指示 | ベータ2 |
| [WS130](ws130/ws.md) | IPv6 | ベータ2 の範囲は完了（2026-10-13、UAT はユーザー OK） | なし（残りは規約＝ベータ3） | ベータ2 |
| [WS131](ws131/ws.md) | libkeiland-backend と libkeiland の分解 | incomplete。p002 in-progress（D7 の確認）、p024 planning | ユーザーの判断 D7 の確認、規約はベータ3、Linux・FreeBSD の回帰は 10/13 以降 | ベータ2 |
| [WS132](ws132/ws.md) | /dev/system の電源・PnP・自動 mount | incomplete。p001 は Q1 の判定、p006・p007・p008 が残り | 実機の確かめ（蓋の事象 p008、UAT p007）、蓋の自動の切り替えはベータ3 | ベータ2（旧ベータ1 の段） |
| [WS134](ws134/ws.md) | System Monitor | incomplete。p003 uncleared、p009・p010 in-progress | T1 の試験（p009）、実機の確かめ（i915 の値・デモ）、規約はベータ3 | ベータ2（旧ベータ1 の段） |
| [WS143](ws143/ws.md) | Bluetooth（HID・pairing・Settings） | incomplete。p003・p005 in-progress、p006 test-wait、p007〜p009 planned | T1 の再試験（T1-446・bt-hid-p005）、実機の確かめ（5330 の UAT p008）、Linux は 10/13 以降、規約はベータ3 | ベータ2 |
| [WS145](ws145/ws.md) | 印刷 | incomplete。p001〜p004・p007 cleared、p005・p006 planning | Linux・FreeBSD の guest は 10/13 以降、規約はベータ3 | ベータ2 |
| [WS154](ws154/ws.md) | Languages の頁の IME の選択と SKK の IME | ベータ2 の範囲は完了（2026-10-13、UAT はユーザー OK） | なし（残りは規約＝ベータ3） | ベータ2 |
| [WS156](ws156/ws.md) | app の通知（下の中央の popup と ring の log） | incomplete（p001〜p004 cleared、p005 uncleared、p006 planning） | 実機の確かめ（5330 の UAT p005）、規約はベータ3、p006（Linux の D-Bus）は後回し | ベータ2 |
| [WS159](ws159/ws.md) | native の touchpad（I2C-HID）と touchpad の層 | ベータ2 の範囲は完了（2026-10-13、UAT はユーザー OK） | なし（残りは規約＝ベータ3） | ベータ2（表はベータ1、ベータ1 はベータ2 に一本化） |
| [WS161](ws161/ws.md) | YubiKey（USB の FIDO2・NFC の CTAP2） | incomplete。p001〜p005 cleared、p006 は実機の UAT OK（2026-10-13 ユーザーの実機の確かめ OK、YubiKey）、p007 規約 | Linux・FreeBSD の build（p006 の残り）、規約 p007 はベータ3 | ベータ2 |
| [WS164](ws164/ws.md) | 起動時の Welcome の画面 | ベータ2 の範囲は完了（2026-10-13、UAT はユーザー OK） | なし（残りは規約＝ベータ3） | ベータ2 |
| [WS165](ws165/ws.md) | 手書きの入力（段 1） | ベータ2 の範囲は完了（2026-10-13、UAT はユーザー OK） | なし（残りは規約＝ベータ3） | ベータ2 |
| [WS169](ws169/ws.md) | メーラの app と compositor のメールの API | incomplete（p000〜p005 cleared、p006 は作らない、p007 規約） | 規約 p007 はベータ3（OAuth2 の p006 はユーザーの決定で外した） | ベータ2 |
| [WS170](ws170/ws.md) | Phone の app（連絡先とタイムライン） | ベータ2 の範囲は完了（2026-10-13、UAT はユーザー OK） | なし（残りは規約＝ベータ3） | ベータ2 |
| [WS173](ws173/ws.md) | AAT（エージェントが実機を操作する受け入れの枠組み） | incomplete（p001〜p004・p006 cleared、p005 planning） | 素の 5330 の起動の方法（ユーザーの判断） | ベータ2 |
| [WS174](ws174/ws.md) | 起動時の Ctrl・Shift で boot の選択を変える | incomplete（p001〜p003・p005 cleared、p004 BIOS は案） | ユーザーの指示で BIOS（p004）は後日、実機（UEFI の 5330）は UAT | ベータ2（表はベータ1、ベータ1 はベータ2 に一本化） |
| [WS175](ws175/ws.md) | Notes で PDF の画像と文字を編集 | incomplete（p002 判定待ち、p010 in-progress、p011 規約は未作成） | T1 の再実行（p010 の AAT）、p002 は Q1 の判定 | ベータ2 |
| [WS177](ws177/ws.md) | ベータ2 積み残し（準正常系・異常系） | incomplete（p019 以外は cleared） | p019 の残り（Browser の IME (b)(d)）はベータ3 に回した | ベータ2 |
| [WS181](ws181/ws.md) | 窓の状態・App Home のモード・整列のモード | incomplete（p001〜p010 cleared、p011・p012 in-progress） | p011 は PNG を見たユーザーの返事待ち、p012 は実機の感触の UAT（T1-559・T1-560 PASS） | ベータ2 |
| [WS189](ws189/ws.md) | app の間の drag and drop | incomplete（p001・p004 cleared、p002・p003 test-wait） | T1 の残りの項目（across-displays・pdfviewer の drag-out・photo-to-notes 3〜4 など） | ベータ2 |
| [WS190](ws190/ws.md) | 文字の欄と Text Editor の指での選択 | incomplete（p001〜p003 cleared、p004 規約 planned） | 規約 p004 はベータ3 | ベータ2 |
| [WS191](ws191/ws.md) | 再生の音を libkeiland の audio stream の口へ | incomplete（p002〜p004 cleared、p001 設計は planned のまま、p005 規約） | 規約 p005 はベータ3、p001 の記録が planned のまま | ベータ2 |
| [WS193](ws193/ws.md) | menuconfig の階層の作り直しと Build boot image | incomplete（p001・p002・p004〜p011 cleared、p003 規約 planning） | RPi4 の host key・SSH の確かめと WS の受け入れの照合、規約 p003 が未着手 | ベータ2 |
| [WS194](ws194/ws.md) | keiland-linux・keiland-freebsd の package の確認と導入 | planned と記録（p001・p002 cleared、p003 規約 planning） | 規約の全文の見直し p003 が未着手 | ベータ2 |
| [WS197](ws197/ws.md) | Bluetooth のスマホ連携（SMS・連絡先・通話） | incomplete（p006・p010 in-progress、p007 planning、p008・p014 planned、p009 規約） | ユーザーの判断（HFP をベータ2 に入れるか）、UAT（beta2 の #12・#13）、実機 p008（Android）、規約 p009 はベータ3 | ベータ2（HFP は未決） |
| [WS202](ws202/ws.md) | libmedia だけで mp4・m4a を再生 | incomplete（p013 UAT OK、p012 planned、p014 planned、他の code の Phase は uncleared） | T1 の全項目（p012）、規約 p014、BUG-300（スライダー） | ベータ2 |
| [WS225](ws225/ws.md) | Vulkan の header を仕様だけで書き直す | planning（blocked、p001 planned） | ユーザーの指示で着手しない（WS226 の後） | ベータ2（blocked） |
| [WS004](ws004/ws.md) | ハードウェアの拡張（USB・WLAN・NVMe・PCI） | incomplete。p021・p038 uncleared、p025 planned（実機）、p034 test-wait、p035・p039 planned | 実機の確かめ（5330 の SN740、p025）、T1 の試験（p034）、Queue なし | ベータ2（5330 の NVMe に書き込んでよい（2026-10-13 ユーザー）） |
| [WS009](ws009/ws.md) | 製品の文書（GPU の文書ほか） | incomplete。p001〜p008 済み、DOC-54 は Phase 未定義 | GPU の文書の保留の解除待ち（DOC-54） | ベータ2（2026-10-13 ユーザー） |
| [WS046](ws046/ws.md) | GNU 互換の make | incomplete。p011・p014 uncleared、p005（規約と回帰）planning | なし（着手待ち、p014 の当て直しから、Queue なし） | ベータ2（2026-10-13 ユーザー） |
| [WS095](ws095/ws.md) | 日本語 IME | incomplete。p001〜p005・p012 cleared、p006・p007 は T1 待ち、p009〜p011 planning | T1 の試験待ち（p006・p007）、p009 は WS074 の担当次第 | ベータ2（優先度低め（2026-10-13 ユーザー）） |
| [WS155](ws155/ws.md) | カレンダーの app（予定・メモ） | ベータ2 の範囲は完了（2026-10-13、UAT はユーザー OK） | なし（残りは規約＝ベータ3） | ベータ2（UAT OK（2026-10-13 ユーザー）） |
| [WS157](ws157/ws.md) | 写真の管理の app（Photos） | incomplete（p011〜p014・p016 in-progress、p015・p017 planned） | T1 の試験待ち（p013・p014・p016）、p016 は main に未統合、実機の GUI の UAT は未実施 | ベータ2（UAT OK（2026-10-13 ユーザー）） |
| [WS171](ws171/ws.md) | hal.h の全関数に契約の comment | planning（p001 cleared、p002 差分の案・p003 適用） | 担当が未割当、p002 はユーザーの review が要る | ベータ2（優先度低め、やることが無い時に入れる（2026-10-13 ユーザー）） |
| [WS172](ws172/ws.md) | passkey の認証の枠組み（/sbin/passkey） | incomplete（p002・p003 cleared、p007 test-wait、p004〜p006 planning） | T1 の試験（p007 の再依頼）、p001 は phase.md で判断 P1〜P10 待ちのまま（ws.md は cleared と記録） | ベータ2（2026-10-13 ユーザー） |
| [WS196](ws196/ws.md) | useradd・usermod・userdel | planning（p001 範囲の設計） | ユーザーの判断 E1〜E6、design-reviewer、WS195 p001 | ベータ2（2026-10-13 ユーザー） |
| [WS074](ws074/ws.md) | Web ブラウザ（libbrowser と shell） | incomplete。p100 ほか多数が planned、待機 | Codex のセッションが main の checkout で作業中（ユーザーの依頼） | ベータ2（2026-10-13 ユーザー「Codex のセッションは、私が WS074 の作業を頼んだ物です。これもベータ2までに完了予定です。」） |
| [WS217](ws217/ws.md) | 画面の回転（Settings の Display） | planning（p001 設計中、p002a 実装済み、p002b 以降は案） | T1-576 の (B)（p002a）、p002b 以降は設計の review の後 | ベータ2（2026-10-13 ユーザー「WS217 p003（入力の座標）を続ける」） |

### 区分が記録に無い物（今日の Pi 4・網・描画・license などの新しい WS が中心）（24 件）

| WS | 概要 | 状態 | 止めている物 | 区分 |
| --- | --- | --- | --- | --- |
| [WS082](ws082/ws.md) | /dev/kvm の移植の検討 | planning。p001 cleared、以降は未計画 | ユーザーの判断（study.md §10 の 11 項目） | 不明（アイディアの Phase） |
| [WS123](ws123/ws.md) | VA-API | キャンセル（2026-10-02 ユーザー） | なし（キャンセル済み） | 不明 |
| [WS141](ws141/ws.md) | Raspberry Pi 4 の VC6 GPU（表示・V3D・Vulkan） | incomplete。p001〜p006・p013・p014 in-progress、p015〜p019 planning | 実機の確かめ（Pi 4、電源の入れ直しはユーザー）、P3 が担当 | 不明（Target はベータ4 以降、beta2.md は進行中） |
| [WS162](ws162/ws.md) | FIDO2 の login（WS172 に吸収） | incomplete（p001〜p004 canceled、以後実行しない） | なし（WS172 p003 へ移した） | 不明（WS172 に吸収） |
| [WS203](ws203/ws.md) | Pi 4 の内蔵 Ethernet（GENET） | incomplete（p001・p002・p004 cleared、p003 uncleared） | 実機の確かめ（Pi 4 の DHCP の由来・抜き差し） | 不明 |
| [WS204](ws204/ws.md) | HD Audio の録音と HFP 通話の音 | incomplete（p001・p002 in-progress、p003〜p005 planned） | ユーザーの判断 U1・U2 とベータ2 に入れるかの判断 | 不明（判断待ち） |
| [WS205](ws205/ws.md) | Bluetooth audio（A2DP と headset の HFP） | planning（p001〜p003 in-progress、p004〜p011 planned） | ユーザーの判断 D1（試験の headset）とベータ2 に入れるかの判断 | 不明（判断待ち） |
| [WS206](ws206/ws.md) | Pi 4 の WiFi（BCM43455、SDIO） | incomplete（p003 cleared、p001・p002a・p002b・p004・p005・p008 in-progress、p006・p007 planned） | T1-575（p005）実行中、T1 と Pi 4 の実機（p002a・p002b・p004）、ユーザーの判断 E2（試験の AP） | 不明 |
| [WS207](ws207/ws.md) | Pi 4 の音（再生と録音） | planning（p001・p002 in-progress、p003〜p005 planned） | T1 と Pi 4 の実機（p002）、ユーザーの判断 F2（録音の機種） | 不明 |
| [WS208](ws208/ws.md) | arm64 の HAL の SMP（Pi 4 の 4 core） | incomplete（p001〜p004・p007 in-progress、p005・p006 planning） | Pi 4 の実機（p007 の固まりの再現）、T1（p002・p004）、p003 の T1-537 一部 FAIL の Q1 の判定 | 不明 |
| [WS209](ws209/ws.md) | audiod の音の endpoint と route | planning（p001〜p003 in-progress、p004〜p006 planned） | p001 の再 review、call の speaker・mic の QEMU・5330 の確かめ、p004 は WS191 の後 | 不明 |
| [WS210](ws210/ws.md) | Keiland の app の描画を GPU へ | planning（p001・p002・p014 in-progress、p013 planning、p003〜p012 planned） | p001 は Q1 の確認、p002・p013 は Pi 4 の実測 | 不明 |
| [WS211](ws211/ws.md) | Pi 4 の compositor の描画の削減 | planning（p003・p005〜p011 実装済みで Pi 待ち、p001 第 4 版、p002・p004 planned） | Pi 4 の実機の確かめ | 不明 |
| [WS212](ws212/ws.md) | Wiseview の作り直し（仮想デスクトップ・display） | planning（p001〜p005 planned） | 担当が未割当（ユーザーが着手を決める） | 不明 |
| [WS213](ws213/ws.md) | compositor の thread 化（入力・cursor と描画） | planning（p001 第 6 版、p002〜p005・p007 in-progress、p006 planned） | T1 と Pi 4 の確かめ（p002〜p005） | 不明 |
| [WS214](ws214/ws.md) | 窓を HVS の plane に載せる（Pi 4） | planning（p001 第 3 版、p003a・p003b 実装済み、p005・p006 は案、p002 planned） | Pi 4 と T1 の確かめ（p003a・p003b）、p005 は WS213 p004 とユーザーの判断（glass の blur） | 不明 |
| [WS215](ws215/ws.md) | Pi 4 の Bluetooth（BCM43455） | incomplete（p001〜p005 in-progress、p006〜p008 planned） | Pi 4 の実機（p002・p003・p005）、ユーザーの判断 D5 | 不明 |
| [WS216](ws216/ws.md) | Notes の描く道具（筆・鉛筆・塗りつぶし・水彩） | planning（p003 cleared、p004 test-wait、p005 実装中（P1）、p001 review-2 待ち、他は planned） | T1 の試験（画は p007）、p009 は WS141 p017 | 不明 |
| [WS218](ws218/ws.md) | Intel WiFi driver の license の表示を実態に合わせる | planning（p001 planned と記録、p002 in-progress） | T1-573 PASS の Q1 の判定、実機の AX211 の回帰、削除 3 path は Q1 | 不明 |
| [WS219](ws219/ws.md) | Venus の op の流用の整理（出典の明記） | incomplete（p003 cleared、p001・p002 in-progress） | T1-572 PASS の Q1 の判定（p002） | 不明 |
| [WS220](ws220/ws.md) | TCP の転送の速さの安定（受けの window） | incomplete（p001・p002 in-progress、p003 実装済み、p004〜p009 案） | T1-574（基線）実行中、p003 は review と T1 の測り | 不明 |
| [WS221](ws221/ws.md) | Jumbo frame（USB LAN で MTU 9000） | incomplete（p001 第 2 版、p002〜p007 案） | ユーザーの判断 §9 Q1〜Q13、review-2、p005 は WS220 p006・p007 | 不明 |
| [WS223](ws223/ws.md) | 全画面は合成、game mode は拡張の時だけ | planning（p001 第 1 版、p002〜p005 planned） | design-reviewer の後の第 2 版、ユーザーの判断 D1〜D6 | 不明 |
| [WS226](ws226/ws.md) | Vulkan 1.3 の header と libvulkan の 1.3 の実装 | planning（p001 第 1 版） | design-reviewer の review-1、担当は次に空いた P | 不明 |

### ベータ3 以降の物（31 件）

| WS | 概要 | 状態 | 止めている物 | 区分 |
| --- | --- | --- | --- | --- |
| [WS001](ws001/ws.md) | POSIX.1-2024 の utility・header の適合 | incomplete。p041〜p043 in-progress、p044 test-wait、p045 planned、p046〜p051 planning | T1 の guest 試験待ち（p041〜p044）、p045 は 10/17 の後に merge（Q1 の判定）、p046〜p051 は担当が未割当 | ベータ3 以降 |
| [WS026](ws026/ws.md) | 試験の資産の整理 | planning。Phase 未定義 | なし（着手待ち、Phase 未定義） | ベータ3 以降 |
| [WS037](ws037/ws.md) | NVIDIA RTX（nvrtx）の driver | planning。p001 in-progress、p002〜p009 planned・planning | ユーザーの指示で要検討・ブロック、試験機の host の情報待ち（p002） | ベータ3 以降（Target はベータ4 以降） |
| [WS038](ws038/ws.md) | Intel Arc dGPU の driver | planning。番号の予約だけ、Phase なし | ユーザーの指示で要検討・ブロック | ベータ3 以降（Target はベータ4 以降） |
| [WS039](ws039/ws.md) | AMD RDNA GPU の driver | planning。番号の予約だけ、Phase なし | ユーザーの指示で要検討・ブロック | ベータ3 以降（Target はベータ4 以降） |
| [WS052](ws052/ws.md) | 電源管理（S0i3 の sleep） | incomplete。p001・p002・p005・p006・p009 in-progress、p010〜p012 test-wait、p007 planning、p008 planned | T1 の QEMU の試験と実機の確かめ（5330 の UAT）、設計の review（p001・p007） | ベータ3 以降 |
| [WS068](ws068/ws.md) | EGL・OpenGL ES・desktop GL | incomplete。p004・p009 in-progress、p007・p039 ほか planning・planned、GL 3.3 以降（p034〜p037）は保留 | GL 3.3 以降はユーザーの指示で保留、p009 は T1-180 の計測待ち | ベータ3 以降 |
| [WS080](ws080/ws.md) | PE/COFF の動的ローダ ld.coff | planning。p001 の設計まで、p002〜p012 planning | ユーザーの指示で要検討・ブロック、ユーザーの判断 D1〜D17 と HAL の差分の承認（A1・A2） | ベータ3 以降（Target はベータ4 以降） |
| [WS101](ws101/ws.md) | GPU の compute（Noct の自動並列化） | incomplete。L1 で区切り、p012・p013・p018 planned、p016・p017 は優先を下げた | ユーザーの指示で最適化の優先を下げた（再開の指示待ち） | ベータ3 以降 |
| [WS106](ws106/ws.md) | test app を userland/tests へ集約 | incomplete。p002 は cleared 候補、p003 planning | なし（着手待ち） | ベータ3 以降 |
| [WS112](ws112/ws.md) | Debian・Ubuntu・RPi OS の deb | incomplete。p002・p003 cleared、p007（規約）planned | 規約の全文の見直し p007 が未着手 | ベータ3 以降（残りは規約） |
| [WS115](ws115/ws.md) | upstream GTK4 の移植 | planning。p010 in-progress（中断）、p003 planning | rtld の上限で GTK が起動しない（p010 の再開点） | ベータ3 以降 |
| [WS116](ws116/ws.md) | upstream Qt6 の移植 | planning。p001〜p005 planning | 他の WS の依存（WS117 p002・WS115 p008） | ベータ3 以降 |
| [WS117](ws117/ws.md) | Linux の標準 Qt6 の互換 | planned。p001 planned、p002〜p006 planning | ユーザーの判断（p002 の行別の採否）、p001 は WS114 p007 の後 | ベータ3 以降 |
| [WS119](ws119/ws.md) | インストーラの作り直し | planning。p001 planned、p002〜p006 planning | アイディアの Phase の後のユーザーとの議論と要件の判断 | ベータ3 以降（Target はベータ4 以降） |
| [WS121](ws121/ws.md) | browser の動画の再生 | incomplete。p001・p002・p004〜p006 cleared、p003（Range）planning | browser（WS074）と一緒にベータ3 へ（Q1 の判定） | ベータ3 以降 |
| [WS124](ws124/ws.md) | Emacs（端末版）の package | planning。p001・p002 planned、p003〜p006 planning | 担当が未割当、p004 は ws125-p002 の後 | ベータ3 以降（Target はベータ4 以降） |
| [WS125](ws125/ws.md) | vim の package | planning。p002 test-wait、p001 planned、p003・p004 planning | T1 の試験（ws126-p005 の image の boot） | ベータ3 以降（Target はベータ4 以降） |
| [WS126](ws126/ws.md) | Python 3（core）の package | planning。p002・p003 cleared、p005 test-wait、p004 は行わない、p006 planning | T1 の試験（p005） | ベータ3 以降 |
| [WS144](ws144/ws.md) | VPN | planning。p001 planning | アイディアの Phase の後のユーザーとの議論 | ベータ3 以降（Target はベータ4 以降） |
| [WS146](ws146/ws.md) | SSH の独自のオンラインストレージ | planning。p001 planning | 時期の決定、WS150 の後 | ベータ3 以降（Target はベータ4 以降） |
| [WS147](ws147/ws.md) | OneDrive の連携 | planning。p001 planning | 時期の決定、WS150 の後 | ベータ3 以降（Target はベータ4 以降） |
| [WS152](ws152/ws.md) | system の更新（Settings の Updates の頁の実体） | planning（p001 方式の検討の第 2 版） | ユーザーの判断 U1〜U12 | ベータ3 以降 |
| [WS158](ws158/ws.md) | Keiland と app の翻訳（日本語） | incomplete（p001 cleared、p002〜p005 in-progress・test-wait、p006 planning） | T1 の試験（p003・p004）、ユーザーの日本語の review（p005） | ベータ3 以降 |
| [WS176](ws176/ws.md) | Canvas（pen のイラストと画像の編集） | planning（p001 planning） | ユーザーの決定でベータ3 | ベータ3 以降 |
| [WS180](ws180/ws.md) | Emacs ベースのグラフィカルなエディタ | planning（Phase 未定） | ユーザーの決定でベータ3 以降（WS だけ作成） | ベータ3 以降 |
| [WS184](ws184/ws.md) | 左手デバイスの OSK（クリエイターモード） | planning（p001・p002 planned） | ユーザーの決定でベータ3 | ベータ3 以降 |
| [WS185](ws185/ws.md) | ゲームパッドの OSK とゲームコンソールモード | planning（p001〜p003 planned） | ユーザーの決定でベータ3 | ベータ3 以降 |
| [WS186](ws186/ws.md) | Realtek RTL8822CE の WiFi（5320） | planning（p001 planned） | ユーザーの決定でベータ3（ベータ2 が早く終われば前倒し） | ベータ3 以降 |
| [WS198](ws198/ws.md) | zedBSD の上で zedBSD を self-build | planning（p001 cleared 候補） | ユーザーの決定でベータ3、p001 は Q1 の判定 | ベータ3 以降 |
| [WS201](ws201/ws.md) | /home の暗号化（FIDO2 の PRF と key slot） | planning（p001 設計） | ユーザーの指示で今は検討しない | ベータ3 以降 |

### 保留（ユーザーの指示・判断待ち・Future Work）（5 件）

| WS | 概要 | 状態 | 止めている物 | 区分 |
| --- | --- | --- | --- | --- |
| [WS013](ws013/ws.md) | CPAR（kernel・root・app を file で選ぶ区画） | incomplete。boot の基盤 p002〜p006 済み、残りは F-002 | Future Work F-002 に保留（昇格まで再開しない） | 保留 |
| [WS098](ws098/ws.md) | IME のニューラル化 | planning。p001（設計）planning | ユーザーの指示で要検討・ブロック、WS095 の基本の辞書 | 保留（要検討・ブロック） |
| [WS150](ws150/ws.md) | FUSE に当たる枠組み | planning。p001 planning | ユーザーの指示で要検討・ブロック | 保留（要検討・ブロック） |
| [WS153](ws153/ws.md) | Settings の Apps の頁と third-party の app の repository | planning（p001 設計の第 4.1 版、p002 以降は未定） | ユーザーの判断 U2〜U15（ユーザーが検討中、Q1 は聞かない） | 保留 |
| [WS222](ws222/ws.md) | 画像・PDF・字形の library を Desktop へ戻す | planned（p001 planned） | ユーザーの指示で今の作業が全部終わってから | 保留 |

## 運用

- 凍結の目標の 10/13 の後も、ベータなので UAT の Bug の直しは 10/16 の準備に間に合う範囲で続ける。新しい仕様の変更は「ベータ3 に回すか」を Q1 がユーザーに聞く。
- Q1 は進むたびにこの表を更新し、完了した項目を消す（2026-10-09 ユーザー「都度、beta2.mdを更新していただけると、進捗がわかって助かります」）。

## ユーザーの実機の UAT を通っていない項目（2026-10-14 夕 Q1、調べた agent の報告の要約）

最後の 5330 の UAT の image は b67a6d7e1（10/11）。それより後の変更は 5330 の UAT を通っていない。10/12 以降のユーザーの UAT 済みは BUG-303（13400F）・BUG-309（Ra43）・WS202 p013・WS233（2026-10-14「ユーザの方で確認できました。CIが通ればOK」）と 10/13 の一括の判断の物だけ。

| 区分 | 項目 | 状態 |
| --- | --- | --- |
| 既定の driver・kernel | WS229 Gen9（既定 on） | SV7 の passthrough の T1 だけ。素の USB 起動・6 世代は未 |
| | WS231 AHCI（既定 y） | QEMU だけ。実機の SATA・RAID mode の ID は未 |
| | BUG-303 TSC の較正 | 13400F は UAT 済み、5330 は未 |
| | BUG-304・305 flush・shutdown、BUG-307 fsync | QEMU だけ |
| | WS218 AX211 の改名・MSI-X、WS220 TCP、WS228 p004 VT の CSI | QEMU だけ |
| | VBT の削除 | 5330 の passthrough の試験だけに影響、素の起動は影響なしの見込み |
| desktop | WS074 p229 ブラウザの titlebar | T1-618 QEMU PASS（p010 の回帰 FAIL を P2 が調査中） |
| | WS217 回転・BUG-306 | QEMU だけ。release notes の「Settings has no control」は古い（P1 が直す） |
| | WS202 p012・BUG-300、WS216 Notes の道具、WS157 Photos の decode、BUG-302 SKK、WS196 useradd、WS228 vi、WS227・232 menuconfig | QEMU だけ（または T1 の差し替えだけ） |

リリースの前にユーザーが見るとよい上位: (1) 5330 で最終 image を素に起動（TSC・shutdown・保存・WiFi・TCP をまとめて）、(2) SV7 など 7〜10 世代で USB から素に起動（Gen9・AHCI）、(3) ブラウザ（google.com、アドレス欄、長押し）、(4) 5330 の回転（touch・ペンの向き、Settings が消えない）、(5) SKK と Video Player のスライダー。
