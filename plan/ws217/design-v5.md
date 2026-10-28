<!-- awesome-plan project=zedbsd record=ws217-design-v5 -->

# WS217 設計の補遺（第 5 版）: 残りの段の並べ直しと p009 の i915 の作り直し

Parent: [WS217](ws.md)、本体の設計 [p001 第 4 版](phase001/phase.md)（この補遺が置き換える所だけを書く。他は第 4 版のまま）
作成: 2026-10-29 P5（q1666、設計だけ）。照合した tree: main 61630bb0b（`grep -n` で引いた行。第 4 版の行番号は main 278aa9088 の物で、i915 の display は今は無い file を指す）

## 第 5 版 r2 の改訂（2026-10-29 P5、[design-v5-review.md](design-v5-review.md) の blocker 3・major 9・minor 17 を反映。この節が下の本文に優先する）

| # | 改訂 | review |
| --- | --- | --- |
| A1 | **回転は buffer の絵と一緒に保つ**。`struct i915_xd_picture_config` に `fb_rotation` を足し、prepare が `flip.current_rotation = config->fb_rotation`・`plane_fill(pic, config->fb_surf, config->fb_rotation)`。resident は window の終わりに keep_buffers なら `current_rotation` を `display->window` に残して relight の cfg へ（`resident.c:2348-2357`、`resident.c:869`）、head は `drv_i915_head_stop(keep=1)` で `head->kept_rotation` に残し `drv_i915_head_resume` の cfg へ（`head.c:672-780`, `:911`）。buffer を保たない点灯・release の stop は 0。第 4 版の「cfg に rotation の欄は足さない」と §2.3 の「点灯・relight・resume は 0」「1 frame まで逆さ」を撤回する | B1、m1、m2 |
| A2 | **libvulkan の WSI・display は WS309（P1）の範囲**（ws309/ws.md の決定 9、ws309/design.md:7・214-219）。p009 を 2 つに分ける: **p009a** = UAPI の 3 つの定数・ops の印・核の検査（`gpu.c:1258` の登録、present の組、commit の検査は `gpu_commit_ioctl` で ops を見る）・i915 の能力の印を出さない形（driver が印を立てるまで何も変わらないので先に merge できる）。**p009b** = i915（§2 と A1・A3・A4）と 5330 の回帰（guardrail の i915 の節: 5330 で通るまで main に入れない）。libvulkan・vkdemo の部分は **p009c** にし、WS309 の WSI の段と順を Q1 が P1 と決める（Q217-2） | B2、M9、m3 |
| A3 | present の item を作るのは 3 か所: `drv_i915_present`（`present.c:145`、呼び手 `:427`）・`i915_present_shared`（`:1442`）・`i915_present_head`（`:1930`）。`drv_i915_present` に回転の引数を足す（review-4 M23 を戻す）。`i915_present_flip` の 3 つ目の呼び手 `drv_i915_lcd_resident_flip`（window の終わりの buffer A への flip back、`resident.c:2363`）は保った回転（A1）で flip する | M1、M2 |
| A4 | `current` を `pending` から写すのは `xd-screen.c` の純粋な関数 3 つ: `drv_i915_xd_flip_judge`（:716、同期の flip）・`_idle`（:741、poll と retire）・`_settled`（:764、settle）。`drv_i915_xd_flip_arm(flip, surface, rotation)`（:681）、同期の `drv_i915_xd_picture_flip`（`xd-picture.c:921`）も回転を取る。`i915_xd_picture_flip_retire` は写さない | M3 |
| A5 | 「flip しない経路」（`present.c:1639-1643`、`head.c:1215-1220`）は `I915_PRESENT_NO_VSYNC=1`（既定 0、`present.c:102`）の build だけ。回転が違えば worker（display の owner）は `drv_i915_xd_picture_flip_settle` で待ち（`flip_wait` は owner の外の thread 用、`xd-picture.c:1084-1092`）、`*flip = 1`。判定を純粋な関数に切り出して host 試験にする | M4 |
| A6 | **Gen9（xd9）も入れる**（§2.5 と Q217-1 を撤回。ユーザーの決定「180°: Intel は全部」）。前提の誤り: SV7（Gen9.5、UHD 620、10.0.10.27、`plan/tools.md:26, 74`）がある。`xd-takeover.c:1681` の「rotated」は firmware の plane を引き取る時だけ読むので変えない。足す物: xd9 の arm で firmware の PLANE_CTL の bit 1:0 だけを替えて SURF の前に書き、lease の終わりで firmware の値に戻す（`xd9-flip.c:8-41, 518, 1034`。書き込みの順と DC の扱いは実装の最初に確かめる）。+4 LW（±50%）と SV7 の割当 | M5 |
| A7 | **p006a も WS309 の後**: libkeiland-backend の device の open（WS309 の I/O-1 の thread）と displays.conf の I/O（I/O-2 の thread）を触るので、「thread に依らない」は誤り | M6 |
| A8 | 試験（§2.6 に足す）: `host-xd-screen-test.c` の項（arm → judge・idle・settled で `current_rotation`、stuck では残る）と既存の呼びの追従（`host-xd-screen-test.c:1186-1223`、`host-xd-picture-test.c:904, 915, 918`）、A1 の keep の後の点灯、A5 の純粋な関数。p009c は vkdemo に `--transform=180` の option を足す（今は無い、renderer は `vkdemo_display_open` を通る）、libvulkan の host 試験の枠は p009c の最初に作る。build は amd64 の vmunix に加え compositor の 3 OS・libvulkan・vkdemo・arm64。実機の項に head の 180・hold・DC の idle の後（数分放置の後に PLANE_CTL を読む） | M7 |
| A9 | 実機: 5330（10.0.30.3、P3 の WS221 の副の機械）・SV7（10.0.10.27）・5320・Pi 4。5320 と Pi 4 は `plan/tools.md` の試験の台の表に無い → Q1 に機械の割当を頼む（Q217-3） | M8 |
| A10 | p011 の依存「WS214 p003a/b の clear」は満たせない（WS214 は completed、p003a/b は in-progress のまま閉じた、ws214/ws.md:46-47）→「p011 の最初の段で Pi 4 の今の main の HVS の flip が動くことを確かめる」に置き換える | B3 |
| A11 | 細かい直し: `display_query` は `display.c:2038`・`:2701` の 2 か所（m4）、libvulkan の行は `wsi-display.c:340-363, 1419, 1859, 2410`（m5、p009c の最初に引き直す）、核の host 試験は WS の完了で消えないよう `plan/tools/` に置き tools.md に登録（m6、Q1）、PRM の確認は display 11（Ice Lake）を含まない（m7、Ice Lake は PRM の Vol 2c で同じ field を実装の最初に確かめる）、display 14 以上を xd から外す門は `xd-bandwidth.c:292` ではない（m8、実装で本当の門を引く）、source の comment の出典は PRM の題名と Doc Ref で書き plan/ を書かない（m10）、p012 の依存に p005（m11） | m4〜m11 |

