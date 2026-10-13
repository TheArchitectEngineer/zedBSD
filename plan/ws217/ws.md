<!-- awesome-plan project=zedbsd record=ws217 -->

# WS217: 画面の回転（Settings の Display、縦長で Notes を使う）

<!-- awesome-plan-current:start -->
Status: planning（2026-10-13 Q1 作成。P4 が p001 の第 1 版、design-reviewer の [review-1](phase001/review-1.md)（致命的 2・重大 6・中 14・軽微 10）、P2 の第 2 版、Q1 の [review-2](phase001/review-2.md)（致命的 2・重大 3・中 10・軽微 15）、P2 が全指摘と「scanout の回転」の決定を入れた第 3 版（[phase001/phase.md](phase001/phase.md)、agent/p2-ws215）。Q1 の review-3 と、残る問い D13〜D17（phase001 §10、推奨つき）待ち）
Resume point: p001 の review-3 の後、p002（compositor の中核と turn。Q1 の判断で p002a/p002b に割ってよい）と p009（scanout の 180°: UAPI・i915・libvulkan、p002 と並行可）から。D5（Notes の toolbar の幅）は p007 で測る。Pi 4 の 180° は p011、90°・270° は WS141 p015 の後（D7）。
Primary Milestone: MG006
Related Milestones: なし
<!-- awesome-plan-current:end -->

## 原文（2026-10-13 ユーザーの UAT）

「・SettingsのDisplay設定に、画面回転を追加してほしいです。縦長でNotesを使いたいです。」

## 目的と完了の条件

1. Settings の Display に display ごとの回転（0°・90°・180°・270°）があり、選ぶとすぐ反映し、再起動後も保たれる。
2. 回転した画面で、窓・App Home・Wiseview・system bar・スクリーンキーボードの配置が新しい縦横に合う。
3. touch panel・ペン（tablet）・mouse・touchpad の座標が回転に合う（ペンで Notes に縦長で書ける）。
4. i915（5320・5330）で動く。Pi 4（vc4）・QEMU でも動くか、動かない所を記録する。
5. 描画の速さが大きく落ちない（plane の回転が使えればそれを使い、使えなければ compositor の側で回す）。
6. （2026-10-13 ユーザーの確認で追加）pen・touch と display の結び付けが Settings で選べ、「この画面に触って」でも決められ、再起動後も保たれる。結び付けの無い device は主の出力に写る（今の挙動）。

## Phase

設計の要点（p001 第 3 版 §0）: **180° は表示の hardware が回す**（UAPI に plane の回転の印 `GPU_DISPLAY_ROTATE_180`・`PRESENT_ROTATE_180`・`PLANE_ROTATE_180`、i915 は既存の `PLANE_CTL_ROTATE_180` の計算に `hw.rotation` を通す、libvulkan は display の `supportedTransforms`・surface の `transform`・swapchain の `preTransform` に ROTATE_180 を受ける、compositor は surface を transform つきで開き直す。Pi 4 は HVS の 2 つの反転 bit）。**90°・270° は compositor の turn pass**（論理の大きさの turn.image に描き 1 pass で swapchain へ。Intel は全部。Pi 4 は WS141 p015 の後、それまで rpi4 の build で止めて無効表示）。**能力の policy**: hardware が 180 を回せない display（Venus、KMS、古い driver）は回転を一切出さず、display ごとの選べる角度と理由を protocol の `rotations` で Settings へ（無効表示）。試験のためだけに `--rotate-policy=any`。物理の mode は `compose->mode_width/height`、論理は `server->width/height`（書くのは 1 つの関数の 2 行、回転は `compose_display` で config から当て、open の全経路で論理の大きさを前後で比べる）。相対の入力は変えず、touch screen と絶対の pointer は常に direct（物理→逆回転→論理）、pen は `INPUT_PROP_DIRECT` で分け、机の上の pen tablet は論理に直に。head に結んだ指は head の矩形で clamp（edge・corner・OSK の gesture は anchor だけ）、mirror は fit の逆写像で anchor へ。pen・touch は identity（bus・vendor・product・serial、空白は `_`）で display に結ぶ（Settings の card／「Touch to assign」、pen は down だけ）。protocol は manager version 34 の `set_rotation`・`bind_touch`・`listen_touch` と event `rotation`・`rotations`・`touch`（wire は p004 で全部）、libkeiland は getter（ABI 不変）、wl_output は transform normal のまま mode を論理に（案 E）、保存は `displays.conf` の `rotate=KEY 90`・`touch=IDENTITY KEY`。回転した出力（180° の scanout も）では game mode を合成に落とし、cursor の plane と overlay を使わない。

