<!-- awesome-plan project=zedbsd record=ws217 -->

# WS217: 画面の回転（Settings の Display、縦長で Notes を使う）

<!-- awesome-plan-current:start -->
Status: planning（2026-10-13 Q1 作成。P4 が p001 の設計の第 1 版を書いた（[phase001/phase.md](phase001/phase.md)、agent/p4）。design-reviewer と Q1 の確かめ、ユーザーの判断 D1〜D6（phase001 §9）待ち）
Resume point: p001 の review の後、p002（compositor の中核）から。D3（ペン LCD は唯一の出力か主の出力の時だけ）と D5（Notes の toolbar の幅）はユーザー。
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

## Phase

設計の要点（p001 §0）: どの driver も plane の回転を持たないので compositor が回す（論理の大きさの画像に描き、最後の 1 pass で swapchain へ回して写す。0° は今の経路のまま）。相対の入力は変えず、絶対（touch・pen）は物理→論理に写す。protocol は `kl_system_displays_v1` v20 の `set_rotation`、保存は `displays.conf` の `rotate=KEY 90`。

| Phase | 内容 | Status | 依存 |
| --- | --- | --- | --- |
| [p001](phase001/phase.md) | 設計: compositor と display の回転の持ち方（hardware の plane の回転か compositor の合成か）、入力の座標の変換、Settings と保存の形、段の分け方、試験 | planning（第 1 版 2026-10-13 P4、design-reviewer と D1〜D6 待ち） | — |
| p002 | compositor の中核（anchor）: `rotate.c`、論理/物理の分離、turn（画像・pipeline・shader・pass）、damage の写像、scene/backdrop/layers/glass の大きさ、`kwl_output_rotate`（park の中）、`displays.conf` の `rotate=`、wl_output の transform、scanout の `rotated`、`--rotate=N` と `KWL OUTPUT rotate` の行 | 案（p001 の review の後。Q1 の判断で p002a/p002b に割ってよい） | p001 |
| p003 | 入力と cursor の plane: 絶対の 3 経路（受け箱・touch・tablet）の物理→論理、受け箱の rotation と物理の大きさ、C の位置の写像、M の像と hotspot の回転、同じ物理の点への pointer の warp | 案 | p002 |
| p004 | protocol・libkeiland・Settings: `kl_system_displays_v1` v20（`set_rotation`・`rotation`）、`struct kl_display.rotation`・`kl_system_displays_set_rotation`・KL_VERSION、`kl_window_output_mode` の入れ替え、`page-display.c` の row の回転の button と card の印 | 案 | p002（p003 と並行可） |
| p005 | head の回転（extended の head の turn と配置の再検査、mirror の元を scene の画像に） | 案 | p002、p004 |
| p006 | 確かめ: T1 の scenario（Settings から 90/180/270/0、注入の touch と pen、再起動後の保持、0° の回帰）、5330（ユーザー: 内蔵 panel とペン LCD の縦置きで Notes）、Pi 4 の計測、D5 の Notes の幅 | 案 | p002〜p005 |
| p007 | 規約の全文の見直し | 案 | p002〜p005 |

## ユーザーの決定（2026-10-13）

「ではあとは推奨でお願いします」 → 推奨で決定。D1 は数字ではなく倒す向きの絵で選ぶ（内部・file は時計回りの度数）、D2 hardware の回転はやらない（Future Work）、D3 は回転と一緒に pen・touch と display の結び付けを入れる（Settings で選ぶ＋「この画面に触って」で決める、自動の推測は後で。机の上の画面の無い pen tablet は INPUT_PROP_DIRECT で分けて回さない）、D4 すぐ反映・確認なし、D5 p006 で先に測る、D6 回転した出力の game mode は合成に落とす、mirror は各 display が自分の回転に対して正立、回転した出力では cursor の plane を使わない（review-1 S3）。

2026-10-13 ユーザーの確認（原文）:「画面ごとに向きを持つ、ペン・タッチと画面の結び付ける」→ 回転は display ごと（mirror でも各 display が自分の向きを持つ、GNOME の共有の形は採らない）、pen・touch と display の結び付けをこの WS に含める（上の D3 の決定の確認）。
