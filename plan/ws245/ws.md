<!-- awesome-plan project=zedbsd record=ws245 -->

# WS245: i915 の Gen12 系（display 12・13）の display の書き直し

<!-- awesome-plan-current:start -->
Status: incomplete（2026-10-15 作成。p001 の [design.md](design.md) は改訂 6 で cleared（2026-10-16 Q1）。q1218（P1、2026-10-16）で段の実装を開始: p047（証拠の道具）cleared、p016（OpRegion）・p017（VBT の解析）は 5330 の素の起動の回帰に合格し main に merge（p016）/ merge の依頼済み（p017））
Primary Milestone: MG006
Related Milestones: MG002
Parent: [Master](../master.md)
Related: [WS244](../ws244/ws.md)（i915 の Linux 由来の code の整理、棚卸し plan/ws244/inventory.md・tests/inventory.py）、[Guardrail](../guardrail.md)「MIT・GPL の出典のデータの引用」
Resume point: 2026-10-16 16:40 q1224（P1）の区切り: p047・p016・p017・p018 は cleared（main）。**p053**（EDID の decode）は実装済み（tip 918265e98）で、5330 の書き込みの全部の列の比べを T1 に依頼中（phase053、手順は `plan/ws245/tests/5330-bootcycle.sh`、2026-10-16 ユーザー「P1のテストはT1にオフロード」）。次は **p019**（AUX の transaction、xd-aux。[phase019](phase019/phase.md) に aux.c の継ぎ目と保つ振る舞いを調べてある。code は未着手）。5330 の回帰は段ごとに T1 に依頼する（kernel を `~/ws245/` に置き、基準の kernel を親の main の source から build、`5330-bootcycle.sh` の 2 回（digest が違えば `DEBUG=1`）、`5330-survey-diff.py` で判定）。USB の image は 33cde7ac9（main d454d4bd3 以降）、UEFI の entry は Boot0008（image を dd し直すたびに PARTUUID が変わるので `efibootmgr --create-only`）。
<!-- awesome-plan-current:end -->

## ユーザーの指示（原文、2026-10-15）

「Gen12（5330）のディスプレイは書き直します。WSを立ててください。」
（前提の決定: 「code・コメント・構造:については、MITであっても写してはいけません。」「範囲を全部の WS に広げます。」「ガードレイルに、ファイル上部には参考にしたことや引用したことを一切書かず、該当箇所にブロック単位で記載することを明記してください。」）

## 対象（WS244 の棚卸しの「Gen12 の display の Linux 環境」の群、約 6 万行）

`src/drivers/gpu/i915/display/` の ddi・dp・dp-sink・dp-internal.h・pipe・plane・clock・watermark・vbt・vbt.h・panel・panel-backlight・power・dmc・takeover・takeover-internal.h・hotplug・hotplug-internal.h・modeset・modeset-internal.h・internal.h・opregion・opregion-internal.h・aux・gmbus・hdmi・hdmi-mode・edid・edid-read・color・vblank・phy・state・diagnostics・dp-ext・dp-ext-kern・tc・dkl-phy。どれも「Derived from the Linux kernel v6.8.12」と Linux の型・関数の分け方・comment を持つ（ddi.c 107/111、dp.c 92/92、pipe.c 74/74、plane.c 43/44 の関数が写し）。intel/ の header のうち Gen12 だけが使う struct の配置（vbt-defs.h・ref.h・connector.h・wm.h）も含める。

## ユーザーの決定（2026-10-15、design.md のレビューの後）

「5は、(1) a、(2) 5330のみ、(3) Linuxのコードを参考にする、(4) Linuxを優先、でおねがいします。」
1. 逃げ道の形は (a): 口（xd.h）を最初の方の Phase で決め（design.md 改訂 2 では p003）、own の file は最初からその口だけを呼ぶ。口の実装を旧（薄い adapter、旧 code を消す Phase で削除。design.md 改訂 2 では p012）と xd で丸ごと切り替える（状態を混ぜない）。
2. 「5330のみ」（意味は未確認。下の対象の方針が優先）。Q1 は当初「Tiger Lake（display 12）は支えない」と記録したが誤り（2026-10-15 ユーザーの補足「Tiger Lakeは実機で動作確認済みです。display 10,11以外は動作確認済みです。」「記録が食い違っているだけです。」）。display 9・12・13 は実機で動いている機能で落とさない。xd は display 12（Tiger Lake、5320）と display 13（ADL-P、5330）の両方を持つ。display 10・11 は今も支えていない（範囲外）。
3. 手順は Linux の code を参考にする（Tiger Lake・Alchemist の PRM は補助）。guardrail のまま、code・comment・構造は写さず、値は block ごとに Value(s) referenced:、参考にした手順は Procedure referenced: <版> <path> (<関数>) を付ける。
4. PRM と Linux（5330 で動いている実績）の値・手順が食い違う時は Linux を優先する。