### r2 の Phase の表（§5 を置き換える。LW は ±50%）

| Phase | 内容 | 依存 | LW |
| --- | --- | --- | --- |
| p009a | UAPI・ops の印・核の検査と host 試験（A2） | p009-1（済み） | 4 |
| p009b | i915 xd（A1・A3〜A5）と xd9（A6）、host 試験（A8）、5330・SV7 の回帰（5320 は機械しだい） | p009a | 16 |
| p009c | libvulkan・vkdemo（第 4 版 §5.3、A8） | p009a、WS309 の WSI の段との順（Q217-2） | 6 |
| p011 | Pi 4 の 180°（最初に今の main の HVS の flip を確かめる、A10） | p009a、Pi 4 の機械（Q217-3） | 9 |
| p010・p005・p006a・p006b・p012 | compositor の段（WS309 の構成の後、本文 §4） | WS309 | 46（見積もり直し） |
| p007・p008 | 確かめと規約 | 全部 | 10 |

計 91 LW（うち WS309 の後の段 46）。先に始められる: p009a（4）、p009b（16、i915 は 5330 の回帰まで branch に積む）。

### r2 のユーザー・Q1 に聞く点（§7 を置き換える）

- Q217-1: 撤回（A6、決定どおり Gen9 も入れる）。
- Q217-2（Q1）: p009c（libvulkan の WSI の 180°）と WS309 の WSI の段（P1）の順と担当。推し「WS309 の WSI の段の後に、P1 の範囲に p009c を足すか、P1 の merge の後に別の担当」。
- Q217-3（Q1）: 実機の割当: 5330（P3 と共有）・SV7・5320・Pi 4。5320 と Pi 4 の場所と接続は tools.md に無い。

