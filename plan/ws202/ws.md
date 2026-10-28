<!-- awesome-plan project=zedbsd record=ws202 -->

# WS202: libavcodec なしの H.264＋AAC の mp4 と .m4a の再生（libmedia の自前の decoder、Video Player・Music）

<!-- awesome-plan-current:start -->
Status: completed（2026-10-27 P3 の精査 q1608、ユーザー「…masterとWSを修正して、completedやcanceledにしてください。」: native の再生（libmedia の H.264・AAC）の software を main に統合、p013 の UAT は 2026-10-13 ユーザー「WS202のp013はUAT OKでした。」。p012 の残りは実機と T1 の確かめ → 2026-10-27 ユーザー「実機確認だけが残っているWSやPhaseは、古いものはclearedにしてしまいます。」）
Completed: 2026-10-27（q1608 の精査）
Primary Milestone: MG006
Related Milestones: —
Objectives: O2
Parent: [Master](../master.md)
Queue: なし
Resume point: なし（新しい要求は新しい WS として立てる。この WS は再開しない）
<!-- awesome-plan-current:end -->

## 目標

- libavcodec の無い image で、Video Player が H.264（Baseline・Main・High、8 bit 4:2:0 progressive）＋AAC-LC の mp4 を、音つき・同期・seek・終わりまで再生する。H.264 は Vulkan Video（[WS083](../ws083/ws.md)、i915 の VCS0）で decode する。
- libavcodec の無い image で、Music が .m4a（AAC-LC）を libmedia の自前の decoder で再生する。
- libavcodec の入った image で、今再生できる file を悪くしない。

設計は [design.md](design.md)（第 4 版の参照は [design-v4-reference.md](design-v4-reference.md)、review は [review-001.md](review-001.md)・[review-002.md](review-002.md)・[review-003.md](review-003.md)）。後のユーザーの決定は [policy-20261010.md](policy-20261010.md) が設計に優先する。

## ユーザーの決定

- 2026-10-11（由来）「libavcodecなしで、我々の独自のlibmediaの機能だけで、H.264とAACのmp4動画を再生できるようにしてください。また、libavcodecなしで、音楽アプリがlibmediaを使って.m4aを再生できるようにしてください。これはWSを立てて、設計をかいて」「libmediaと動画アプリ、音楽アプリは、WSを立てて設計だけ書いてください。実装は別なセッションで行います。」
- 関係する前の決定: 2026-10-07（WS120）「AAC は今は libavcodec、独自は後」。2026-10-11 Vulkan Video（WS083）は既定で ON。
- 2026-10-10（実装の会話の回答、[policy-20261010.md](policy-20261010.md) の「最新ユーザー決定」）: H.264 は Vulkan Video API で可否を問い合わせ、libmedia に機種の条件を書かない。QEMU・未対応 GPU で libmedia は誤りを返し、Video Player・Music が app のレベルで libavcodec（dlopen）へ fallback する。libmedia の backend に libavcodec を使わず、全機能を Zlib で自前に。AAC は AAC-LC だけ、HE-AAC は誤り（LC core だけを鳴らす degraded の経路は作らない）。「Huffmanの表は数値だけ抽出してください」。i915／libvulkan の標準 NV12 の読み戻しの補完は「i915／必要なlibvulkanの補完も含める（推奨）」。
- 2026-10-10 main への統合の承認（[main-integration-20261010.md](main-integration-20261010.md)）。
- 2026-10-08「コーディング規約による整形はベータ3でやります」（p014）。
- 2026-10-13「WS202のp013はUAT OKでした。」
- 2026-10-27（完了の根拠、q1608 経由）「実機確認だけが残っているWSやPhaseは、古いものはclearedにしてしまいます。」

## 結果

