<!-- awesome-plan project=zedbsd record=ws225 -->

# WS225: Vulkan の header を仕様から導ける内容だけで書き直し、Khronos の著作権表示を外す

<!-- awesome-plan-current:start -->
Status: planning（2026-10-13 Q1 作成。**blocked**: ユーザーの指示で着手しない。ベータ2 で実装する）
Primary Milestone: MG006
Related Milestones: なし
<!-- awesome-plan-current:end -->

## 原文（2026-10-13 ユーザー）

「Khronosの著作権表示を外した、仕様から導ける内容のみで構成されたヘッダへの書き直しを、新しいWSとして追加します。これはまだ着手せず、ブロッキングにしておきます。ベータ2で実装します。」

（経緯: 同じ日に WS224 で、今の header を派生物として扱い zedBSD の file header の下に Khronos の表示を別の block で入れ、API ごとの解説を足すと決めた。この WS はその先の、Khronos の header の文に依らない書き直し。）

## 目的と完了の条件（案、着手の時に p001 で確定）

1. `include/libc/vulkan/` の header を、Vulkan の仕様（公開の仕様書の本文と、そこから導ける名前・型・値・構造体の field の並び）だけを入力にして、zedBSD の構成・順番・書き方で書き直す。Vulkan-Headers の file や registry の XML の文を入力にしない（入力に何を使うかの規則と出典を p001 で決め、記録する）。
2. Khronos の著作権表示を外し、zedBSD の表示と、仕様の版（Vulkan 1.3 の仕様の revision）の引用だけにする。WS224 の API ごとの解説は保つ。
3. 名前・型・値・field の並びは互換のため今の header と同じ（機械で今の header と ABI を比べる試験: 型の大きさ・offset・enum の値・関数の型）。
4. libvulkan と Vulkan を使う全部の build が warning 0、libvulkan の host 試験・ABI の比較の試験・license の inventory の試験が PASS。法的な最終判断はユーザー。

## Phase

| Phase | 内容 | Status |
| --- | --- | --- |
| p001 | 入力の規則と出典、書き直しの方法（道具か手か）、ABI の比較の試験の設計 | planned（blocked、ベータ2） |

2026-10-13 ユーザー（原文）:「さきほどの、独自VulkanヘッダにするWSはブロッキングのままです。」→ blocked のまま。先に WS226（Khronos の Vulkan 1.3 の header をそのまま使い、libvulkan で 1.3 を実装しきる）を行う。この WS を始める時は WS226 の 1.3 の API の全部を入力にする。