## 0. 要点

1. **i915 の display は WS245 で作り直された**。第 4 版 §5.2 の `modeset.c`・`plane.c`・`hw.rotation`・`drv_i915_lcd_modeset_flip_nowait` は今の tree に無い。
   Gen11〜13（display version 11〜13、5320 の Tiger Lake と 5330 の Alder Lake-P）の scanout は `xd-*`（`xd-picture.c`・`xd-pipe.c`・`xd-screen.c`）、
   Gen9（Skylake・Kaby Lake）は firmware の plane の surface を差し替える `xd9-flip.c`。p009 の i915 の部分を §2 で設計し直す。
   PRM の確認（[p009 の phase.md](phase009/phase.md) §1〜§4）は hardware の事実なので有効のまま。
2. **順**（2026-10-29 Q1 の判断、「WS217 の推しで OK…kernel・libvulkan の p009・p011 を先に。compositor の段（p010・p005・p012・p006 の一部）は WS309 の構成の確定の後」）:
   kernel・UAPI・libvulkan・vkdemo の p009 と Pi 4 の p011 を先に作る。compositor の段は [WS309](../ws309/ws.md) の「画面の制御」thread
   （回転・hotplug・modeset・swapchain の作り直しを直列化する常駐の thread）の上に載せる前提で、WS309 の Phase の分割が決まってから設計を引き直す（§4）。
3. Gen9（xd9）は 180° の scanout を出さない（§2.5、問い Q217-1）。compositor は hw180 の無い display で 180° を turn pass で出す（2026-10-14 ユーザー「D14 を緩める」）ので、機能は欠けない。

## 1. 第 4 版から変わった事実（main 61630bb0b）

| 項目 | 第 4 版の前提 | 今 |
| --- | --- | --- |
| i915 の flip | `modeset.c` の `pend_surf`/`cur_surf`、`drv_i915_lcd_modeset_flip_nowait` | `present.c:1697` の `i915_present_flip` → `drv_i915_xd_picture_flip_nowait(&display->xd.pictures, I915_XD_PICTURE_RESIDENT, surf, &result)`（`xd-picture.h:363`）→ `i915_xd_picture_flip_arm`（`xd-picture.c:2266`）→ `i915_xd_picture_plane_fill`（`:2146`、`struct i915_xd_plane` を memset して作り直す）→ `drv_i915_xd_plane_words`（`xd-pipe.c:950`、PLANE_CTL は ENABLE・FORMAT・display version 13 の ARB_SLOTS だけ）→ `drv_i915_xd_plane_noarm`／`_arm`。head は `head.c:1241` から同じ `flip_nowait`（`I915_XD_PICTURE_HEAD`） |
| flip の状態 | `pend_surf`／`cur_surf` | `struct i915_xd_flip`（`xd-screen.h:243`: `current`・`pending`・`old`・`armed`・`stuck`・`gen`）、`drv_i915_xd_flip_refused`（`xd-screen.c:648`、同じ buffer への flip は断る）・`drv_i915_xd_flip_arm`（`:681`） |
| 回転の register の定義 | `PLANE_CTL_ROTATE_*`（`intel/plane.h`） | xd の側に無い（`display-regs-xd.h:970-974` は ENABLE・ARB_SLOTS・FORMAT・TILING だけ）。Gen9 は `I915_XD9_PLANE_CTL_ROTATE_MASK 0x3`（`display-regs-xd9.h:336`）を takeover の検査（`xd-takeover.c:1681`、回った firmware の plane は引き取らない）だけに使う |
| flip しない経路（review-4 M23） | `present.c:1552-1575`・`head.c:1186-1208` | `i915_present_target`（`present.c:1626`、`*flip = 0` は `:1642`）と `i915_head_target`（`head.c:1204`、`:1219`）に残る |
| worker の item | `struct i915_worker_present`（`worker.h`） | 同じ形（`worker.h:73-89`: pixels・extent・vm・build・context・head）。回転の欄は無い |
| i915 の host 試験 | `host-lcd-modeset-test.c` | 無い。`src/drivers/gpu/i915/tests/display/host-xd-picture-test.c`（flip の状態の模型、`:911` で `cur_surf`）・`host-xd-pipe-test.c`（`drv_i915_xd_plane_words` を display version ごとに、`:469, 487`）・`host-gen9-flip-test.c` |
| 核の ops の印の検査（review-4 M24） | `gpu.c:1096` | `gpu.c:1257-1258`（`flags & ~DRV_GPU_DISPLAY_UNGATED` で EINVAL）。印は `include/drivers/gpu/gpu-display.h:97` の `DRV_GPU_DISPLAY_UNGATED 1U` だけ |
| 核の present の flags の検査 | `gpu.c:3530` | `gpu.c:3930-3931`（`FIFO` か `FIFO|BLOB` だけ） |
| commit | i915 に無い、bcm2711 だけ | 同じ（`bcm2711/display/display-device.c:1529`）。`gpu_commit_plane_check` は `gpu.c:4417` から |
| `gpu_display_info.flags` の空き | 4096 | 使用は 1〜2048（`gpu-display.h:30-36`）。4096 は空き（実装の時に WS306 などの追加と取り合わないか main で確かめる） |
| libvulkan | `wsi.c:125, 621`、`wsi-display.c:584`、`wsi-swapchain.c:1214` | `wsi.c:125`（`supportedTransforms = IDENTITY`）・`wsi.c:621`（IDENTITY 以外を断る）・`wsi-display.c:591-592`（surface の capabilities）・`wsi-swapchain.c:1274`（`preTransform`） |
| vkdemo の呼び手（review-4 M25） | `compose.c:307`・`heads.c:837` | `compose.c:362`・`heads.c:993`（`vkdemo_display_open_on`、`userland/tests/vkdemo/display.h:29`） |