| Phase | 内容 | Status | 依存 |
| --- | --- | --- | --- |
| [p001](phase001/phase.md) | 設計: scanout の 180° と compositor の 90/270、能力の policy と告知、論理と物理、入力の座標の変換、pen・touch と display の結び付け、Settings と保存の形、段の分け方、試験 | planning（第 3 版 2026-10-13 P2、review-2 の全指摘と scanout の決定に対応。Q1 の review-3 と D13〜D17 待ち） | — |
| p002 | compositor の中核と turn（anchor）: `rotate.c`（写像・plan・policy）、`mode_*`・`size_*`・`kwl_output_size_set()`、config の `rotate=`（`compose_display` と open の全経路、論理の前後の比較）、turn（画像・2 つの pass・pipeline・`compose_target_*` の helper・`turn.frag`・CLEAR/LOAD の選び方）、damage、scene/backdrop/layers/glass の大きさと `output_resized(server, glass)`、`kwl_output_rotate`（turn の経路）、wl_output の論理（案 E）、scanout の `rotated`、cursor の plane の停止、`--rotate=N`・`--rotate-policy=any`、`KWL OUTPUT rotate`、`SHOT`/`DISPLAYS` の `rotation=`、`SHOT partial` | 案（p001 の review-3 の後。Q1 の判断で p002a/p002b に割ってよい） | p001、WS213 p004 の scoped な出力 |
| p009 | scanout の 180°（kernel・libvulkan）: UAPI の 3 つの定数と核の検査、i915（能力の印、present/commit → `hw.rotation`、Intel の PRM で linear の 180 を確かめる段）、libvulkan（`supportedTransforms`・surface の transform・`preTransform`・present/commit の flag）、vkdemo の `open_on` の transform | 案（p002 と並行可。実機 5330・5320 で受け入れ） | p001 |
| p010 | compositor の scanout の 180° と能力の policy: `scanout_rotation`、`hw180`、surface の transform、reopen の経路、`rotations` の mask と why、保存の角度が mask に無い時 | 案 | p002、p009 |
| p003 | 入力（anchor）: `struct kwl_cursor_geometry`、`kwl_output_absolute()`、touch・tablet・受け箱の置き換え、direct の分類（touch screen と絶対の pointer は常に、pen は property）、傾き、kernel の hid-input の property の規則（BUG-267 の fixture）、注入の pen の DIRECT と `PEN_POINTER`（D12）、Linux・FreeBSD の backend の `EVIOCGPROP`（D11） | 案 | p002 |
| p004 | protocol・libkeiland・Settings の回転: version 34 の wire の全部（requests 7・events 6）と KL_VERSION の次の API の全部、compositor は `set_rotation`・`rotation`・`rotations` を実装し `bind_touch`・`listen_touch` は UNSUPPORTED、`page-display.c` の 4 つの絵と無効表示の理由、shown でない display の論理の snapshot | 案 | p010、p003 |
| p005 | head の回転: `head->mode_*`、head の turn と scanout の 180、extended と mirror（元を scene の画像に、論理の fit、各 display が自分の向きで正立）、配置の再検査と `heads_windows_follow`、head の cancel・refit | 案 | p010、p004 |
| p011 | Pi 4: bcm2711 の能力の印と HVS の反転（180°）、`display-flip-host-test`、rpi4 の build の `KWL_TURN_DISABLED`（90/270 は WS141 p015 の後に外す） | 案（Pi の実機で受け入れ） | p009、p010、WS214 p003a/p003b の main への統合 |
| p006 | pen・touch と display の結び付け: `kl_backend_input_uniq`（2 file）、identity、`touch=`、結び付けの引き直し、mirror の fit の逆写像、contact の矩形（head の clamp、gesture は anchor だけ）、`kwl_pointer_warp_to(slot)`、listen（pen は down だけ）、`bind_touch`・`listen_touch`・`touch` の中身、libkeiland の `kl_system_touches_*`、Settings の「Touch and pen」の card と「Touch to assign」 | 案（Q1 の判断で p006a/p006b に割ってよい） | p003、p005 |
| p007 | 確かめ: T1 の scenario の全部、5330・5320（ユーザー: 180° の scanout、内蔵 panel とペン LCD の縦置きで Notes）、D5 の Notes の幅、Pi 4 の 180°（p011）と 90/270 の記録（WS141 p015 待ち） | 案 | p002〜p006、p009〜p011 |
| p008 | 規約の全文の見直し（p007 の直しの後） | 案 | p007 |

T1 の束（p001 §13）: A = p002+p003（QEMU）、B = p009+p010（実機）、C = p004+p005（QEMU）、D = p011（Pi）、E = p006（QEMU）、F = p007。

## ユーザーの決定（2026-10-13）

