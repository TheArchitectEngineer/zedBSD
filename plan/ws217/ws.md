<!-- awesome-plan project=zedbsd record=ws217 -->

# WS217: 画面の回転（Settings の Display、縦長で Notes を使う）

<!-- awesome-plan-current:start -->
Status: planning（2026-10-13 Q1 作成。P4 が p001 の第 1 版、design-reviewer の [review-1](phase001/review-1.md)（致命的 2・重大 6・中 14・軽微 10）、P2 の第 2 版、Q1 の [review-2](phase001/review-2.md)（致命的 2・重大 3・中 10・軽微 15）、P2 の第 3 版、Q1 の [review-3](phase001/review-3.md)（致命的 2・重大 2・中 8・軽微 12）、P4 が全指摘と game mode の決定を入れた第 4 版（[phase001/phase.md](phase001/phase.md)、agent/p4）。Q1 の review-4 と、残る問い D18（video にも game の session の回転の停止を当てるか。推奨は game だけ）待ち）
Resume point: review-3 の判定で p002a（compositor の中核、turn 無し、open の比較は最初から組）と p009-1（Intel の PRM の確認）は第 4 版の後に始められる。p002b・p009 の実装・p010 以降は review-4 の後。p012（game の session）は D18 の後（game だけで先に実装してよい）。D5（Notes の toolbar の幅）は p007 で測る。Pi 4 の 180° は p011、90°・270° は WS141 p015 の後（D7）。
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
| [p001](phase001/phase.md) | 設計: scanout の 180° と compositor の 90/270、能力の policy と告知、論理と物理、入力の座標の変換、game の session の回転の停止、pen・touch と display の結び付け、Settings と保存の形、段の分け方、試験 | planning（第 4 版 2026-10-13 P4、review-3 の全指摘と game mode の決定に対応。Q1 の review-4 と D18 待ち） | — |
| [p002a](phase002a/phase.md) | compositor の中核、turn 無し: `rotate.c`（写像・plan・mask と why・`kwl_rotate_steps`）、`mode_*`・`size_*`・`kwl_output_size_set()`、`rotation_selected`／`rotation`、config の `rotate=`（`compose_display` と open の全経路、**open の前後の（論理の大きさ, rotation）の組の比較と手順**）、wl_output の論理（案 E）、scanout の fact と `KWL_SCANOUT_ROTATED`、cursor の plane の停止、`--rotate=N`、`KWL OUTPUT rotate`、`SHOT`/`DISPLAYS` の `rotation=`、`compose_target_*` の helper（0° の値）と p001 §1.8 の置き換え。0 以外は UNSUPPORTED | **cleared**（T1-576 の (A)(B)(C) PASS、Q1 判定 2026-10-13。実装 agent/p4 `b58ff4b06`） | p001、WS213 p004 の scoped な出力（main 済み） |
| [p002b](phase002b/phase.md) | turn: 画像・2 つの pass・pipeline・`turn.frag`・CLEAR/LOAD の選び方、damage、scene/backdrop/layers/glass の論理の大きさ（glass の関数に幅・高さの引数）、`kwl_output_rotate` の TURN の経路（先に作る・park・cancel・同じ物理の点・backdrop の retire・`persist`）、open の turn の資源の失敗 → 0、`--rotate-policy=any`、`SHOT partial` | planning（2026-10-13 P1 q948: 設計の第 1 版、design-reviewer の review 待ち） | p002a |
| p009-1 | Intel の公開の PRM で linear の 180 を確かめる段（引用と出典を p009 の phase.md に） | **planned（review-3 の判定で開始可）** | p001 |
| p009 | scanout の 180°（kernel・libvulkan）: UAPI の 3 つの定数と ops の印 `DRV_GPU_DISPLAY_ROTATE_180`、核の検査、i915（能力の印、**worker の present の item の `rotation`** → flip → `hw.rotation`）、libvulkan（display の `supportedTransforms` と surface の transform だけ、capabilities と swapchain は不変、present/commit の flag）、vkdemo の `open_on` の transform | 案（review-4 の後。p002 と並行可。実機 5330・5320 で受け入れ） | p009-1 |
| p010 | compositor の scanout の 180° と能力の policy: `scanout_rotation`、`hw180`（display の property）、surface の transform、REOPEN の経路（heads も閉じる）、`rotations` の mask と why（`WHY_RESOURCE`）、保存の角度を当てられない時の 1 つの規則 | 案 | p002b、p009 |
| p003 | 入力（anchor）: `struct kwl_cursor_geometry`、`kwl_output_absolute()`、touch・tablet・受け箱の置き換え、ROTATED の手順の geometry と同じ物理の点、direct の分類（touch screen と絶対の pointer は常に、pen は property）、傾き、kernel の hid-input の property の規則（BUG-267 の fixture）、注入の pen の DIRECT と `PEN_POINTER`（D12）、Linux・FreeBSD の backend の `EVIOCGPROP`（D11） | 案 | p002b（p010 と並行可） |
| p004 | protocol・libkeiland・Settings の回転: version 34 の wire の全部（requests 7・events 6、`rotation` は選択の値、`why` の 4 bit）と KL_VERSION の次の API の全部、compositor は `set_rotation`・`rotation`・`rotations` を実装し `bind_touch`・`listen_touch` は UNSUPPORTED、`page-display.c` の 4 つの絵と理由の文、shown でない display の論理の snapshot、`keiland-desktop` の policy の file | 案 | p010、p003 |
| p005 | head の回転: `head->mode_*`、head の turn と scanout の 180、extended と mirror（元を scene の画像に、論理の fit、各 display が自分の向きで正立）、配置の再検査と `heads_windows_follow`、head の cancel・refit、head の open の前後の組 | 案 | p010、p004 |
| p011 | Pi 4: bcm2711 の能力の印と HVS の反転（180°、3 倍の buffer の実験）、`display-flip-host-test`、rpi4 の build の `KWL_TURN_DISABLED`（90/270 は WS141 p015 の後に外す） | 案（Pi の実機で受け入れ） | p009、p010、WS214 p003a/p003b の clear |
| p006 | pen・touch と display の結び付け: `kl_backend_input_uniq`（2 file）、identity、`touch=`、結び付けの引き直し、mirror の fit の逆写像、contact の矩形（head の clamp、gesture は anchor だけ）、`kwl_pointer_warp_to(slot)`、listen（pen は down だけ）、`bind_touch`・`listen_touch`・`touch` の中身、libkeiland の `kl_system_touches_*`、Settings の「Touch and pen」の card と「Touch to assign」 | 案（Q1 の判断で p006a/p006b に割ってよい） | p003、p005 |
| **p012** | **game の session の回転の停止**（ユーザーの決定「ハードウェアでできない回転を使っているときにゲームモードにすると、回転が無効になる」、p001 §4.12）: session の定義、`KWL_SCANOUT_UNROTATE`、外す・戻す（保存しない、配置も保存しない、他の窓は動かさない、90° の資源は保つ）、期限、session の間の `set_rotation`、`WHY_GAME`。rpi4・head・180 の scanout は対象外 | 案（**D18（video）の決定待ち**、game だけで実装してよい） | p002b、p003、p004 |
| p007 | 確かめ: T1 の scenario の全部、5330・5320（ユーザー: 180° の scanout、内蔵 panel とペン LCD の縦置きで Notes）、D5 の Notes の幅、Pi 4 の 180°（p011）と 90/270 の記録（WS141 p015 待ち） | 案 | p002〜p006、p009〜p012 |
| p008 | 規約の全文の見直し（p007 の直しの後） | 案 | p007 |