## 対象の機種（2026-10-15 ユーザー「すでに多数の機種で動作しており、5330を特別扱いはしません。Skylake～Raptor Lake Refreshまですべてサポートします。」）

- WS245 は 5330 を特別扱いしない。対象は今の Gen12 の code が動かしている全機種: display 12（Tiger Lake・Rocket Lake・Alder Lake-S・Raptor Lake-S/HX と Raptor Lake Refresh のデスクトップ）と display 13（Alder Lake-P/U・Raptor Lake-P/U）。display 9（Skylake〜Comet Lake）は gen9-*.c（WS244）。
- display 9・12・13 は実機で動作確認済み（ユーザー「display 10,11以外は動作確認済みです。」）。display 11（Ice Lake）と display 10 は今の code で支えていない。Ice Lake（display 11）は、この WS の最後の方の Phase で新しく支える（2026-10-15 ユーザー「display 11は、i915のリファクタリングの既存WSに最後の方のphaseとして入れてください。」）。display 10 は範囲外。
- 下の「目的と完了の条件」の「5330」の記述（2026-10-15 の作成時）は、この方針で読み替える。実機の確かめは 5330 に限らない。

## 目的と完了の条件

- Gen12 の display を、Intel の公開の PRM（Gen12、ADL-P）と hardware の振る舞いから設計し直して書く。Linux の code・comment・構造（関数の分け方、型、制御の流れ、名前）を写さず、行ごと・関数ごとの言い換えもしない。
- register の offset・bit・表（DDI の buf trans、PLL・clock の表など）はデータとして独自の名前で残し、block ごとに「Value(s) referenced: <版> <path> (<名前>)」を付ける。file の上部には出典を書かない。
- 外向きの口（display の API、Gen9 と共有の部分）は保つ。
- 書き直しの後、`Derived from the Linux kernel` と Linux の MIT の表示は Gen12 の file から消え、`userland/base/licenses/i915-driver/` は WS244 の最後で整理する。
- 完了（対象の機種の方針で読み替え: 5330 だけでなく display 12・13 の対象機種で）: 5330 の実機で、起動・GOP の引き継ぎ・内蔵 panel（eDP）・backlight・外部の DP/HDMI（USB-C を含む）・hotplug・mode set・suspend の戻りが今と同じに動く（ユーザーの確かめ、試験の一覧に載せる）。

## 実装の開始とロールバックの体制（2026-10-15 夕）

ユーザー:「WS245は、ロールバックできる体制にしておいて、今から実装します。」
- ロールバックの基点: main 0c25c87a7（WS245 の実装の commit が入る前）。Phase ごとに別の merge commit にするので、`git revert -m 1 <merge>` で Phase 単位に戻せる。
- kernel の振る舞いは p009 まで変えない: p002・p004〜p008 は新しい file と host 試験だけ。p003 の adapter 以降も `i915.display=old` が既定で、xd は opt-in（design.md §7 #0、既定の切り替えは p011）。`old` は p013 まで逃げ道。
- §7 の判断は既定の案で進める。#18（2026-10-15 ユーザー、クリック「今のまま（efifb）」）: RKL・ADL-S/RPL-S は device.c に足さず今の振る舞い（efifb）を保ち、xd の platform の表にだけ持つ。#22（2026-10-15 ユーザー、クリック）: Gen9 の実機は SV7（10.0.10.27）。
- merge の時期（§7 #13、2026-10-15 夕 ユーザー「WS245はいつでもロールバックできるので、今マージします」）: デモの後まで待たず、p003 以降も Phase の成果ができ次第 main に merge する（既定の案の「p003 以降はデモの後」を置き換え）。戻す時は Phase の merge commit を revert する。

## 進め方の改め（2026-10-15 夜 ユーザー、作業の即時中断の後）