## 2. p009 の i915 の設計（第 4 版 §5.2 を置き換える）

### 2.1 範囲

display version 11〜13 の xd の経路だけ（5320・5330 と、同じ経路の Ice Lake・Rocket Lake・Alder Lake-S/P・Raptor Lake）。
xd9（Gen9）は 2.5。display version 14 以上は xd の対象外（`xd-bandwidth.c:292-293` が 11〜13 に限る）。

### 2.2 運ぶ形

- `struct i915_worker_present`（`worker.h:73`）に `unsigned rotation`（0 か 180、度数。p001 §2 の時計回りの約束）を足す。
  `drv_i915_present_display_present`（`present.c` の ops の入口）が `request->flags & GPU_DISPLAY_PRESENT_ROTATE_180` から写す。
  head の item（`head = 1`）も同じ欄。frame の back buffer への copy は回さない（hardware が回す）。
- `drv_i915_xd_picture_flip_nowait(pictures, screen, surface, result)` に `rotation` の引数を足す（呼び手は `present.c:1697` と `head.c` の 2 か所。旧い形の wrapper は作らない: 呼び手が 2 つで同じ段で直せる）。
- `struct i915_xd_plane`（`xd-pipe.h:175`）に `uint32_t rotation` を足し、`i915_xd_picture_plane_fill(pic, surface, rotation)` が入れる。
  `drv_i915_xd_plane_words` が PLANE_CTL の bit 1:0 に 180 を `10b` で入れる（新しい定義 `I915_XD_PLANE_CTL_ROTATE` mask `0x3`・値 `I915_XD_PLANE_CTL_ROTATE_180 2`、
  値の出典は PRM: [p009 §3.1](phase009/phase.md)。block の前に「Value referenced:」の複数行の comment、guardrail の出典の規則）。
- `PLANE_OFFSET`・`PLANE_POS`・`PLANE_SURF`・stride は変えない（PRM: hardware が offset に plane の大きさを足す、p009 §3.3）。

### 2.3 状態（review-4 M23 の今の形）

- `struct i915_xd_flip`（`xd-screen.h:243`）に `current_rotation`・`pending_rotation` を足す。`drv_i915_xd_flip_arm` が `pending_rotation` を書き、
  `current` を `pending` から写す所（latch の確認・retire・同期の flip）で `current_rotation` も写す。点灯（`xd-picture.c:367` の `flip.current = config->fb_surf`）は 0。
- **flip しない経路**（`present.c:1642`・`head.c:1219`）: item の回転が armed の回転（`pending_rotation`）と違う時は、armed の buffer に描かずに latch を待ち、
  flip する（`flip = 1` にする）。同じなら今のまま。