T1 の束（p001 §13）: A = p002b+p003（QEMU）、B = p009+p010（実機）、C = p004+p005（QEMU）、D = p011（Pi）、E = p006+p012（QEMU）、F = p007。

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
2026-10-13 ユーザー（原文）:「D14～D17  推奨でいいです。」→ 上の Q1 の採った推奨を確定（D13 は Q1 の推奨のまま）。
2026-10-13 ユーザー（原文）:「Game Modeでは回転不要です。むしろ、ハードウェアでできない回転を禁止します。」→ Q1 の読み（ユーザーに確認中）: game mode の窓を回して合成に落とす D6 はやめる。90°・270°（compositor の回転）の出力では game mode に入らない（直の scanout も合成もしない、窓は通常の窓のまま）。180°（hardware）の出力の game mode は D17 のとおり今は直の scanout をしない。
2026-10-13 ユーザー（原文）:「ハードウェアでできない回転を使っているときにゲームモードにすると、回転が無効になる、でOKです」→ 決定: 90°・270°（compositor の回転）の出力で game mode に入ると、その間は回転を無効にして 0° の物理の向きで game mode の直の scanout をする（turn pass を外す、park の中で切り替え、入力の写像も 0° に）。game mode を出たら元の回転に戻す。D6（回した出力の game mode を合成に落とす）は置き換え。180°（hardware）の出力の game mode は D17 のまま（今は直の scanout をしない、Future Work）。次の設計の版で反映。