「ではあとは推奨でお願いします」 → 推奨で決定。D1 は数字ではなく倒す向きの絵で選ぶ（内部・file は時計回りの度数）、D2 hardware の回転はやらない（Future Work）、D3 は回転と一緒に pen・touch と display の結び付けを入れる（Settings で選ぶ＋「この画面に触って」で決める、自動の推測は後で。机の上の画面の無い pen tablet は INPUT_PROP_DIRECT で分けて回さない）、D4 すぐ反映・確認なし、D5 p006 で先に測る（第 1 版の段の番号。第 2 版以降は p007）、D6 回転した出力の game mode は合成に落とす、mirror は各 display が自分の回転に対して正立、回転した出力では cursor の plane を使わない（review-1 S3）。

2026-10-13 ユーザーの確認（原文）:「画面ごとに向きを持つ、ペン・タッチと画面の結び付ける」→ 回転は display ごと（mirror でも各 display が自分の向きを持つ、GNOME の共有の形は採らない）、pen・touch と display の結び付けをこの WS に含める（上の D3 の決定の確認）。

2026-10-13 Q1（第 2 版の残る問い D7〜D12）: ユーザーの「ではあとは推奨でお願いします」に沿って推奨を採る: D7 Pi 4 の回転の受け入れは WS141 p015 の後、D8 度数に統一、D9 login 画面は回らない（制限として記録、Future Work）、D10 案 E（wl_output は transform normal・mode は論理）、D11 Linux・FreeBSD の backend に EVIOCGPROP の読みを足す、D12 UAPI に `INPUT_INJECT_KIND_PEN_POINTER` を足し注入の pen を DIRECT に（HAL ではない、Q1 の判断で可、ユーザーに報告）。番号の割当: `KL_SYSTEM_SINCE_ROTATION` = 34・`KL_SYSTEM_MANAGER_VERSION` = 34（実装の時に main の最大を確かめ直す）、KL_VERSION は実装の merge の時に Q1 が次の空きを割り当てる（93 は WS210 と取り合い）。

2026-10-13 Q1: review-2 の判定「p002 はまだ始められない」（致命的 2 C3・C4、重大 3、中 10、軽微 15）。第 3 版で C3（turn pass の部分 frame は pass_load）・C4（回転は compose_display の中で当て、open の経路で論理の大きさが変われば resize の手順）と p002 に関わる M1〜M4 を直せば開始可。新しい問い S8（mirror の中の結び付けは各 display の回転で写す）と M8（WS141 p015 の前の Pi では回転を UNSUPPORTED）は推奨を採る（ユーザーに報告、変更可）。第 3 版の担当は次に空いた P。

## ユーザーの決定（2026-10-13、scanout の回転）

ユーザー（原文）:「はい、Intelのscanoutの回転機能を使ってください。KHR_displayでできそうですか？」「Venusでは回転できなくていいです。画面サイズを変えればいいだけなので。この場合は回転機能を無効表示にします。」→ Q1 が Linux i915 v6.17 の `skl_universal_plane.c:2959-2964`（display version 13 以降は plane の回転が 0°・180° だけ、12 以前は 90°・270° も、90/270 は tile の並びが条件）を示し、案を出した。ユーザー:「はい、回転はそれで実装してください。」→ 決定:
- 180°: Intel は全部、表示の hardware の回転（KHR_display の `VkDisplaySurfaceCreateInfoKHR.transform` と surface の `supportedTransforms`、swapchain の `preTransform`。libvulkan の IDENTITY 以外の拒否を外し、UAPI に plane の回転を足し、i915 で plane の回転を有効に）。Pi 4 は HVS の左右・上下の反転の組で 180°。
- 90°・270°: Intel は全部（5320 の Tiger Lake も）compositor の GPU の回転（第 2 版の turn pass）。Y-tiled の作業はしない（Future Work）。Pi 4 は WS141 p015 の後に compositor で、それまでは無効表示。
- Venus（QEMU）: 回転は無効表示（画面の大きさを変えればよい）。
- 第 3 版で review-2 の C3・C4・M1〜M4 と合わせてこの形にする。担当は次に空いた P。

2026-10-13 Q1（第 3 版の残る問い D13〜D17）: ユーザーの「推奨でいい」の流れに沿って推奨を採り、ユーザーに報告（変更可）: D13 test image だけ `--rotate-policy=any`、D14 hw180 の無い display は回転を出さない（Linux・FreeBSD の build も無効表示、KMS の rotation は Future Work）、D15 Pi の HVS の反転はまず tree と実機、GPL の参照は分からなかった時だけ（その時はユーザーに）、D16 UAPI の 3 つの定数（90/270 の bit は作らない）、D17 180 の scanout の出力で game mode の直の scanout はしない（Future Work）。
