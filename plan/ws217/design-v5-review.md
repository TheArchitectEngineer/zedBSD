<!-- awesome-plan project=zedbsd record=ws217-design-v5-review -->

# WS217 設計の補遺 第 5 版の敵対的レビュー

- 対象: [design-v5.md](design-v5.md)（2026-10-29 P5、q1666）。照合の基準: [p001 第 4 版](phase001/phase.md) §5・§9・§13、[review-4](phase001/review-4.md)、
  [p009](phase009/phase.md)、[ws.md](ws.md)、[WS309 ws.md](../ws309/ws.md)。
- 照合した tree: worktree の HEAD `61630bb0b`（設計と同じ）。main は `9fd49a6f9` まで進んでいるが、`git diff --stat 61630bb0b main -- src/drivers/gpu include/uapi/gpu-display.h include/drivers/gpu userland/desktop/libvulkan userland/tests/vkdemo` は空で、
  変わったのは `plan/ws309/ws.md`（決定 9 の追加）と `plan/ws309/design.md`（新規）だけ。この 2 つは main の版を `git show main:` で読んだ。
- 文書・source は変えていない（この file だけを書いた）。`.internal/` と GPL の source は読んでいない。build・host 試験・QEMU・実機は流していない。
- 重大度: **blocker**（このまま実装・投入すると壊れる、または段に入れない）／**major**（主要な経路・受け入れ・計画を外す）／**minor**（局所の誤り・欠落・不正確）。
- 件数: blocker 3、major 9、minor 17。

## 判定

- **p009 の i915 の部分は、B1 を直すまで実装に入れない**。§2.3 の「点灯・relight・resume は 0」は、今の code の「relight と head の resume は最後の絵をそのまま出し直す」という約束と
  衝突する。180 の出力で relight・head の resume が起きると、新しい present が来るまで（head はほぼ来ない）絵が逆さのまま残る。
- **p009 の libvulkan の部分は、B2（WS309 の決定 9 で libvulkan の WSI・display が WS309 P1 の所有になった）の調整を Q1 がするまで投入しない**。
- **p011 は B3 の依存の書き直しまで始められない**（依存の WS214 p003a/b は「in-progress のまま閉じた」ので cleared にならない）。
- M1〜M4 は §2 の文の直しと関数の一覧の追加で直る。M5 は Q217-1 の前提が誤っているので、ユーザーに聞く前に直す。

## blocker

### B1. relight と head の resume で 180 の絵が逆さのまま残る（§2.3「0 に戻る所」、第 4 版の「cfg に rotation の欄は足さない」を引き継いだ誤り）

- 根拠:
  - resident の relight: `resident.c:2348-2357` は「Lit again for two pipes next: buffer A … takes the picture B shows, so the output comes back with its last picture and not an older one」で、
    B の絵を A に写してから window を出る。relight（`present.c:1082-1112` の retrain と `drv_i915_head_needs_relight`、`present.c:2049-2054`）の後の点灯は
    `resident.c:869` の `env->cfg.fb_surf`（buffer A）で prepare され、`xd-picture.c:367-368` の `flip.current = config->fb_surf` と `i915_xd_picture_plane_fill(pic, config->fb_surf)` が
    plane を作る。設計ではここが rotation 0。**新しい present を待たずに「最後の絵」を出すのがこの経路の目的**（present の無い時に出す）。
  - head の resume: `head.c:672-735` の `drv_i915_head_stop(display, keep=1)` は B を A に写して dormant にし、`head.c:737-780` の `drv_i915_head_resume` が次の window の始めに
    「with its last picture」で点け直す（`head.c:911` の `head->cfg.fb_surf = head->buf[0].surf`）。`head.c:663-667` の注記は
    「the compositor of the extended mode presents a head only when it opens it or the wallpaper changes」。