2026-10-13 Q1: review-3（致命的 2 C5・C6、重大 2 S10・S11、中 8、軽微 12）。判定: p002 全体（p002b）はまだ不可、第 4 版で C5（open の比較を（論理の大きさ, rotation）の組に、rotation の変化で cancel・geometry・resync・tell）・C6（game mode で回転を外す決定を game の session と直の scanout に分け、回転は session の間だけ外し保存しない、新しい段 p012）と M11〜M13 を直せば開始可。p002a（turn 無し、0 以外 UNSUPPORTED）は第 3 版のまま開始可（open の比較を最初から組で）。p009 は最初の段（Intel の PRM の確認）だけ今開始可、実装は第 4 版で S10（i915 の 180 は present の item で運ぶ）・S11（surface の transform を scanout が当て、capabilities と swapchain は IDENTITY のまま）・M17 の後。ユーザーへの問い D18（video の fullscreen にも game mode の回転の外しを当てるか、推奨は game だけ）。第 4 版の担当は次に空いた P。

2026-10-13 ユーザー（原文）:「Video Playerは、Game Modeをやめます。…Game Modeは別途、Wayland拡張で要求があったときのみ使用することにします。Game Modeを利用したアプリは現段階では存在しない、という整理にします。全画面ではコンポジターを利用しましょう。」→ D18 は不要（video は game mode に入らない、WS223）。p012 の対象は game mode を Wayland の拡張で求めた client だけ。

2026-10-13 Q1: review-4（致命的 1 C7、重大 5 S12〜S16、中 9、軽微 12）。p002a は C7（open の前後の比較の before は compose が持つ「最後に開いた・回した状態」と比べる。switch は open の前に server->width を書くので入口で取ると RESIZED が立たない、回転 0 でも壊れる）と L49 を p002a の phase.md に入れてから（S16 の steps の mode の引数も推奨）。p002b は C7・S12・M21 の後、p009 の実装は M23・M24・M25・M27 を p009 の phase.md に書いた後、p010 は S13 の後、p012 は第 5 版で §4.12 を直してから（WS223 p001 の game mode の求めの決定にも依る）。Q1 の判断: p012 を p007 の後ろへ回す（完了の条件は外れない、ユーザーに報告）。暫定の「turn の出力では game mode に入らない」は安全。

2026-10-13 Q1（WS223 p001 の依頼、第 5 版で反映）: p012 の session の定義は `KWL_SCANOUT_CONTENT_GAME`（WS223 の推奨 (a) wp_content_type_v1 の game のまま）。p012 と §12.2 の回帰の項 7・9 は WS223 の p002（規則から video を外す）・p003（wltest に `--content-type=`）に依存。§4.12 の「video は D18」・§10 の D18・§12.2 の 9 の末尾「Videoplayer は reason=rotated のまま」は「video は content type で合成・log 無し」に直す。