ユーザー:「待ってください、書き換えが一度に大幅すぎます。すべての作業を即時中断してください。」に続けて「まず、Intel GPUのディスプレイコントローラについては、ゴールとしてはSkylakeからRaptor Lake Refreshまですべてで動くようにします。ただし、各ステップで5330で動くかどうかをきちんとリグレッションテストしながら、ステップで書き換えていきます。そうでないと破綻します。」
- design.md §6.2 の Phase の分け方（p002・p004〜p008 を host 試験だけで新しい file として作り、p009 で xd に切り替える）は、この方針に合わない。小さな段ごとに動いている道（5330）を書き換え、各段で 5330 の回帰の試験を通してから次へ進む形に、Phase の表を作り直す。
- 既に main に入った p002（土台）・p004（VBT・EDID）の新しい file（呼ばれない）と、止めた p006 の途中の差分（p1 の worktree、未 commit）の扱いは、作り直しの中で決める（ユーザーに確かめる）。
- 作り直すまで WS245 の実装は止める。

## 5330 の素の起動の確かめ（2026-10-15 夜）

- U10（`build/uat/ws245-amd64/hdd-image.img`、main 1716c14bc、p002・p004 の merge を含む）: 普通の起動で graphical の login・session・compositor・内蔵 LCD・backlight（brightness=100）、GPU の投入 2〜4.6 ms、device lost 0。ユーザー「問題なく使えてます。」→ 今の main は 5330 で動く（passthrough は VBT が無く panel が点かないので使わない、ユーザー「ベアメタルにしましょう」）。
- kernel の binary の比べ（Q1、sha256）: p002 の 2 つ目以降と p004 の xd の file は呼ばれず binary を変えない。binary を変えたのは p002 の 1 つ目（9e78e62f8、own の型の分離と Gen9 の移設）と AX211 の整理。
- 観測（未解析）: Ctrl+Shift の console の起動で `/bin/wayland` を ssh から root で手で起動すると、GPU の仕事が 1 回約 2 秒（GPU 自身は 16 ms）で時間切れ → device lost。普通の起動では出ない。最初の普通の起動（boot-000001、log が 0 byte）で backlight が消えて何も写らなかった原因は不明。

## 5330 の素の起動での段ごとの回帰の手順（2026-10-15 夜 Q1 が実施して確かめた形）

1. 段の kernel を build（`make -j16 ZEDBSD_CONFIG=config/ci/config-amd64.mk BUILD=… vmunix`）。userland を変えない段は kernel だけを差し替える。
2. 5330 が Linux（chaos、`ssh solaris10-man`）の時: `sudo mount /dev/sda2 /mnt/zb`（USB の ZEDBSD 区画）→ 前の `vmunix` を控えて段の kernel を `vmunix` に写す → umount → `sudo efibootmgr -n 0005`（USB の UEFI の entry、1 回だけ）→ `sudo systemctl reboot`。zedBSD の時は `ssh kei@10.0.30.3`（password kei、sudo も kei）で `/boot/boot0/` に写して `reboot`（再起動は既定の Linux に戻るので、続けて上の BootNext を使う）。
3. zedBSD に ssh で入り、`sysctl kern.boot.login`（graphical）、`/var/log/sessiond.log`（SESSION start・HANDOFF）、`dmesg` の i915 の行（`lease 1 claimed`・`picture up`（大文字小文字を区別しない grep）・`perf: … per submit`・`device lost` が 0）、backlight（`backlight-probe -f /dev/backlight/backlight0`、Q1 の `build/q1/blp/bin/backlight-probe`）を見る。LCD の目視はユーザー（頼まず、一覧に）。
4. 5330 の DHCP が取れない事がある（2026-10-15、ユーザーが再取得して回復）。ssh が届かない時は止まったと決めつけず、ユーザーに知らせる。
5. **書き込みの全部の列が要る時だけ**（design.md §6.3。survey 1 の digest が基準と違った段）: Linux から USB の EFI の区画を mount し、zedBSD の loader の設定（`zedbsd-native-uefi.cfg` から作られる 1 行 1 語の file。build の `ZEDBSD_BOOT_EXTRA_LINES` と同じ経路）を控えてから `i915.debug=display` の 1 行を足す。同じ語の行が既にあれば足さない。見終わったら控えに戻す。
6. **survey**（p047 から）: `plan/ws245/tests/5330-survey.sh <host> <記録の名前>` が ssh で dmesg・sysctl・sessiond.log・backlight-probe を取り `plan/ws245/tests/5330-<段>-<日付>.md` に写し、`5330-survey-diff.py <基準> <段>` が survey と digest の差と判定（PP_STATUS on、PWM、frame counter が 3 以上、SURFLIVE、CRC が 0 でない、underrun 0、TRANS_CONF）を出す。