- 起こる状況: 180 の出力（p005 の head、p010 の resident）で、(a) 2 台目の display を挿して resident が 2 pipe 用に点け直る、(b) DP の link の再 train、(c) sleep・hold の時間切れで window を出て
  次の window で head が resume する。どれも 0° で点くので、最後の絵が上下逆に出る。resident は次の damage の present まで（idle の desktop では長い。compositor が damage の時だけ present するのは
  p002b の damage の設計からの推測）、head は壁紙の変更か開き直しまで逆さのまま。§2.3 の制限「1 frame まで逆さ」は誤り。
- 直し方: `struct i915_xd_picture_config` に `fb_rotation` を足し、prepare が `flip.current_rotation = config->fb_rotation`・`plane_fill(pic, config->fb_surf, config->fb_rotation)` にする。
  回転は「buffer の絵の回転」として buffer と一緒に保つ: resident は window の終わりに keep_buffers なら `current_rotation` を `display->window` の欄に残し、relight の cfg に渡す
  （buffer を保たない点灯は 0）。head は `drv_i915_head_stop(keep=1)` で `head->kept_rotation` に残し、`drv_i915_head_resume` の cfg に渡す（release の stop は 0）。
  host 試験に「keep の後の点灯が保った回転で plane を作る」を足す。第 4 版の「cfg に rotation の欄は足さない」を撤回すると明記する。

### B2. libvulkan の WSI・display は WS309 の範囲（P1 の所有）になった（§0 の 2、§3、§5 の「先に始められる p009」）

- 根拠: main の `plan/ws309/ws.md` の決定 9（2026-10-29、ユーザーの回答「V2 swapchain 共有 (推し)」）と Q1 の解釈「libvulkan の WSI・display の部分を WS309 の範囲に含め P1 の所有に足す」。
  main の `plan/ws309/design.md:7`（範囲に「game mode の (V2) に要る libvulkan の WSI・display の部分」）、`:214-219`（display の swapchain の external memory の export、
  `VK_EXT_swapchain_maintenance1` の present fence を `wsi-swapchain.c`・`wsi-display.c` に足す）。設計の照合の基準 `61630bb0b` にはこの決定が無い。
- 起こる状況: p009 が `wsi.c`・`wsi-display.c`（surface の作成・present・commit）を、WS309 P1 が同じ `wsi-display.c`・`wsi-swapchain.c`（swapchain の作成・present）を並行に変え、
  AGENTS §4 の file の所有と merge の衝突になる。WS309 の V2（client が display の swapchain の image に直に描く）は、surface の transform 180 の swapchain の image を
  client に渡す時の向きの約束（README に書く「surface の座標は scanout の後の向き」）にも触れる。
- 直し方: Q1 が (a) p009 の libvulkan の変更を WS309 P1 に渡す、(b) p009 を先に入れて WS309 がその上に載る順を決める、のどちらかを決めてから p009 を投入する。
  §3 に WS309 の design.md の V2 と transform の関係（180 の出力の game mode は D17 で直の scanout をしないので V2 の対象外、と書けば足りる見込み）を 1 行足す。

### B3. p011 の依存「WS214 p003a/b の clear」は満たせない（§5 の p011 の行）

- 根拠: main の `plan/ws214/ws.md:6`（Status: completed、2026-10-27）、`:46-47`（p003a・p003b は「in-progress のまま閉じた」、Pi・T1 は未実施）。完了した WS の Phase は cleared にならない（AGENTS §2）。
- 起こる状況: p011 がいつまでも始められない。依存の表と master の Next が食い違う。
- 直し方: 依存を「main の `display-flip.c`（WS214 p003a `4438fc682`・p003b `114e7c68c`）が Pi 4 の実機で動くことを p011 の最初の段で確かめる」に書き換える
  （Pi の実機の確認の担当と順は Q1）。L33（同じ file を 2 つの WS で触らない）は WS214 が閉じたので当たらない、と書く。

## major

