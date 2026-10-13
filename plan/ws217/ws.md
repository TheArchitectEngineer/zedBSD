<!-- awesome-plan project=zedbsd record=ws217 -->

# WS217: 画面の回転（Settings の Display、縦長で Notes を使う）

<!-- awesome-plan-current:start -->
Status: planning（2026-10-13 Q1 作成。P4 が p001 の第 1 版、design-reviewer の [review-1](phase001/review-1.md)（致命的 2・重大 6・中 14・軽微 10）、P2 が全指摘に対応した第 2 版（[phase001/phase.md](phase001/phase.md)、agent/p2-ws215）。Q1 の review-2 と、残る問い D7〜D12（phase001 §9、推奨つき）待ち）
Resume point: p001 の review-2 の後、p002（compositor の中核。Q1 の判断で p002a/p002b に割ってよい）から。D5（Notes の toolbar の幅）は p007 で測る。Pi 4 の回転の受け入れは WS141 p015 の後（D7 の推奨）。
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

設計の要点（p001 第 2 版 §0）: どの driver も plane の回転を持たないので compositor が回す（論理の大きさの turn.image に描き、最後の 1 pass で swapchain へ回して写す。0° は今の経路のまま。Pi 4 は raster→UIF の変換が要るので WS141 p015 の後）。物理の mode は `compose->mode_width/height`、論理は `server->width/height`（書くのは 1 つの関数）。相対の入力は変えず、絶対は `INPUT_PROP_DIRECT` の device（touch screen・ペン LCD・QEMU の tablet）だけ物理→論理に写し、机の上の pen tablet は論理に直に（回さない）。pen・touch は identity（bus・vendor・product・serial）で display に結ぶ（Settings の card で選ぶ／「Touch to assign」で compositor が listen）。protocol は `kl_system_displays_v1` の manager version 34（案）の `set_rotation`・`bind_touch`・`listen_touch`、libkeiland は getter（ABI 不変）、wl_output は transform normal のまま mode を論理に（案 E）、保存は `displays.conf` の `rotate=KEY 90`・`touch=IDENTITY KEY`。回転した出力では game mode を合成に落とし、cursor の plane を使わない。

| Phase | 内容 | Status | 依存 |
| --- | --- | --- | --- |
| [p001](phase001/phase.md) | 設計: compositor と display の回転の持ち方、入力の座標の変換、pen・touch と display の結び付け、Settings と保存の形、段の分け方、試験 | planning（第 2 版 2026-10-13 P2、review-1 の全指摘に対応。Q1 の review-2 と D7〜D12 待ち） | — |
| p002 | compositor の中核（anchor）: `rotate.c`、`mode_width/height` と `kwl_output_size_set()`、turn（画像・2 つの pass・layout の helper・`turn.frag`）、damage（scene は今の frame、turn は age の union）、scene/backdrop/layers/glass の大きさ、`kwl_output_rotate`（資源を先に作る・park・touch の cancel・同じ物理の点）、`displays.conf` の `rotate=`（open の入口で適用）、wl_output の論理（案 E）、scanout の `rotated`、cursor の plane の停止、`--rotate=N`、`KWL OUTPUT rotate`、`SHOT`/`DISPLAYS` の `rotation=`、`SHOT partial` | 案（p001 の review-2 の後。Q1 の判断で p002a/p002b に割ってよい） | p001、WS213 p004 の scoped な出力 |
| p003 | 入力（anchor）: `struct kwl_cursor_geometry`（warp/outputs の丸ごと）、`kwl_output_absolute()`（direct は物理→論理、非 direct は論理に直に）、touch・tablet・受け箱の置き換え、`INPUT_PROP_DIRECT` の分類、傾きの回転、kernel の hid-input の pen の property の規則、注入の pen の DIRECT と `PEN_POINTER`（D12）、Linux・FreeBSD の backend の `EVIOCGPROP`（D11） | 案 | p002 |
| p004 | protocol・libkeiland・Settings の回転: manager version 34（`set_rotation`・`rotation`）、`kl_system_displays_rotation/set_rotation`、KL_VERSION 93（Q1 が番号）、`page-display.c` の倒す向きの 4 つの絵と card の印、shown でない display の論理の snapshot | 案 | p002、p003 |
| p005 | head の回転: `head->mode_*`、head の turn、extended と mirror（元を scene の画像に、論理の fit、各 display が自分の向きで正立）、配置の再検査 | 案 | p002、p004 |
| p006 | pen・touch と display の結び付け: `kl_backend_input_uniq`（3 backend）、identity、`touch=`、結び付けの引き直し、`kwl_pointer_warp_to(slot)`、listen（「この画面に触って」）、protocol（`bind_touch`・`listen_touch`・`touch`）、libkeiland の `kl_system_touches_*`、Settings の「Touch and pen」の card と「Touch to assign」 | 案（Q1 の判断で p006a/p006b に割ってよい） | p003、p005 |
| p007 | 確かめ: T1 の scenario の全部（回転・部分 frame・入力・Settings・head・結び付け・game mode・switch/hotplug・lock/login・OSK・Wiseview・窓の refit・0° の回帰）、5330・5320（ユーザー: 内蔵 panel とペン LCD の縦置きで Notes）、D5 の Notes の幅、Pi 4 は WS141 p015 の後（D7） | 案 | p002〜p006 |
| p008 | 規約の全文の見直し（p007 の直しの後） | 案 | p007 |

## ユーザーの決定（2026-10-13）

「ではあとは推奨でお願いします」 → 推奨で決定。D1 は数字ではなく倒す向きの絵で選ぶ（内部・file は時計回りの度数）、D2 hardware の回転はやらない（Future Work）、D3 は回転と一緒に pen・touch と display の結び付けを入れる（Settings で選ぶ＋「この画面に触って」で決める、自動の推測は後で。机の上の画面の無い pen tablet は INPUT_PROP_DIRECT で分けて回さない）、D4 すぐ反映・確認なし、D5 p006 で先に測る、D6 回転した出力の game mode は合成に落とす、mirror は各 display が自分の回転に対して正立、回転した出力では cursor の plane を使わない（review-1 S3）。

2026-10-13 ユーザーの確認（原文）:「画面ごとに向きを持つ、ペン・タッチと画面の結び付ける」→ 回転は display ごと（mirror でも各 display が自分の向きを持つ、GNOME の共有の形は採らない）、pen・touch と display の結び付けをこの WS に含める（上の D3 の決定の確認）。

2026-10-13 Q1（第 2 版の残る問い D7〜D12）: ユーザーの「ではあとは推奨でお願いします」に沿って推奨を採る: D7 Pi 4 の回転の受け入れは WS141 p015 の後、D8 度数に統一、D9 login 画面は回らない（制限として記録、Future Work）、D10 案 E（wl_output は transform normal・mode は論理）、D11 Linux・FreeBSD の backend に EVIOCGPROP の読みを足す、D12 UAPI に `INPUT_INJECT_KIND_PEN_POINTER` を足し注入の pen を DIRECT に（HAL ではない、Q1 の判断で可、ユーザーに報告）。番号の割当: `KL_SYSTEM_SINCE_ROTATION` = 34・`KL_SYSTEM_MANAGER_VERSION` = 34（実装の時に main の最大を確かめ直す）、KL_VERSION は実装の merge の時に Q1 が次の空きを割り当てる（93 は WS210 と取り合い）。
