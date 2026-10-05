<!-- awesome-plan project=zedbsd record=ws176 -->
# WS176: Canvas — pen 入力のイラストと画像の編集の app

Master: [master](../master.md)
Status: planning（2026-10-06 追加、**ベータ3**（ユーザー））
Primary Milestone: MG006

## 由来

ユーザー（2026-10-06）「もう1つアプリ追加です。これはベータ3でOKです。Canvasという名前の、イラスト作成のアプリです。ペン入力でイラストを作成するアプリです。画像編集にも使えます。レイヤー構成が使えます。画面をタッチで回転や移動や拡大しながらイラストを編集できます。~/Documents/Canvas/以下にパターンやブラシや背景素材などを突っ込んでおけば、CLIP STUDIO PAINTのようにプロのワークフローに耐える製作環境になります。ブラシがいろいろあり、筆、鉛筆、ペン、エアブラシ、アクリル、コピック、ぼかし、水彩、油彩など。スポイト、手ぶれ補正、筆圧調整、レイヤー、合成モード、バケツ、グラデーション、テキスト、選択、変形（拡大縮小、自由変形、メッシュ変形）、色調補正、フィルター、スクリーントーン集、吹き出し集、効果線集、コマ割り集、描き文字集、。左手デバイスのサポート、左手で操作できるタブレットUI。保存形式はCLIP STUDIO互換、Photoshop互換、エクスポートはPNG、JPEG。」

## 目標（単一）

pen と touch で、プロの作業に耐えるイラストの制作と画像の編集ができる app「Canvas」。

- 画面: touch で回転・移動・拡大縮小しながら描く。左手で操作できる tablet の UI、左手 device の対応。
- brush: 筆・鉛筆・pen・airbrush・acrylic・コピック風・ぼかし・水彩・油彩ほか。筆圧の調整、手ぶれの補正。
- 道具: スポイト、バケツ、グラデーション、テキスト、選択、変形（拡大縮小・自由変形・mesh 変形）、色調の補正、filter。
- layer と合成の mode。
- 素材の集: screen tone・吹き出し・効果線・コマ割り・描き文字。`~/Documents/Canvas/` に置いたパターン・brush・背景の素材を読む。
- 保存: CLIP STUDIO 互換・Photoshop（PSD）互換。書き出し: PNG・JPEG。

## 計画の注記（Q1）

- 規模が大きい（Q1 の概算 30 LW 以上）。p001 で段に分ける（段 1: canvas と layer・基本の brush・筆圧・touch の操作・PSD の保存と PNG・JPEG の書き出し、のように）。
- 前提: pen の筆圧と傾き（tablet の digitizer、WS159 の HID の digitizer の上）、GPU の描画（Vulkan、WS101 の compute の利用も候補）。
- **CLIP STUDIO の形式（.clip）は公開の仕様が無い独自形式**。互換の範囲（読みだけか、書きもか）と、解析で作ることの license・法の面を p001 で調べ、ユーザーに判断を出す。PSD は Adobe が仕様を公開している。
- 素材の集（tone・吹き出しなど）は自前で作るか、license の明らかな素材を取り込むか（AGENTS.md の外部の物の監査）。

## Phase

| Phase | 内容 | 状態 | 依存 |
| --- | --- | --- | --- |
| [ws176-p001](phase001/phase.md) | 要件の整理と段の分け方、形式の互換（.clip・PSD）の調査、設計。design-reviewer | planning（ベータ3） | WS159（pen の入力） |