### M1. present の item を作る 3 か所と `drv_i915_present` の引数が抜けた（§2.2、review-4 M23 の後退）

- 根拠: §2.2 は「`drv_i915_present_display_present` が `request->flags` から写す」。実際の item は `drv_i915_present_display_present` の中では作らず、
  (1) 公開の `drv_i915_present(device, pixels, width, height, stride, bgra)`（`present.h:42`、`present.c:145-170`、呼び手は `present.c:427` だけ）、
  (2) `i915_present_shared`（`present.c:1442-1500`、resident と head の blob、`item.head = head`）、(3) `i915_present_head` の copied の経路（`present.c:1930-1938`）で作る。
  head は `present.c:366-375` で `drv_i915_present_display_present` を早く抜ける。
- 起こる状況: 写す所を入口の 1 か所と読んで実装すると、copied の経路（`display_present_native`、libvulkan の `wsi-display.c:1859`）と head で回転が落ちる。
- 直し方: §2.2 に 3 か所と、`drv_i915_present` に `unsigned rotation` を足す（呼び手 1 つ）、`i915_present_shared` に引数を足す、を書く。

### M2. `i915_present_flip` の 3 つ目の呼び手（window の終わりの flip back）の回転が無い（§2.2・§2.3）

- 根拠: `i915_present_flip` の呼び手は `present.c:1189`（CPU の frame）、`:1281`（blob）、`:1320` の `drv_i915_lcd_resident_flip`。
  後者は `resident.c:2363` が window の終わりに buffer A へ戻す（relight では A に B の絵が入っている）。
- 起こる状況: 回転の引数を frame の item から取る形にすると、flip back は 0 か不定で、stop の前の 1 frame と（B1 を直した後でも）relight の前の絵が逆さに出る。
- 直し方: `drv_i915_lcd_resident_flip` は `flip.current_rotation`（今 live の回転）で flip する、と書く。B1 の「保つ回転」と同じ値にする。

### M3. flip の状態を写す所の一覧が不正確で、`struct i915_xd_flip` の純粋な関数とその試験が計画に無い（§2.3・§2.6）

- 根拠: `current` を `pending` から写すのは `xd-screen.c` の 3 つの純粋な関数: `drv_i915_xd_flip_judge`（`:716`、同期の flip）、`drv_i915_xd_flip_idle`（`:741`、poll と retire）、
  `drv_i915_xd_flip_settled`（`:764`、`drv_i915_xd_picture_flip_settle` の待ち）。`i915_xd_picture_flip_retire`（`xd-picture.c:2345-2358`）は event と vblank の参照を返すだけで写さない。
  `drv_i915_xd_flip_arm(flip, surface)`（`xd-screen.c:681-689`）の形が変わる。`i915_xd_picture_flip_arm` の呼び手は `flip_nowait`（`xd-picture.c:1003`）のほかに
  同期の `drv_i915_xd_picture_flip`（`:921`）。これらの関数の試験は `host-xd-screen-test.c:1186-1223`（`drv_i915_xd_flip_refused`・`_arm` を直に呼ぶ）。
- 起こる状況: settle（window の終わり・dark の前）で `current_rotation` を写し忘れ、次の `drv_i915_lcd_resident_flip` が古い回転を使う。`drv_i915_xd_flip_arm` の形を変えると
  `host-xd-screen-test.c` と `host-xd-picture-test.c:904, 915, 918`（4 引数の呼び）が build で壊れる（§2.6 に無い）。
- 直し方: §2.3 に 3 つの関数の名前と行、`drv_i915_xd_flip_arm(flip, surface, rotation)`、同期の flip も回転を取る（呼び手は host 試験だけ）を書く。§2.6 に `host-xd-screen-test.c` の項
  （arm → judge/idle/settled で `current_rotation`、stuck では残る）と、既存の 2 つの試験の呼びの追従を足す。