- 状態の報告（`xd-picture.c:836-838` の `status`）に `cur_rotation` を足し、log の段の印（`i915: plane rotate=180` の形、変わった時だけ）に使う。
- 0 に戻る所: flag の無い present は 0、点灯・relight・retained（`drv_i915_xd_picture_retained`、`present.c:2064`）・resume の再点灯は 0。
  hold（release の後）は flip しないので最後の絵の回転のまま残り、次の lease の最初の flip が正す（第 4 版 §5.2 の制限と同じ: 180 の session の最中の resume は 1 frame まで逆さ）。

### 2.4 能力の印

- i915 の resident の ops に `DRV_GPU_DISPLAY_ROTATE_180`（`gpu-display.h` の新しい印 `2U`）。capture の ops には付けない。
- `display_query` は xd の経路の display（resident と head）にだけ `GPU_DISPLAY_ROTATE_180`。xd9・bootfb の copy の経路には出さない。
- 核: `gpu.c:1258` の既知の bit に `DRV_GPU_DISPLAY_ROTATE_180` を足す（review-4 M24。落とすと i915 の登録が EINVAL で画面が出ない）。
  `gpu.c:3930` の present の組に `|ROTATE_180` の 2 つ、ops に印が無ければ EOPNOTSUPP。commit の primary の `PLANE_ROTATE_180` は `gpu.c:4417` の呼び手で ops を見て同じ扱い。
  検査は純粋な関数に切り出し host 試験にする（`src/drivers/gpu/` に核の host 試験の枠は無い。`plan/ws217/tests/host-gpu-display-flags.c` を新しく作る）。

### 2.5 Gen9（xd9）を外す理由（問い Q217-1）

`xd9-flip.c` は firmware が点けた plane の surface の register だけを書き換える作り（`xd9-flip.c:12-33` の説明、`i915_xd9_flip_arm(flip, surf, stride)`）で、PLANE_CTL を持たない。
180 を足すには PLANE_CTL の読み書きと takeover の検査（`xd-takeover.c:1681`）の緩めが要り、Gen9 の実機（Skylake・Kaby Lake の機械）が試験の台に無い。
compositor は hw180 の無い display で 180 を turn pass で出すので、Gen9 でも 180 は選べる（速さだけが違う）。→ 推し: この WS では Gen9 に hw180 を出さない。

### 2.6 試験（review-4 M27 の今の形）

| 試験 | 合格 |
| --- | --- |
| `host-xd-pipe-test.c` に項を足す | display version 11・12・13 で rotation 180 の PLANE_CTL の bit 1:0 が `10b`、0 で `00b`、他の bit は今と同じ |
| `host-xd-picture-test.c` に項を足す | 180 の flip で `pending_rotation`、latch で `current_rotation`、stuck・refused の後に live の回転が残る、flip しない経路で回転が違えば flip する |
| `plan/ws217/tests/host-gpu-display-flags.c`（新） | 印のある ops と無い ops で present・commit の `ROTATE_180` の受け・EOPNOTSUPP、登録の検査が新しい印を通す |
| build | `$(BUILD)/vmunix` の warning 0（amd64） |
| 5330・5320 の実機（T1、素の起動、script 1 本） | vkdemo `--transform=180` の間 ssh で PLANE_CTL（pipe A の plane 1）の bit 1:0 が `10b`、SURFLIVE が flip を追う、vkdemo を終えて通常の frame で `00b`。写真はユーザーの拾える一覧に（guardrail の i915 の節: register の証拠で代え、目視は節目だけ） |

guardrail「i915 の display の書き換えは 5330 で動く段ごとに」: p009 の i915 の変更は 5330 の回帰（起動・内蔵 LCD・desktop）に通るまで main に入れない。

## 3. p009 の libvulkan・vkdemo（第 4 版 §5.3 の行の引き直し）

中身は第 4 版 §5.3 のまま（display の `supportedTransforms` に 180、surface の `transform` を受ける、capabilities と swapchain は IDENTITY のまま、present と commit の呼び手で flag）。行は `wsi.c:125, 621`・`wsi-display.c:591-592`・`wsi-swapchain.c:1274`。
vkdemo は review-4 M25 のとおり新しい `vkdemo_display_open_on_transform(...)` を足し、今の `vkdemo_display_open_on` はそれを IDENTITY で呼ぶ（compositor の `compose.c:362`・`heads.c:993` は変えない。compositor は p010 で移る）。

## 4. compositor の段（p010・p005・p006・p012）と WS309