## レビュー 4 への判断（2026-10-15 夜 ユーザー、クリック）

- 内蔵 LCD に映っているかの確かめ（レビュー 4 の B3）:「register などの証拠で代える」→ 各段の回帰で panel の電源（PP_STATUS）・backlight の PWM・frame counter・表示中の buffer（SURFLIVE）・pipe の CRC を ssh で読み、段の前後で比べる。目視は節目だけユーザーの一覧に。
- HDMI・USB-C DP の外部 monitor の常設（M7）:「常設できない」→ 外部出力は節目だけユーザーの一覧で確かめる。
- 段の kernel で止まった時（M8）:「その都度自分で入れ直す」→ 止まったら Q1 がユーザーに知らせる。

## §7 の回答（2026-10-15 夕 ユーザー、1 件ずつクリック）

| # | 回答 | 既定の案との差 |
| --- | --- | --- |
| 0 | 「もっと早く（p009 から）」: xd を既定にするのは p009 から | 変更（既定は p011 だった） |
| 1 | 「案のとおり（DVI mode）」 | — |
| 2 | 「案のとおり（変えない）」: mode set で CDCLK を変えない | — |
| 3 | 「案のとおり」: PSR off、DC6 は DMC の時、DC3CO 無し | — |
| 4 | 「案のとおり」: storm は rate の上限だけ | — |
| 5 | 「案のとおり」: OpRegion の runtime 無し | — |
| 6 | 「案のとおり（引用で可）」 | — |
| 7 | 「案のとおり（合成）」 | — |
| 8 | 「早く消す」: 旧 code と adapter は xd を既定にした時点（p009 以降）で早く消す | 変更（p013 まで残す案だった）。逃げ道は git の revert（「WS245はいつでもロールバックできる」） |
| 9 | 「案のとおり（支える）」: eDP 1.4 の rate の表 | — |
| 10 | 「案のとおり（置き換える）」: ktest-display を小さな ktest に | — |
| 11 | 「PRM を出典にも使う」: Intel の PRM の値を直に引用してよい（Notices の条項を受け入れる） | 変更（照合だけの案だった） |
| 12 | 「この WS で縮める」: display.h の段を own の段（start・serve・suspend・resume・stop）に WS245 の中で縮める | 変更（WS244 で、の案だった） |
| 13 | 「今マージします」（別に回答済み） | 変更 |
| 14 | 「案のとおり（支えない）」: ADL-P A0 | — |
| 15 | 「案のとおり（付け替えて残す）」: 5330 の passthrough の scenario 7 つ | — |
| 16 | 「案のとおり（立て直す）」: WS051・WS113・WS084 の display の Phase | — |
| 17 | 「案のとおり（入れる）」: TGL の TC port を p010 に | — |
| 18 | 「今のまま（efifb）」（別に回答済み） | — |
| 19 | 「案のとおり（支えない）」: RKL＋CMP | — |
| 20 | 「別の WS を今立てる」: RKL・ADL-S・ICL の GT は WS245 では起こさず、[WS252](../ws252/ws.md) を作った | 変更（別の WS を今作る） |
| 21 | 「全部 xd を既定に」: 実機で確かめていない platform も xd を既定にする（device.c が attach する物。RKL・ADL-S/RPL-S は #18 で attach しない） | 変更（opt-in の案だった） |
| 22 | 「SV7（10.0.10.27）」（別に回答済み） | — |

→ design.md の改訂 4 が要る（#0・#8・#11・#12・#21、Phase の表と p009〜p013 の受け入れ）。p001 はこの回答で判断が揃ったので、改訂 4 の後に cleared。

## Phase（design.md §6.2、改訂 4。p001 の clearance で確定）

改訂 3a の p003・p005〜p015 は、2026-10-15 夜のユーザーの方針（「進め方の改め」）で**段ごとの置き換え**の表（p016〜p046。改訂 5 で p047〜p052 を足し、表は実行の順）に置き換えて cancel した（disposition canceled。ID と履歴は残す。p006 は q1207 の attempt が途中で止まり uncleared: 差分は temp に保存、design.md §6.7）。各段の受け入れは「host 試験 + build warning 0 + 5330 の素の起動の回帰（上の手順 1〜3 と p047 の survey の基準との差）に合格 → main に merge」。内蔵 LCD は survey の register の証拠（レビュー 4 への判断）、目視・外部の出力・suspend は節目だけユーザーの一覧（design.md §6.1 R9）。