### M4. 「flip しない経路」の直し方は既定の build では死んだ経路で、計画の試験では届かない（§2.3・§2.6）

- 根拠: `*flip = 0` は armed の flip が latch していない時だけ（`present.c:1639-1643`、`head.c:1215-1220`）。FIFO では presenting thread が先に latch を待つ
  （`present.c:414-419` の `i915_present_latch`、head は `present.c:1914-1920`）ので、`I915_PRESENT_NO_VSYNC`（`present.c:102`、`0`）を 1 にした build でしか起きない。
  §2.3 の「latch を待ち flip する」は worker（display の owner）で待つことになるが、`drv_i915_xd_picture_flip_wait` は「from a thread that is not the display owner's … Nothing of the screen changes」
  （`xd-picture.c:1084-1092`）で、owner が使うのは `drv_i915_xd_picture_flip_settle`。§2.6 の「`host-xd-picture-test.c` で flip しない経路で回転が違えば flip する」は、
  `i915_present_target`・`i915_head_target` が `present.c`・`head.c` の static な関数なので、その試験から呼べない。
- 起こる状況: 実装が `flip_wait` を worker で呼ぶと retire されず、次の `i915_present_flip` が BUSY で断られる（`xd-screen.c:663-664`）。試験の項は書けず、未試験の経路が残る。
- 直し方: 「回転が違えば worker が `drv_i915_xd_picture_flip_settle` で待ち（失敗は EIO）、`*flip = 1` で back buffer に描く」と書く。NO_VSYNC の build だけの経路と明記し、試験の表から外す
  （または `i915_present_target` の判定を純粋な関数に切り出して host 試験にする）。

### M5. Q217-1（Gen9 を外す）の前提が誤っている（§0 の 3、§2.5、§7）

- 根拠:
  - Gen9 の実機はある: `plan/tools.md:26`（SV7 `10.0.10.27`）、`:74`（「8 世代の Let's Note SV7（UHD 620 8086:5917、Gen9.5）」）。
  - takeover の検査の緩めは要らない: `xd-takeover.c:1681` の「rotated」は `i915_xd9_fw_decode_plane` の中で、firmware の plane を引き取る時（`xd9-flip.c:243` の `drv_i915_xd9_fw_decode`）だけに読む。
    resume の確かめは `drv_i915_xd9_fw_runs`（`xd9-flip.c:605`）で、回転を見ない（`xd9-flip.c` の grep で `ROTATE` は無い）。zedBSD が自分で立てた回転には当たらない。
  - 足す所は小さい見込み: `xd9-flip.c` は PLANE_SURF だけを書く（`xd9-flip.c:8-41` の説明「the plane's control, stride and watermarks are the firmware's」）。回転は firmware の PLANE_CTL の
    bit 1:0 だけを替えて SURF の前に書き（同じ二重 buffer）、lease の終わりで firmware の値に戻す、の 2 点（推測。xd9 の書き込みの順と DC の扱いは未確認）。
  - ユーザーの決定（ws.md、2026-10-13）「180°: Intel は全部、表示の hardware の回転」。Gen9 を外すのは決定の変更なので、問いにするのは正しいが、前提が違う。
- 起こる状況: 「試験の台が無い・takeover を緩める」という誤った理由でユーザーが外す判断をする。
- 直し方: §2.5 を「SV7（Gen9.5）で試験できる。takeover の検査は変えない。xd9 の PLANE_CTL の bit 1:0 の書き込みと lease の終わりの戻しで、見積もり +3〜5 LW（推測）と SV7 の割当」に直し、
  推しをどちらにするかを決め直して Q217-1 を出す。

### M6. p006a は WS309 と同じ file と同じ経路を触る（§4、§5 の p006a「thread に依らない」）