- WS309 の決まった構成（[ws309/ws.md](../ws309/ws.md) の表）は、回転・hotplug・modeset・swapchain の作り直しを「画面の制御」の常駐 thread に移し、
  ゲームモードの direct scanout を Vulkan display（`VK_KHR_display`）の swapchain で作る。第 4 版の §4.6（`kwl_output_rotate` の A・B の手順、open の前後の組の比較 C5・C7）・
  §4.11（REOPEN）・§4.12（game の session、`KWL_SCANOUT_UNROTATE`）は main thread の同期の手順として書かれており、そのまま実装すると WS309 が書き直す所を二重に作る。
- 決め（Q1、2026-10-29）: compositor の段は WS309 の構成の確定（Phase の分割）の後に、「画面の制御」thread の命令として設計を引き直す。その時に review-4 の C7・S12〜S16・M19〜M22・M26 を当てる。
  p006 のうち kernel・backend・libkeiland の部分（`kl_backend_input_uniq`、identity、`kl_system_touches_*` の答え）は thread に依らないので p006a として先に設計してよい（compositor の結び付けは p006b）。
- p012 の対象は game mode を Wayland の拡張で求めた client だけ（ws.md の 2026-10-13 ユーザーの決定、WS223）。WS223 は P6 が実装中で main に未 merge（master.md の P6 の行）。p012 は WS223 と WS309 の両方の後。

## 5. Phase の表（第 5 版、LW は ±50%、1 LW ≈ 実時間 15〜25 分）

| Phase | 内容 | 依存 | LW |
| --- | --- | --- | --- |
| p009-1 | Intel の PRM の確認 | — | 済み（[p009 §1〜§4](phase009/phase.md)） |
| p009 | UAPI の 3 つの定数と ops の印、核の検査（純粋な関数＋host 試験）、i915 xd（§2）、libvulkan・vkdemo（§3）、5330・5320 の実機 | p009-1 | 14（UAPI と核 2、i915 4、i915 の host 試験 2、libvulkan 3、vkdemo 1、実機の script と試験 2） |
| p011 | Pi 4 の 180°（第 4 版 §5.4 のまま、bcm2711 の commit の経路）| p009、WS214 p003a/b の clear | 8 |
| p006a | 入力の identity: `kl_backend_input_uniq`（2 file）、identity の整え、`displays.conf` の `touch=` の parse/format、libkeiland の `kl_system_touches_*` | p003（済み） | 6 |
| p010 | compositor の scanout の 180° と能力の policy（「画面の制御」thread の命令として） | p009、WS309 の該当の Phase | 10（WS309 の後に見積もり直す） |
| p005 | head の回転 | p010、p004（済み） | 12（同上） |
| p006b | compositor の結び付け・listen・Settings の card | p006a、p005 | 10（同上） |
| p012 | game の session の回転の停止 | p010、WS223、WS309 の game mode | 8（同上） |
| p007 | 確かめ（T1・実機・Pi） | 上の全部 | 6 |
| p008 | 規約の全文の見直し | p007 | 4 |

計 78 LW（うち WS309 の後の段 40 LW は見積もり直し）。先に始められる: p009（14）・p006a（6）、WS214 の後に p011（8）。

## 6. 選ばなかった案

- **i915 の xd に 180 を入れず全部 turn pass**: 速さの目標（ws.md の完了の条件 5「plane の回転が使えればそれを使い」）と、ユーザーの「Intelのscanoutの回転機能を使ってください」（2026-10-13）に反する。
- **compositor の段を WS309 の前に第 4 版のまま作る**: WS309 が thread の構成を変えるので二重の作業になる。p002a・p002b の main の上の code は WS309 が移す。
- **`drv_i915_xd_picture_flip_nowait` の旧い形を wrapper で残す**: 呼び手が 2 つで、同じ段で直せる。wrapper は回転を落とす道を残す。

## 7. ユーザーに聞く点（Q1 へ）

- Q217-1: Gen9（Skylake・Kaby Lake）の 180°。推し: hardware の回転は出さず、compositor の turn pass で 180（§2.5）。
- （報告だけ）順は Q1 の判断で決まった（§0 の 2）。

## 8. 出典

- Intel の公開の PRM（TGL Vol 2c・Vol 12、ACM Vol 2c・Vol 12）: [p009 の phase.md](phase009/phase.md) §2 の表（URL と版）。PLANE_CTL の bit 1:0 の値、PLANE_OFFSET の 180 の扱い。
- zedBSD の source（Zlib）: 上の file:行。GPL の source は読んでいない。