| ID | 内容 | 状態 |
| --- | --- | --- |
| [p001](phase001/phase.md) | 設計: display 11〜13 の model、外向きの口と契約、データの表の引用の方法、段の分割と順序、試験の道、LW。design-reviewer を通す（5 回） | cleared（2026-10-16 Q1、design.md 改訂 6） |
| [p002](phase002/phase.md) | 土台: register と表の header、xd-io、xd-platform、host の試験の枠と own の register の model、own の型の分離、Gen9 の own への移設 | cleared（2026-10-15、T1-699 PASS、U10 で 5330 の素の起動に合格） |
| p003 | （改訂 3a）口 `xd.h` と adapter、`i915.display=` | **canceled**（2026-10-15 夜、段ごとの置き換えに置き換え。adapter と boot の語は作らない: design.md §6.1 R5） |
| [p004](phase004/phase.md) | VBT（合成の fixture）・EDID の解析（呼ばれない部品） | cleared（2026-10-15、host 試験、main 2d5b167aa、U10 で 5330 に合格） |
| p005 | （改訂 3a）AUX・GMBUS | **canceled**（→ p019・p020） |
| p006 | （改訂 3a）power・DMC・clock・TC の register | **canceled**（→ p023〜p028・p040。q1207 の attempt は uncleared: 途中の差分は `temp/p006-partial-20261015.patch` と `temp/p006-partial-20261015-untracked/`、design.md §6.7） |
| p007 | （改訂 3a）DDI・DP・panel | **canceled**（→ p021・p022・p029〜p031） |
| p008 | （改訂 3a）pipe・DBUF | **canceled**（→ p032〜p035） |
| p009 | （改訂 3a）screen・takeover・xd の opt-in | **canceled**（→ p036〜p038） |
| p010 | （改訂 3a）hotplug・Type-C・外部 DP・HDMI・head | **canceled**（→ p039・p040） |
| p011 | （改訂 3a）suspend・既定の切り替え | **canceled**（→ p041。既定の切り替えは無い） |
| p012 | （改訂 3a）platform の網羅 | **canceled**（→ p044） |
| p013 | （改訂 3a）旧 code の削除と出典の監査 | **canceled**（→ 各段で消す + p043） |
| p014 | （改訂 3a）display 11 | **canceled**（→ p045） |
| p015 | （改訂 3a）規約の全文 | **canceled**（→ p046） |
| p047 | 回帰の証拠の道具: own の survey（PP_STATUS・PWM・frame counter・SURFLIVE・pipe CRC・underrun・TRANS_CONF などを picture up の後に dmesg へ）と `mmio.c` の書き込みの log（`i915.debug=display`）、`5330-survey.sh`（改訂 5、レビュー 4 の B3・M6） | cleared（4 LW。2026-10-16 main 3e1e0c687。LCD の目視は beta2.md の U11） |
| [p016](phase016/phase.md) | OpRegion の場所と map（xd-vbt の find、`struct i915_xd` と io・platform の用意）。opregion.c を消す | cleared（2026-10-16 5330 の回帰に合格、main 9244ac507、旧 file の削除 54fc334c5。SV7 は beta2 の U12） |
| [p017](phase017/phase.md) | VBT の解析（xd-vbt を真に、旧 parser と影の比較を 1 段） | cleared（2026-10-16 5330 の回帰に合格、main 45c83da79） |
| [p018](phase018/phase.md) | 旧 VBT parser の本体の削除（本番の呼び手 10 関数を殻 `vbt-shell.c` に、ktest-display の 2 file を削除）。2026-10-16 に p018 と p053 の 2 段に分けた | cleared（2026-10-16 5330 の回帰に合格、merge 8fea53a1d、旧 4 file は Q1 が削除） |
| [p053](phase053/phase.md) | EDID の decode を xd-edid に（`edid.c` の decode を消し、`state.c` の mode を xd-edid の first timing に。drm_modes の helper は edid.c に殻として残る）。p018 から分けた | in-progress（2 LW。2026-10-16 実装・host・build 済み、5330 は survey lit、書き込みの全部の列の比べを T1 に依頼） |
| [p019](phase019/phase.md) | AUX の transaction（xd-aux。well・DC off・VDD は呼ぶ側） | planned（4 LW。2026-10-16 q1225 で調べだけ、code は未着手: phase019 に再開点） |
| p020 | GMBUS と DDC の EDID の読み（xd-gmbus） | planned（3 LW。HDMI の plug はユーザーの一覧） |
| p021 | backlight の PWM（xd-panel） | planned（2 LW） |
| p022 | panel power（PPS、xd-panel） | planned（3 LW） |
| p023 | power well の操作（W1、xd-power。p006 の差分を当て直す） | planned（5 LW） |
| p024 | well の所属（W3）と refcount（xd-power） | planned（4 LW） |
| p025 | DC state と DC9（xd-power、dc9.c を消す） | planned（3 LW） |
| p026 | DMC（xd-dmc） | planned（4 LW） |
| p027 | CDCLK（xd-clock） | planned（3 LW） |
| p028 | combo DPLL（xd-clock） | planned（5 LW。5320 の節目） |
| p029 | combo PHY（xd-ddi: init・lane・swing） | planned（4 LW） |
| p030 | DDI の buffer（xd-ddi: BUF_CTL・DP_TP・idle） | planned（4 LW） |
| p031 | DP の訓練（xd-dp） | planned（6 LW。節目: USB-C DP と HDMI の 2 画面はユーザーの一覧） |
| p032 | transcoder と pipe（xd-pipe） | planned（5 LW） |
| p033 | plane と flip（xd-pipe） | planned（3 LW） |
| p034 | DBUF と watermark（xd-dbuf） | planned（7 LW。HDMI の 2 画面はユーザーの一覧） |
| p035 | display の init/uninit の列（xd-power の司令） | planned（4 LW。5320 の節目） |
| p036 | GOP の読み出しと判断（N0、xd-takeover。旧 N0 と影の比較） | planned（4 LW） |
| p037 | 引き継ぎ（N1）の判断と GOP の出力の止め方（xd-takeover。readout・setup_outputs・n1 の helper は p050 まで旧） | planned（5 LW） |
| p038 | screen の plan（純関数。eDP・HDMI・DP-ext の 3 種を同じ段で） | planned（5 LW。改訂 5 で p038 を p038・p048・p049・p050 の 4 段に分けた） |
| p048 | screen の enable/disable の列（3 種を同じ段で。retained） | planned（7 LW。改訂 6: 継ぎ目は commit_enable/_disable。節目: HDMI・USB-C DP の 2 画面、GOP が外部を灯した起動、LCD の目視はユーザーの一覧） |
| p049 | flip と resident run の契約（EAGAIN・UNPLUGGED・RELIGHT・acquired） | planned（4 LW） |
| p050 | 口 `xd.h` と own の file の付け替え（backlight.c を含む）、modeset・state・ddi・dp-sink・pipe・plane・aux・panel・panel-backlight・vblank・takeover の残り・internal.h の Linux の型の削除 | planned（4 LW。5320・SV7 の節目、QEMU の boot-test） |
| p039 | hotplug（xd-hpd。hotplug.c を消す） | planned（6 LW。改訂 5 で p039 を p039・p051 に分けた。節目: HDMI の plug で 2 画面はユーザーの一覧） |
| p051 | own の output.c の port の規則と head-rules の pipe の規則を platform の記述に（hdmi.c・hdmi-mode.c・head-rules.c を消す） | planned（3 LW） |
| p040 | Dekel の窓と TC PHY、TC PLL（xd-tc の窓を dkl-phy.c と一緒に入れ替える、clock.c の残りを消す） | planned（5 LW。改訂 5 で p040 を p040・p052 に分けた） |
| p052 | Type-C の状態機械（流れ A）と USB-C の DP（UCSI、DP-ext の probe。tc.c・tc-kern.c・dp-ext*.c を消す） | planned（6 LW。節目: USB-C の plug はユーザーの一覧） |
| p041 | suspend の戻り（xd-display） | planned（4 LW。suspend はユーザーの一覧） |
| p042 | display.c の段の縮約（§7 #12）・log の文言・xd-diag | planned（4 LW） |
| p043 | 旧 code の残りの削除・試験の整理・出典の監査 | planned（4 LW。QEMU の boot-test、SV7） |
| p044 | platform の網羅（display 12 の行）と TGL の TC（流れ B、host だけ）。#21 で全部 xd | planned（4 LW。5320 はユーザーの一覧） |
| p045 | display 11（Ice Lake） | planned（8 LW。実機は無い） |
| p046 | 規約の全文の見直しと p045 の出典の監査 | planned（3 LW） |
合計 38 段、161 LW（±40 %。改訂 6 の 159 に、p018 を p018・p053 に分けた +2: 2026-10-16 q1223）。5330 の素の起動は段の kernel で最少 37 回、基準の起動と `i915.debug=display` の起動を含めて見込み約 50 回（最大 70〜85 回、design.md §6.6）。