- 根拠: main の `plan/ws309/design.md:7`（範囲に libkeiland-backend の共通と zedBSD）、`:34`（T6 io-device: 入力の device の検出「readdir・open・capability・close」）、`:35`（T7 io-file: displays.conf の書き込みと fsync）。
  p006a の `kl_backend_input_uniq`（第 4 版 §13 の変更する file: `libkeiland-backend/evdev/input-evdev.c`、`libkeiland-backend-zedbsd/input-zedbsd.c`）は device の open の時に identity を読む所で、
  T6 に移る経路そのもの。`displays.conf` の `touch=` の parse/format は compositor の `displays.c`（WS309 の範囲）。
- 起こる状況: p006a と WS309 の T6・T7 の作り直しが同じ関数を並行に変え、merge の衝突か二重の実装になる。
- 直し方: p006a も「WS309 の T6・T7 の Phase の後」にするか、p006a を kernel の部分と純粋な関数（parse/format、identity の比べ）だけにして file の所有を Q1 が WS309 P1 と調整する、と書く。

### M7. 試験の計画が変更の主な部分を見ない（§2.6・§3）

- 根拠と欠け:
  - vkdemo の 180 の試験の入口が無い: vkdemo の option は `--device-index=`・`--offscreen`・`--readback`・`--output=`・`--token=`・`--duration=`・`--time-ms=`・`--hold=`・`--verify-session` だけ
    （`userland/tests/vkdemo/main.c:256-335`）で `--transform=` は無い。renderer は `vkdemo_display_open`（`renderer.c:622`）を呼び、それが `vkdemo_display_open_on` を IDENTITY で呼ぶ
    （`display.c:77`、`:133`）。§3 は `vkdemo_display_open_on_transform` だけで、`vkdemo_display_open` の transform 版と main の option が抜けている。`display.c:70-72` は
    display の `supportedTransforms` の IDENTITY だけで選ぶので、180 を選ぶ時の判定も要る。
  - libvulkan に試験が無い: §2.6 に libvulkan の項が無く、host 試験の形も決めていない（第 4 版 §5.3「形があれば」のまま。libvulkan の directory に試験の file は無い）。
    flag の写し（`wsi-display.c:340-363`）・surface の受け（`wsi.c:621`）・present の flag（`wsi-display.c:1419, 1859`）・commit（`:2410`）は実機の 1 本だけで見る。
  - build の項が `$(BUILD)/vmunix`（amd64）だけ: vkdemo の `display.c` は compositor の build（zedBSD・Linux・FreeBSD、review-4 M25）に入るので 3 つの compositor の build、
    libvulkan と vkdemo、`gpu.c` を共有する arm64（rpi4）の kernel の build も要る。
  - head の 180 を試さない（i915 の head の経路を足すのに試験は pipe A の plane 1 だけ）。
  - hold の経路（release → hold → 次の lease の flag 無しの最初の frame、`present.c:1734-1764`）を明示しない。「vkdemo を終えて通常の frame で `00b`」は、何が次の frame を出すかが無い
    （compositor を止めた素の起動なら hold の時間切れで stop になり、`00b` を読む plane が無い）。
  - DC 状態（p009 §6 の未確認）: flip の間だけ DC を止める（`xd-picture.c:2326` の `power_get`、`:1016` の `power_put_async`）ので、180 のまま present の無い間に DC5/DC6 に入って出た後の
    plane の回転を見る項が無い。register を ssh で読む方法が DC の中で正しい値を返すかは未確認（推測）。
- 直し方: §2.6 に (1) vkdemo `--transform=180` の追加（main の option と `vkdemo_display_open` の transform 版）、(2) libvulkan の試験の形（無ければ「実機だけ」と制限に書く）、
  (3) build の一覧（amd64・arm64 の vmunix、libvulkan、vkdemo、compositor の 3 OS）、(4) 実機で 180 の vkdemo → 10 秒 present 無し → 画面と PLANE_CTL、(5) 180 の vkdemo の終わりの後 hold の時間の中で
  flag 無しの vkdemo → `00b`、(6) head は host 試験で見て実機は p005 に回す、と書く。