- libmedia に自前の AAC-LC の decoder と Vulkan Video の H.264 の back end（parser・DPB・参照の list・gap・seek）、共通の部品（bits・picture と scaler・sound の resample と trim）、mediafile の pasp・colr・end、app 所有の libavcodec の fallback（`userland/desktop/media-app/`）、`media-probe`、i915・libvulkan の標準 NV12 の読み戻し（p017）、利用者の文書を実装し main に統合した（2026-10-10）。
- host 試験と build の証拠は各 Phase と [main-integration-20261010.md](main-integration-20261010.md)・[playback-result-20261010.md](playback-result-20261010.md)・[aac-input-result-20261010.md](aac-input-result-20261010.md)・[h264-progress-20261010.md](h264-progress-20261010.md)。
- 5330 の UAT（p013）はユーザー「WS202のp013はUAT OKでした。」。

## 制限・移管

- 完了の条件 1（5330 で全 fixture の pts と hash の一致）・3（QEMU の AAT）・4（zgears の fps）の T1 と実機の確かめは未実施。実機の pixel（p017）・欠けた packet の実画素（p016）も未確認。
- 再生の slider が終わりで約 50% の件は [BUG-300](../bugs/BUG-300.md)。
- p014（規約の全文の見直しと残す試験の `plan/tools/media/` への登録）は WS の外の一括の見直しへ（2026-10-08 ユーザー「コーディング規約による整形はベータ3でやります」）。
- 非範囲: H.264 の CPU の decoder、HE-AAC の SBR・PS、他の codec、encode、interlaced・10 bit、再生の途中の libavcodec への切り替え。H.264 の自前の経路は Vulkan Video のある GPU（今は 5330 の i915）だけ。

## Phase 一覧

| Phase | 目的 | 結果 |
| --- | --- | --- |
| ws202-p001 | 設計と review | uncleared のまま WS を閉じた（software は main、全条件の確認は未完） |
| ws202-p002 | 試験の stream と参照、host 試験の枠 | uncleared のまま WS を閉じた（同上） |
| ws202-p003 | 共通の部品、自前の backend の表・誤りの分類・app 所有の fallback の境界 | uncleared のまま WS を閉じた（同上） |
| ws202-p004 | mediafile の pasp・colr・`end_us` | uncleared のまま WS を閉じた（同上） |
| ws202-p005 | AAC の構文と表 | uncleared のまま WS を閉じた（同上） |
| ws202-p006 | AAC の信号処理と back end | uncleared のまま WS を閉じた（同上） |
| ws202-p007 | Music の移行 | uncleared のまま WS を閉じた（同上） |
| ws202-p008 | H.264 の parser と DPB | uncleared のまま WS を閉じた（同上） |
| ws202-p009 | Vulkan Video の back end (1) | uncleared のまま WS を閉じた（同上） |
| ws202-p010 | Vulkan Video の back end (2)、media-probe の video、`config-media.mk` | uncleared のまま WS を閉じた（同上） |
| ws202-p011 | Video Player、media-probe の音、利用者の文書 | uncleared のまま WS を閉じた（同上） |
| ws202-p012 | T1: QEMU と 5330 の実機、zgears の fps | planned のまま WS を閉じた（ユーザーの UAT の分は OK、T1 の全項目は未実施） |
| ws202-p013 | 5330 の UAT（ユーザー） | cleared（2026-10-13、ユーザー「WS202のp013はUAT OKでした。」） |
| ws202-p014 | 規約の全文の見直しと残す試験の登録 | planned のまま WS を閉じた（WS の外の一括の見直しへ） |
| ws202-p015 | H.264 の欠けた参照・frame_num の gap・MMCO 5・seek の後の DPB | uncleared のまま WS を閉じた（software は main） |
| ws202-p016 | 参照の list の計算と欠けた参照の判定 | uncleared のまま WS を閉じた（host の証拠あり、実画素は未確認） |
| ws202-p017 | Vulkan の標準 NV12 の読み戻しの補完 | uncleared のまま WS を閉じた（build 済み、実機の pixel は未確認） |

## 記録の所在

Phase の計画・結果・試験は削除した。git の commit `fb1f6afee` の `plan/ws202/` にある。Queue の履歴は [plan/history](../history/index.md)。