### M8. 受け入れの実機が試験の台の一覧に無い（§2.6、§5 の p009・p011・p007）

- 根拠: `plan/tools.md:20-27` の実機の表は 5330・VF2・RDNA の機械・SV7・Pi 5 だけで、5320 と Pi 4 が無い。5330 は今 P3 の従の機械（main の `plan/master.md` の P3 の行）、
  SV7 は「5330 と入れ替えで起動することがある」（`tools.md:26`）。
- 起こる状況: p009 の「5330・5320 の実機」と p011 の Pi 4 の受け入れが組めず、Phase が uncleared で止まる（5320・Pi 4 が別の所にあるかは未確認）。
- 直し方: 受け入れを「5330（display 13）で必須、5320（display 12）は機械がある時」とするか、display 12 の機械を Q1 に確かめる。p011 の Pi 4 の所在も Q1 に確かめる、と §5 に書く。

### M9. p009 の見積もりと分け方（§5）

- 根拠: §5 の 14 LW の内訳に、B1（config と保つ回転）、M1〜M4 の追加、`host-xd-screen-test` の追従、核の host 試験の枠を一から作る事（`src/drivers/gpu/` に host 試験は無い）、
  vkdemo の renderer と option、build の 6 種、5330 の script（DC の待ちを含む）が入っていない。guardrail「i915 の display の書き換えは 5330 で動く段ごとに」（`plan/guardrail.md:134-141`）で
  i915 の変更は 5330 で合格するまで main に入らないので、UAPI・核・libvulkan まで 5330 の空きを待つ。
- 起こる状況: 見積もりの 1.5 倍前後（推測、20 LW 前後）。5330 が空かないと、i915 と関係の無い UAPI・libvulkan も main に入らない。
- 直し方: p009 を p009a（UAPI・核の検査・libvulkan・vkdemo。印を出す driver が無ければ何も変わらない。host 試験と build で main に入れてよい）と p009b（i915 と 5330）に割る。
  見積もりを直す。B2 の結論で p009a の libvulkan の担当が WS309 P1 に移る場合もここで分かれる。

## minor

- m1. §2.3「retained（`drv_i915_xd_picture_retained`、`present.c:2064`）は 0」: `present.c:2062-2066` は「display がまだ読むかもしれない run は試し直さない」の判定で、点灯の経路ではない。
  retained の screen は flip を断り（`xd-screen.c:660-661`）、最後の回転のまま残る。0 に戻る所の一覧から外す。
- m2. §2.3「180 の session の最中の resume は 1 frame まで逆さ」: xd の resume は「the next presentation lights the output again」（`i915.c:459-463`）で、resident は次の present まで暗く、
  点灯は buffer A の黒（0°）→ flag つきの flip なので逆さの絵は出ない（推測、`drv_i915_device_resume_hw` の中は読んでいない）。逆さが残るのは B1 の head の resume。制限の文を直す。
- m3. §2.4「commit の primary の `PLANE_ROTATE_180` は `gpu.c:4417` の呼び手で ops を見て」: `gpu.c:4417` の呼び手は `gpu_commit_check` で ops を持たない。ops を持つのは
  `gpu_commit_ioctl`（`gpu.c:4281-4310`、`ops = device->ops->display` の後に `gpu_commit_check`）。そこに書く。`gpu_commit_plane_check`（`gpu.c:4457`）の既知の bit に足すことと、
  `plane != 0` の ROTATE を EINVAL にすること（第 4 版 §5.1）も §2.4 に再掲すると取り違えない。
- m4. §2.4 の `display_query` の 2 か所を行で書く: resident は `display.c:2038`（`i915_display_query`）、他の connector（head の候補）は `display.c:2701`（`i915_display_other_query`）。
  head は claim の前の query で印を見るので、後者に出さないと head に 180 の surface を作れない。
- m5. §3 の libvulkan の行が足りない: flag の写しは `wsi-display.c:340-363`（新しい `VULKAN_WSI_OUTPUT_ROTATE_180 64U` は `wsi-internal.h:20-27` で空き）、present は `wsi-display.c:1419`（blob）と
  `:1859`（copied）、commit の primary は `:2410`。第 4 版の `1412, 1850, 2401` は古い。
- m6. 核の host 試験を `plan/ws217/tests/host-gpu-display-flags.c` に置くと、WS の完了で消える（AGENTS §5「その WS だけの試験を消す」）。核の回帰の試験なので `src/drivers/gpu/` の下
  （i915 の `tests/display/` と同じ形）に置くか `plan/tools.md` に登録する。切り出す純粋な関数の file と kernel の build の source の一覧への追加も書く。
- m7. §2.1 の範囲に display version 11（Ice Lake）を含めるが、p009 の PRM の確認は TGL（12）と Alchemist（13）だけ（`phase009/phase.md` §2）。ICL の PRM（公開）で確かめるか、
  display 11 は推測と書く。host 試験の ICL の setup は既にある（`host-xd-pipe-test.c:39`）。
- m8. §2.1「display version 14 以上は xd の対象外（`xd-bandwidth.c:292-293` が 11〜13 に限る）」: その行は bandwidth の mailbox の点を読むかどうかの判定で、xd の経路に入るかの gate ではない。
  gate の所を引くか、文を「i915 は display 14 を扱わない（WS295 の xe）」に替える（未確認）。
- m9. p009 の §3.4・§4・§7 の「zedBSD の今」の列は今は無い file（`display/plane.c`・`modeset-internal.h`・`takeover.c`）を引く。xd では FBC は takeover で止め（`xd-takeover.c:674-677`）、
  xd の経路に PSR を有効にする code は無い（`grep -i psr display/xd-*.c` は xd9 の読み出しだけ）。§0 の 1 に「p009 の §3.4・§4・§7 の zedBSD の側は第 5 版で置き換え」と書く。
- m10. 出典: 新しい定義の comment は「Value referenced: Intel … PRM, Volume 2c … Rev 1.0（Doc Ref # …）, PLANE_CTL (Plane Rotation)」の形にし、`plan/` や p009 の節を書かない（guardrail の
  開発の管理情報、`plan/guardrail.md:127-132`）。`display-regs-xd.h:959-967` の既存の block の「Values referenced」は Linux の `i915_reg.h` の名前を並べているので、その中に足すなら
  PRM の行を足すか、別の block にする。`drv_i915_xd_plane_words` の「Procedure referenced」（`xd-pipe.c:946-947`）にも回転の手順の出典を足す。
- m11. §5 の p012 の依存に p005 が無い（review-4 M26「配置の再検査の保存の抑えは p005 が入れる code に依る」）。§4 は「M26 を当てる」と書くので、表にも足す。
- m12. §5 の p011 は「第 4 版 §5.4 のまま」で依存から p010 を外したが、第 4 版 §13 の p011 の受け入れには「Settings の無効表示」（compositor）がある。p010 無しで試せる受け入れ
  （p009 の vkdemo `--transform=180` を Pi で）に書き直す。
- m13. §2.3「log の段の印は変わった時だけ」: 前の値を持つ欄（例: `display->window` に最後に log した回転）と、log を出す所（flip の後の worker）を書く。
  `drv_i915_xd_picture_status` は status を写すだけで、変化を知らない。
- m14. 回転の型と値: item は「度数の `unsigned`」、plane は `uint32_t`、flip の欄は型が無い。`drv_i915_xd_plane_words` は 0 と 180 以外を受けない（display 13 に 90/270 の値が無い、p009 §3.1）と書き、
  host 試験に「90 は 0 として扱う／断る」のどちらかを足す。
- m15. §1 の `gpu-display.h:30-36`（flags の使用 1〜2048）は `include/uapi/gpu-display.h:23-36`。
- m16. ws.md の Phase の表（p009-1 は planned、p006 は 1 行）と「設計の要点」（`hw.rotation`、`PLANE_CTL_ROTATE_180` の既存の計算）は第 5 版と食い違う。§7 に「Q1 が ws.md を直す」を足す。
- m17. 推測・要確認: prepare を経ずに light を繰り返す経路（DP の link を下げての再試行など）があると、`pic->plane` に前の flip の surface と回転が残る。`i915_head_light_once`（`head.c:848`）と
  resident の run の再試行が毎回 prepare するかは読んでいない。B1 の直しと一緒に確かめる。

## 確かめた主張（正しかった物）

| 主張（設計の §1・§2） | 確かめた所 |
| --- | --- |
| flip の経路 `i915_present_flip` → `drv_i915_xd_picture_flip_nowait` → `i915_xd_picture_flip_arm` → `plane_fill`（memset）→ `drv_i915_xd_plane_words` → noarm／arm | `present.c:1660, 1697`、`xd-picture.h:363`、`xd-picture.c:984-1029, 2146-2164, 2266-2331`、`xd-pipe.c:950-962, 974-1021` |
| head も同じ `flip_nowait`（`I915_XD_PICTURE_HEAD`）、head は常に xd の経路 | `head.c:1241`。Gen9 は `i915_bootfb_ops`（`bootfb.c:165`、`display.c:945-948`）で head が無い |
| `struct i915_xd_flip` の欄、refused（同じ buffer を断る）、arm | `xd-screen.h:243-250`、`xd-screen.c:648-689` |
| PLANE_CTL の xd の定義に回転が無い、xd9 の `ROTATE_MASK 0x3` は takeover の検査だけ | `display-regs-xd.h:970-974`、`display-regs-xd9.h:336`、`xd-takeover.c:1681` |
| flip しない経路の行 | `present.c:1626, 1642`、`head.c:1204, 1219` |
| worker の item に回転の欄が無い | `worker.h:73-89` |
| 核の ops の印の検査、present の flags の組 | `gpu.c:1257-1258`、`gpu.c:3930-3931`、`include/drivers/gpu/gpu-display.h:97` |
| i915 に commit が無い、bcm2711 だけ | `display.c:182-199`（commit は NULL）、`bcm2711/display/display-device.c:1528-1529` |
| `gpu_display_info.flags` の 4096 は空き | `include/uapi/gpu-display.h:23-36`。main の同じ file と plan の grep で他の WS の予約は無い |
| libvulkan の行 | `wsi.c:125, 621`、`wsi-display.c:591-592`、`wsi-swapchain.c:1274` |
| vkdemo の呼び手 | `compose.c:362`、`heads.c:993`、`display.h:29` |
| i915 の host 試験 | `host-xd-picture-test.c:904-919`、`host-xd-pipe-test.c:469, 487` |
| cursor・overlay の plane は i915 に無い（回転の対象は primary だけで足りる） | `display.c:175-199`（cursor の op は NULL、「The cursor plane is not used yet」）、`display.c:2049, 2718` の `plane_count = 1U` |
| PLANE_OFFSET は 0 を書く（180 で補正しない、PRM の hardware の加算と合う） | `xd-pipe.c:997` |

## 確かめていない点

- DC5/DC6 の出入りで PLANE_CTL の回転が保たれるか（PRM に記述が無い、p009 §6）。
- 5320 と Pi 4 の所在（`plan/tools.md` に無いことだけ確かめた）。
- compositor が idle の時に resident へ present しない（damage の時だけ）こと（B1 の影響の大きさ）。
- xd9 の PLANE_CTL の書き換えの手順と DC の扱い（M5 の見積もり）。
- display 14 以上で i915 の xd の経路に入らない gate の所在（m8）。
