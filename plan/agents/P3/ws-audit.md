<!-- awesome-plan project=zedbsd record=p3-ws-audit-q1608 -->

# 未完の WS の精査（q1608、P3、2026-10-27）

ユーザーの指示（原文、2026-10-27 04:24）:「すみません、残っているとされたWSの大半が、すでに別なWSで完了していたり、私がブロックしたりキャンセルしたりしたものでした。サブエージェントP3を立てて、残っているWSを精査してください。masterとWSを修正して、completedやcanceledにしてください。」

途中の補足（Q1 経由、ユーザーの原文）:

- 「…あなたのこれまでの判断が正しいという証拠を集めなくていいので、私の言っていることが正しいことを前提に正しい可能性を探って、一貫性がなくなってしまった計画書全体を修正していきましょう。P3にはそれをまかせたいです」
- 「実機確認だけが残っているWSやPhaseは、古いものはclearedにしてしまいます。P3にお伝えください。」
- 「つまり、プロジェクトは一貫性を持った指示の下で運用されているが、記録が複数のエージェントによって破壊されてしまったため、Claudeが混乱して本気を出せないでいると考えています。」
- 「P3にも、P4の取り組みや、プロジェクトの記録上での一貫性が破壊されてしまったことを伝え、コードの実装を正として、判断できない方針は私に質問するようにお伝えください。」

## 判定の規則（適用した物）

- completed: 目標が main の code・ユーザーの確かめ・別の WS で実質達成。残りが実機の確かめだけの物も completed（上の 10-27 の原文、制限に「実機の確認は未実施」）。規約の全文の見直しだけが残る物も completed（2026-10-08 ユーザー「規約の整形（各 WS の全文規約の Phase）はベータ3」→ WS の外の一括の見直しへ）。
- 保留: ユーザーが止めた・「ベータ3」「ベータ4 以降」「後日」「要検討」・設計だけで実装は後日・優先の外で止まったままの物。
- canceled: 別の物で置き換わった物（理由と行き先を書いた）。
- 継続: 今動いている物（WS074 は Codex のセッション）。対象の外: WS290・WS291・WS292・WS295・WS296（今の作業）。

## 集計

| 新しい状態 | 件数 |
| --- | --- |
| completed | 86 |
| 保留 | 59 |
| canceled | 3 |
| 継続 | 2 |
| 計 | 150 |

ユーザー（2026-10-27、Q1 経由）「なお、ベータ3，ベータ4という概念はキャンセルされました。rev.5までが公開されており、実質的にこれがベータ2でした。ベータ3でやること、ベータ4でやること、という内容は、今からQ1と私で整理していきます。」→ ベータ3・4 を理由の保留は「保留（ベータ3・4 の段は取り消し、何をやるかは Q1 とユーザーが整理中）」にし、元の段を列に残した。

各 ws.md の Status 行を下の新しい状態・根拠・旧の値に置き換えた（WS074・WS068 は継続で変えていない）。Phase の表と phase.md は直していない（下の「Phase の後始末の一覧」、Q1 の指示 2026-10-27: P4 の新しい形の後に Q1 がまとめて流す）。

## WS ごとの判定

| WS | 元の状態 | 新しい状態 | 元の段（保留だけ） | 根拠（出典の引用・記録・commit） | 残り・制限 |
| --- | --- | --- | --- | --- | --- |
| [WS001](../../ws001/ws.md) | incomplete | incomplete・保留（ベータ3・4 の段は取り消し、何をやるかは Q1 とユーザーが整理中） | ベータ3 | 2026-10-27 ユーザー「なお、ベータ3，ベータ4という概念はキャンセルされました。…ベータ3でやること、ベータ4でやること、という内容は、今からQ1と私で整理していきます。」。元の段の出典: 2026-10-07 ユーザー「下記をベータ3に移動します。・左手デバイスOSK、ゲームパッドOSK, 写真の続き, カレンダーの続き, IMEの続き、POSIX, NVMe, make, RTL8822C, Sleep」（ws001/ws.md Target、decisions-log）。旧 Master「WS001 はユーザーが指示したときだけ」 | p041〜p044 の guest 試験、p045 POSIX の header、p046〜p051、p040 mesg（BUG-067） |
| [WS004](../../ws004/ws.md) | incomplete（ベータ2） | completed |  | 主な USB・WLAN・NVMe の経路は完了（ws004/ws.md）。残りの p005・p006・p021・p025・p028〜p030・p038・p051 は実機の確かめだけ → 2026-10-27 ユーザー「実機確認だけが残っているWSやPhaseは、古いものはclearedにしてしまいます。」。AX211 は WS248・U9 で実機の接続 | 実機の確認は未実施。p034（QEMU の ECM、test-wait）、p035（deferred）・p039（任意の共通化） |
| [WS009](../../ws009/ws.md) | incomplete（ベータ2） | incomplete・保留（DOC-54） | DOC-54 | p001〜p008 は completed。残りの DOC-54（/dev/gpu の UAPI の文書）は ws009/ws.md で「Producer manual hold」のまま、GPU の UAPI は今も WS290 などで変わっている | DOC-54（docs/ に /dev/gpu の参照の文書は無い） |
| [WS013](../../ws013/ws.md) | incomplete（F-002 に保留） | incomplete・保留（Future Work F-002） | Future Work F-002 | ws013/ws.md「残り…は Future Work F-002 に保留し、昇格されるまで再開しない」、future-work.md F-002 deferred | F-002（runtime の隔離・CLI・service container） |
| [WS026](../../ws026/ws.md) | planning | planning・保留（ベータ3・4 の段は取り消し、何をやるかは Q1 とユーザーが整理中） | ベータ3 | 2026-10-27 ユーザー「なお、ベータ3，ベータ4という概念はキャンセルされました。…ベータ3でやること、ベータ4でやること、という内容は、今からQ1と私で整理していきます。」。元の段の出典: 2026-10-08 ユーザー「ベータ3にします：WS009・026・106 文書・試験の整理・試験アプリの集約、WS139 デスクトップの速さ」 | Phase 未定義 |
| [WS033](../../ws033/ws.md) | incomplete | completed |  | 有線 LAN の後挿し・抜去・networking.wait は p001 cleared（2026-10-14 Q1、T1-480・T1-513・T1-517）で、code は main（managed-lan.c・net lan・net startup）。p002 は実機の立会いだけ → 2026-10-27 ユーザー「実機確認だけが残っているWSやPhaseは、古いものはclearedにしてしまいます。」。p003 の規約は 2026-10-08 ユーザー「規約の整形（各 WS の全文規約の Phase）はベータ3」で一括の見直しへ | 実機の確認は未実施、規約の全文は一括の見直しへ |
| [WS037](../../ws037/ws.md) | incomplete | completed |  | nvrtx は main にあり既定で on（config/ci/config-amd64.mk）。RTX 2070 で passthrough の desktop（R8b〜R9k PASS、T1-678）と素の起動の desktop の session 5/5（T1-684）。速さは WS291（NVIDIA の p010・p011）、RTX 20/30/40 は WS242、NVDEC は WS243、取り消しと復帰は WS250 に分けてある | P4m の間欠の失敗（T1-691）、p001・p003〜p008 の Phase の印、p009 規約は一括の見直しへ |
| [WS039](../../ws039/ws.md) | incomplete | completed |  | amdrdna は main。RX 5600 XT で Keiland の desktop が描ける（p009 ユーザーの目視「RDNAは黄色の問題が解決しました。目視です。」）、p001〜p005・p008・p011・p013・p014・p016・p017 cleared。速さは WS291（P2、継続、p001 で全段 60 fps） | p006・p007・p012・p015・p019 の Phase の印、p018、p010 規約は一括の見直しへ |
| [WS046](../../ws046/ws.md) | incomplete（ベータ2） | completed |  | 2026-10-13 夜 ユーザー: WS046 p014 は「満たしたとみなす」→ cleared、p007 は T1-050 の証拠で cleared（decisions-log）。p008 の coreutils の cross build は p013 で通った、p011 は p012・p014 で置き換え。残りの p005 の規約の全文は 2026-10-08 ユーザー「規約の整形（各 WS の全文規約の Phase）はベータ3」で一括の見直しへ | 規約の全文は一括の見直しへ |
| [WS050](../../ws050/ws.md) | incomplete（ベータ2 範囲完了） | completed |  | p001〜p005 cleared、実機は 2026-10-08 夜 ユーザー「WS050・WS051…は実機で動作を確認」と 2026-10-13 ユーザー「UATの結果待ちのものは、すべてテスト済みなのでclearedにして進めたりcloseしたりしてください。」。残りの p006 の規約の全文は 2026-10-08 ユーザー「規約の整形…はベータ3」で一括の見直しへ | 規約の全文は一括の見直しへ |
| [WS051](../../ws051/ws.md) | incomplete | completed |  | p001〜p005a cleared、p004c は 2026-10-13 ユーザーの一括の cleared。2026-10-08 ユーザー「USB-C DPでディスプレイ出力ができました！」「拡張モードで出力されました。」「ミラーもうまくいきました。」。残りの p005b（抜けの停止・IRQ_HPD の retrain）と p006 の対象の旧 file（tc.c・dp-ext 等）は WS245 p039・p040・p052 で xd に書き直し（5330 の回帰 PASS、WS245 completed 2026-10-27） | S0ix の口（WS052 と一緒、ベータ3） |
| [WS052](../../ws052/ws.md) | incomplete | incomplete・保留（ベータ3・4 の段は取り消し、何をやるかは Q1 とユーザーが整理中） | ベータ3 | 2026-10-27 ユーザー「なお、ベータ3，ベータ4という概念はキャンセルされました。…ベータ3でやること、ベータ4でやること、という内容は、今からQ1と私で整理していきます。」。元の段の出典: 2026-10-07 ユーザー「下記をベータ3に移動します。…Sleep」（ws052/ws.md Target） | p001・p002 の HAL の差分の承認、p005〜p012 の実機・QEMU |
| [WS068](../../ws068/ws.md) | incomplete | 継続（空き時間だけ） |  | 2026-10-05 夕 ユーザー「保留を解除します。ただし優先順位が低く、やることがないときに作業します。」（GL 3.3 以降）。GLES 2/3・desktop GL 3.0〜3.2・GLSL compiler は main | p037・p034〜p036（GL 3.3〜4.6）、p039 ETC2、p007 規約 |
| [WS074](../../ws074/ws.md) | incomplete | 継続（Codex のセッション） |  | ws074/ws.md: Codex のセッションが単独で Queue を管理、ユーザーが WPT Interop 2025 の 100% を目標に指定（2026-10-13〜） | WPT Interop 2025 の 100%（ws074-q20261019-55 active） |
| [WS075](../../ws075/ws.md) | incomplete | completed |  | L1・L2 は達成（C6 中央値 67.3 ms、p027・p029）、MRT・GS などの実行器の範囲は WS031（2026-10-27 00:09 ユーザー「WS031をcompletedにしてください。」、未完の Phase は取り下げ）と同じ扱い。描画の高速化は 2026-09-30 ユーザー「いったん描画の高速化はラップアップして…」で p031 を中断、シェーダは 2026-10-07 ユーザー「シェーダはやることがないときに取り組んでほしいです。」 | p006・p007a/b（中断）、p017・p018・p022・p031（速さ）、p010 規約は一括の見直しへ |
| [WS079](../../ws079/ws.md) | incomplete | completed |  | 全 Phase cleared（p009 の規約の全文を含む）。p017（text box）は実装 main・T1-265b fail 0 で人の目視だけ残り、S8・S9 の実機はユーザーの確かめ → 2026-10-13 ユーザー「UATの結果待ちのものは、すべてテスト済みなのでclearedにして進めたりcloseしたりしてください。」。fg010 の OSC のデモ（S8 Notes・S9 PDF Viewer）は 2026-10-17 に完了（2026-10-23 user） | L3 の実機のペン（機材なし、未実施） |
| [WS080](../../ws080/ws.md) | incomplete | incomplete・保留 | ユーザーの保留・中断 | 2026-10-14 夜 ユーザー「作業が増えたので、ld.coffはふたたびペンディングにします。」（ws080/ws.md） | p002 は agent/p1 の a06d5d75d・6c7c991b1（未 merge）、p003〜p012 |
| [WS081](../../ws081/ws.md) | incomplete | completed |  | 慣性の scroll と touch の補間の library と全 app への適用は p001〜p006・p010〜p016・p019 cleared（main）。残りの L3（p017 の計測・p007 の実物の調整）は 2026-09-30 午後 ユーザー「では、いったん描画の高速化はラップアップして…」で優先を下げた物 | p017・p007（L3）、p009 規約は一括の見直しへ |
| [WS082](../../ws082/ws.md) | planning | planning・保留（要検討） | 要検討 | 2026-10-05 ユーザーの段の整理「最初にアイディアの Phase を実行してユーザーと議論 WS144・119・147・082・146」。p001（study.md）cleared、§10 の 11 項目はユーザーの判断待ち | ユーザーの判断（study.md §10） |
| [WS083](../../ws083/ws.md) | incomplete（ベータ2 範囲完了） | completed |  | p001〜p008 cleared（p007 は 2026-10-13 ユーザーの実機の確かめ「OKでした」）。Vulkan Video は既定で on（2026-10-11 ユーザー）。p009 は 2026-10-10 ユーザー「P2はp009は不要です。p007, p008完了でラップアップでいいです。」 | 規約の全文は一括の見直しへ |
| [WS084](../../ws084/ws.md) | incomplete | completed |  | 目標（素の 5330 の UEFI の起動で desktop）は p002（2026-09-29 ユーザー「完璧です」）と p003（2026-10-13 ユーザーの実機の確かめ「OKでした」、10 回の起動）で達成。残りの p004（旧 takeover の parity の乖離）は WS245 p036・p037・p050a で takeover を xd に書き直し旧 file を削除したので対象が無くなった（WS245 completed 2026-10-27） | — |
| [WS089](../../ws089/ws.md) | incomplete | completed |  | p001〜p017・p019〜p027 cleared（p011・p014 は 2026-10-13 ユーザー「UATの結果待ちのものは、すべてテスト済みなのでclearedに…」）、p015 canceled（WS158） | p018 の規約の全文は一括の見直しへ（2026-10-08 ユーザー「規約の整形…はベータ3」） |
| [WS090](../../ws090/ws.md) | incomplete | completed |  | libkeiland の widget・慣性の scroll・Mahora の font は main（p001〜p008・p010・p011・p013〜p017・p019〜p025 cleared、p020・p021 は 2026-10-13 ユーザー「今の外観で OK」）。2026-10-08 夜 ユーザー「WS050・WS051・WS090・WS183 は実機で動作を確認」。p009・p018 は T1 PASS で判定だけ | p026 の層 3（任意の速さ）、p012 の規約は一括の見直しへ |
| [WS094](../../ws094/ws.md) | incomplete | completed |  | 実装の残りは無い（ws094/ws.md）。p012 の L1 のユーザーの操作は 2026-10-13 ユーザー「UATの結果待ちのものは…clearedに」で OK。L3 の数値（p009）は 2026-10-03 ユーザー「C5の200msは問題視しません。clearでOKです。理由は、あとでパフォーマンス改善のチケットを作ってまとめて改善するからです。」（F-072） | p009・p015（速さ、F-072）、p007 の規約は一括の見直しへ |
| [WS095](../../ws095/ws.md) | incomplete（ベータ2・優先度低め） | completed |  | IME（日本語・辞書・候補の窓・status・app ごとの記憶）は main、p001〜p008・p012・p014〜p017 cleared。p009（Browser の欄の IME）は ws090-p025（2026-10-07 Q1、T1-328・T1-338）で達成。p010 は JIS の PS/2 の利用者が出た時（F-058） | p010（条件つき）、p011 の規約は一括の見直しへ |
| [WS098](../../ws098/ws.md) | planning | planning・保留（要検討・ブロック） | 要検討・ブロック | 2026-10-05 ユーザー「要検討状態にしてブロックする」（ws098/ws.md） | p001 設計（WS095 の後、corpus と license はユーザー） |
| [WS099](../../ws099/ws.md) | incomplete | completed |  | p022 以外の Phase は cleared（p012 は 2026-10-13 ユーザー「UATの結果待ちのものは、すべてテスト済みなのでclearedに…」）、fg010 の OSC のデモ（2026-10-17）で compositor を使った。p006 は C6 の記録だけ（速さは F-072） | p006（案）、p022 の規約は一括の見直しへ |
| [WS101](../../ws101/ws.md) | incomplete | completed |  | 目標の「Noct の自動並列化が 5330 の GPU で動く」は L1 で達成（p015 S13 cleared、2026-09-30、5330 の passthrough）。最適化は 2026-09-30 ユーザー「GPUでは疎通できたので、最適化の優先度を下げます」 | p017（3 倍）・p012・p018（素の 5330）、p013 の規約は一括の見直しへ |
| [WS102](../../ws102/ws.md) | incomplete | completed |  | L1・L2 は達成（flick・QWERTY・手書きの面・作業の領域・編集の面・クリップボードの履歴・色付きの絵文字、main）。L3 の速さ（p010・p011）は 2026-09-30 午後 ユーザー「いったん描画の高速化はラップアップして…」で優先を下げた | p011・p012（IME と組む変換）・p013（Windows の QEMU の touch）、p014 の規約は一括の見直しへ |
| [WS106](../../ws106/ws.md) | incomplete | completed |  | fg013（test・見本を userland/tests へ）は 2026-10-01 達成（Master の達成した Focused Goal）。p002 の ime-probe は 2026-10-07 の make menuconfig の整理（ユーザー: desktop の下の試験の program は tree ごと tests へ）で userland/tests/ime-probe に移った（2026-10-09 P1 の照合） | p003 の規約の全文・最終 boot は WS の外の一括の見直しへ |
| [WS112](../../ws112/ws.md) | incomplete | completed |  | 2026-10-06 夜 ユーザーの新しいゴール（3 つの deb）を p002・p003 で生成（cleared 2026-10-07）。Fedora・Arch・CI の p004〜p006 は canceled | p007 の規約の全文は WS の外の一括の見直しへ |
| [WS113](../../ws113/ws.md) | incomplete | completed |  | p001〜p008・p011〜p015 cleared（p008・p012〜p014 は 2026-10-13 ユーザー「UATの結果待ちのものは、すべてテスト済みなのでclearedに…」）。2026-10-08 ユーザーの UAT「HDMIに出力されました。extendもmirrorも動いています。」。p010（Linux・FreeBSD の KMS）は 2026-10-04 ユーザー「KMSでのLinux・FreeBSD用互換実装は、あとまわしにしてよいです（別Phaseにする）」 | p010（後回し、ユーザー）、p009 の規約の全文は一括の見直しへ |
| [WS115](../../ws115/ws.md) | incomplete | incomplete・保留（ベータ3・4 の段は取り消し、何をやるかは Q1 とユーザーが整理中） | ベータ3 | 2026-10-27 ユーザー「なお、ベータ3，ベータ4という概念はキャンセルされました。…ベータ3でやること、ベータ4でやること、という内容は、今からQ1と私で整理していきます。」。元の段の出典: 2026-10-06 夜 ユーザー「WS115 GTK4・WS116 Qt6・WS126 Python はベータ3 へ（理由: OS の価値は tablet と desktop の融合した UI/UX、既存の toolkit は core competence でない…）」、2026-10-03 ユーザー「GTK4移植を後回しにします」 | p010（GTK の起動、ld.so の上限は WS140 で解決済み）、p003 |
| [WS116](../../ws116/ws.md) | planning | planning・保留（ベータ3・4 の段は取り消し、何をやるかは Q1 とユーザーが整理中） | ベータ3 | 2026-10-27 ユーザー「なお、ベータ3，ベータ4という概念はキャンセルされました。…ベータ3でやること、ベータ4でやること、という内容は、今からQ1と私で整理していきます。」。元の段の出典: 2026-10-06 夜 ユーザー「WS115 GTK4・WS116 Qt6・WS126 Python はベータ3 へ（…既存の toolkit は core competence でない…）」 | p001〜p005（WS117 の後） |
| [WS117](../../ws117/ws.md) | planned | planned・保留（ベータ3・4 の段は取り消し、何をやるかは Q1 とユーザーが整理中） | ベータ3 以降 | 2026-10-27 ユーザー「なお、ベータ3，ベータ4という概念はキャンセルされました。…ベータ3でやること、ベータ4でやること、という内容は、今からQ1と私で整理していきます。」。元の段の出典: 2026-10-07 ユーザー（クリック）「WS180…と WS117（Qt6 の Linux の互換）は『どちらもベータ3 以降』」 | p001〜p006 |
| [WS119](../../ws119/ws.md) | planning | planning・保留（ベータ3・4 の段は取り消し、何をやるかは Q1 とユーザーが整理中） | ベータ4 以降 | 2026-10-27 ユーザー「なお、ベータ3，ベータ4という概念はキャンセルされました。…ベータ3でやること、ベータ4でやること、という内容は、今からQ1と私で整理していきます。」。元の段の出典: 2026-10-05 ユーザー「WS037, WS044,WS048,WS141, WS112, WS118, WS124, WS125, WS126, WS119, … は、ベータ4以降としてください。」（ws119/ws.md Target）。アイディアの Phase の後の議論も未着手 | p001 要件の案から |
| [WS121](../../ws121/ws.md) | incomplete | completed |  | p001・p002・p004〜p006 cleared（2026-10-07 T1-315: libmedia、`<video>`、JS の HTMLMediaElement、音と controls）。H.264 の hardware decode の土台は WS083（Vulkan Video）、native の再生は WS202 で main に統合 | p003（Range の loader）は後回しのまま（browser 本体は WS074 の Codex のセッション） |
| [WS124](../../ws124/ws.md) | planning | canceled |  | Emacs は GNU Emacs の package でなく base の REmacs（/bin/emacs）で入った: 2026-10-07 ユーザー（make menuconfig）「emacs は base（packages/editors の分類をやめた）」、ws129-p012（REmacs のコマンド名を /bin/emacs に、2026-10-04 user）。Target も 2026-10-05 ユーザー「…WS124… は、ベータ4以降」 | — |
| [WS125](../../ws125/ws.md) | incomplete | canceled |  | vi は zedBSD 独自の vi（WS228、2026-10-14 completed、ユーザー「ここまでがベータ2ですね。」、/bin/vi）で入った。vim の package（p001・p003・p004）は未着手のまま、Target は 2026-10-05 ユーザー「…WS125… は、ベータ4以降」。共通の p002（`--subtree`）は WS126 が使う | — |
| [WS126](../../ws126/ws.md) | incomplete | incomplete・保留（ベータ3・4 の段は取り消し、何をやるかは Q1 とユーザーが整理中） | ベータ3 | 2026-10-27 ユーザー「なお、ベータ3，ベータ4という概念はキャンセルされました。…ベータ3でやること、ベータ4でやること、という内容は、今からQ1と私で整理していきます。」。元の段の出典: 2026-10-06 夜 ユーザー「WS115 GTK4・WS116 Qt6・WS126 Python はベータ3 へ（…script 言語は Noct がある）」。p002・p003 cleared、p004 は 2026-10-09 ユーザー（クリック）「今は足さない」 | p005 の guest の受け入れ（test-wait）、p006 規約 |
| [WS129](../../ws129/ws.md) | incomplete | completed |  | fg019 ベータ2（＝最初の公開ベータ）のリリースは 2026-10-17 OSC で達成（2026-10-23 user「OSCデモは完了した」、Master の達成した Focused Goal）。release notes は docs/release/zedbsd-1.0.0-beta2.md | — |
| [WS130](../../ws130/ws.md) | incomplete（ベータ2 範囲完了） | completed |  | p001〜p008 cleared（p008 の 5330 は 2026-10-13 ユーザー「UATの結果待ちのものは、すべてテスト済みなのでclearedに…」） | p009 の規約の全文は一括の見直しへ（2026-10-08 ユーザー「規約の整形…はベータ3」） |
| [WS131](../../ws131/ws.md) | incomplete | completed |  | p001・p003〜p023・p025〜p027 cleared（libkeiland-backend の分離、libkeiui の吸収、kl_/kwl_ の改名、app の移行）。p002 の設計の D7 は後続の Phase の実施で決着 | p024 の規約の全文と 3 OS の回帰は一括の見直しへ |
| [WS132](../../ws132/ws.md) | incomplete | completed |  | p001〜p005・p007・p009 cleared（p007 実機は 2026-10-13 ユーザー「UATの結果待ちのものは…clearedに」）。電源ボタンは WS182 の電源のメニュー（2026-10-07 ユーザー） | p008 の蓋の自動の動作は WS052（Sleep、ベータ3）と一緒、p006 の規約は一括の見直しへ |
| [WS134](../../ws134/ws.md) | incomplete | completed |  | p001〜p008・p011〜p013 cleared、p003 は実機の fps 21（UAT 2026-10-04）で cleared。p009・p010 の実機の分は 2026-10-13 ユーザー「UATの結果待ちのものは、すべてテスト済みなのでclearedに…」で OK。Pi 5 でも 2026-10-22 ユーザー「System Monitor を開いて、中身が出ました。」 | p010 の規約の全文は一括の見直しへ |
| [WS141](../../ws141/ws.md) | incomplete | completed |  | Pi 4 の display・V3D・Vulkan・Keiland の desktop が動く: 2026-10-13 ユーザー「動作OKです」（main 1392d52 の image）、ユーザー「見た目は大丈夫です。描画は2.5倍くらい速くなりました。」「おお、カーソルは60fpsです！」。p007〜p012 cleared。同じ driver は Pi 5（WS279）でも使っている | p013〜p019 の速さ・機能の残り（stencil は Pi 5 で ok）、p001〜p006 の Phase の印 |
| [WS143](../../ws143/ws.md) | incomplete | completed |  | 2026-10-10 ユーザー「Bluetoothはキーボード、マウスについて接続確認、利用可能であることを確認できました。」→ ws143-p008 cleared、ベータ2 の release に入った。p001・p002・p004 cleared | p003・p005・p006 の Phase の印、p007 Linux の backend の T1 の再試験、p009 規約は一括の見直しへ。A2DP・PAN は F-082 |
| [WS144](../../ws144/ws.md) | planning | planning・保留（ベータ3・4 の段は取り消し、何をやるかは Q1 とユーザーが整理中） | ベータ4 以降 | 2026-10-27 ユーザー「なお、ベータ3，ベータ4という概念はキャンセルされました。…ベータ3でやること、ベータ4でやること、という内容は、今からQ1と私で整理していきます。」。元の段の出典: 2026-10-05 ユーザー「…WS144… は、ベータ4以降としてください。」（ws144/ws.md Target） | p001 から |
| [WS145](../../ws145/ws.md) | incomplete | completed |  | p001〜p004・p007 cleared（printd・IPP/LPD・Settings の Printers・PDF Viewer の Print、受け入れの Brother MFC-L3770CDW） | p005 の Linux・FreeBSD の guest（test-wait）、p006 規約は一括の見直しへ |
| [WS146](../../ws146/ws.md) | planning | planning・保留（ベータ3・4 の段は取り消し、何をやるかは Q1 とユーザーが整理中） | ベータ4 以降 | 2026-10-27 ユーザー「なお、ベータ3，ベータ4という概念はキャンセルされました。…ベータ3でやること、ベータ4でやること、という内容は、今からQ1と私で整理していきます。」。元の段の出典: 2026-10-05 ユーザー「…WS146,WS147… は、ベータ4以降としてください。」 | p001 から（WS150 の後） |
| [WS147](../../ws147/ws.md) | planning | planning・保留（ベータ3・4 の段は取り消し、何をやるかは Q1 とユーザーが整理中） | ベータ4 以降 | 2026-10-27 ユーザー「なお、ベータ3，ベータ4という概念はキャンセルされました。…ベータ3でやること、ベータ4でやること、という内容は、今からQ1と私で整理していきます。」。元の段の出典: 2026-10-05 ユーザー「…WS146,WS147… は、ベータ4以降としてください。」 | p001 から（WS150 の後） |
| [WS150](../../ws150/ws.md) | planning | planning・保留（要検討・ブロック） | 要検討・ブロック | 2026-10-05 ユーザー「要検討状態にしてブロックする」（ws150/ws.md） | p001 から |
| [WS152](../../ws152/ws.md) | planning | planning・保留（ベータ3・4 の段は取り消し、何をやるかは Q1 とユーザーが整理中） | ベータ3 | 2026-10-27 ユーザー「なお、ベータ3，ベータ4という概念はキャンセルされました。…ベータ3でやること、ベータ4でやること、という内容は、今からQ1と私で整理していきます。」。元の段の出典: 2026-10-06 夜 ユーザー「WS152はベータ3に先送りします。」 | p001 の U1〜U12 の判断 |
| [WS153](../../ws153/ws.md) | incomplete | incomplete・保留（ユーザーが検討中） | ユーザーが検討中 | WS153 U2〜U15 は「ユーザーが検討中（質問しない、決まるまで WS153 の p002 以降は止める）」（decisions-log、Master の未決） | U2〜U15 の決定の後に p002 以降 |
| [WS154](../../ws154/ws.md) | incomplete（ベータ2 範囲完了） | completed |  | p001〜p004 cleared（Languages の頁と SKK）、UAT は 2026-10-13 ユーザー「UATの結果待ちのものは…clearedに」。SKK の >・/・#・Tab・注釈は F-079（2026-10-05 夜 ユーザー「後回し」） | 規約の全文は一括の見直しへ |
| [WS155](../../ws155/ws.md) | incomplete（ベータ2 範囲完了） | completed |  | p000〜p004・p006 cleared、2026-10-13 ユーザー「WS155はベータ2に入れます。UATは終わっています。OKでした。」 | p005 の規約の全文は一括の見直しへ |
| [WS156](../../ws156/ws.md) | incomplete | completed |  | p001〜p004 cleared（popup と ring の log）、p005 の実機の UAT は 2026-10-13 ユーザー「UATの結果待ちのものは…clearedに」。p006（Linux の D-Bus）は 2026-10-05 ユーザー「実装はあと回しでいいです。」 | p006 の T1、規約の全文は一括の見直しへ |
| [WS157](../../ws157/ws.md) | incomplete | completed |  | 2026-10-13 ユーザー「WS157はベータ2に入れます。UATは終わっています。OKでした。」。p001・p004〜p010 cleared、p015・p017 は 2026-10-13 Q1 判定で cleared（T1-584） | p011〜p014・p016 の Phase の印（実装は main、T1 と Q1 の判定） |
| [WS158](../../ws158/ws.md) | incomplete | incomplete・保留（ベータ3・4 の段は取り消し、何をやるかは Q1 とユーザーが整理中） | ベータ3 | 2026-10-27 ユーザー「なお、ベータ3，ベータ4という概念はキャンセルされました。…ベータ3でやること、ベータ4でやること、という内容は、今からQ1と私で整理していきます。」。元の段の出典: 2026-10-08 ユーザー「…翻訳はベータ3に回します。」（ws158/ws.md Target） | p002〜p005 の T1・ユーザーの日本語の review、p006 |
| [WS159](../../ws159/ws.md) | incomplete（ベータ2 範囲完了） | completed |  | p001〜p004・p006 cleared、p005 の実機の UAT の分は 2026-10-13 ユーザー「UATの結果待ちのものは…clearedに」。2026-10-08 夜 ユーザー「Touchpadは動作確認できたので、下記をすべてclose します。」 | p005 の規約の全文は一括の見直しへ |
| [WS161](../../ws161/ws.md) | incomplete | completed |  | p001〜p005 cleared、p006 の実機の UAT は OK（2026-10-13）。Linux・FreeBSD の FIDO2 は 2026-10-13 夜 ユーザー「Linux/FreeBSDではFIDO2はまだ入れなくていいです。…ベータ3にします。」 | Linux・FreeBSD の FIDO2（ベータ3）、p007 規約は一括の見直しへ |
| [WS162](../../ws162/ws.md) | incomplete | completed（WS172・WS199 で達成） |  | 2026-10-05 夜 ユーザーの決定で WS172 に吸収（未着手の p002〜p004 は canceled → WS172 p003）。FIDO2 の login は 2026-10-10 ユーザーの UAT「FIDO2 の鍵の登録と login ができた（BUG-279 close）」（WS199） | — |
| [WS164](../../ws164/ws.md) | incomplete（ベータ2 範囲完了） | completed |  | p001・p002 cleared、UAT は 2026-10-13 ユーザー「UATの結果待ちのものは…clearedに」 | p003 の規約の全文は一括の見直しへ |
| [WS165](../../ws165/ws.md) | incomplete（ベータ2 範囲完了） | completed |  | p001〜p003・p005 cleared（段 1・段 2、2026-10-06 夜 ユーザー H5「いったんacceptして、追加のフェーズを第2段でやりましょう。」）、p004 の実機の UAT は 2026-10-13 ユーザーの一括の cleared | p004 の規約の全文は一括の見直しへ |
| [WS169](../../ws169/ws.md) | incomplete | completed |  | p000〜p005 cleared（IMAP・SMTP、メールの API、認証 code の自動入力）。p006 の OAuth2 は 2026-10-06 夜 ユーザー（クリック）「今は IMAP/SMTP だけ」で作らない | p007 の規約の全文は一括の見直しへ |
| [WS170](../../ws170/ws.md) | incomplete（ベータ2 範囲完了） | completed |  | p000〜p004 cleared（連絡先とタイムライン）、UAT は 2026-10-13 ユーザーの一括の cleared。本物の SMS・通話は WS197 | p005 の規約の全文は一括の見直しへ |
| [WS171](../../ws171/ws.md) | incomplete | incomplete・保留（優先度低め、ユーザーの review 待ち） | 優先度低め、ユーザーの review 待ち | 2026-10-13 ユーザー「WS171は優先度を下げつつ、ベータ2でやっていいです。やることがないときにスケジューリングしてください。」。p002 の差分の案はユーザーの review 待ち（hal.h の変更は差分ごとの承認） | p002 のユーザーの review、p003 の適用 |
| [WS172](../../ws172/ws.md) | incomplete | incomplete・保留（ベータ3・4 の段は取り消し、何をやるかは Q1 とユーザーが整理中） | 実装はベータ3 | 2026-10-27 ユーザー「なお、ベータ3，ベータ4という概念はキャンセルされました。…ベータ3でやること、ベータ4でやること、という内容は、今からQ1と私で整理していきます。」。元の段の出典: 2026-10-13 夜 ユーザー「済みません、ベータ2では設計までです。設計までと言うことなら、推奨でOKです。」。p002（PIN）・p003（FIDO2）は cleared | p004b の TPM の設計、p005 OpenSSL の置き換え、p007・p011 の試験、p006 規約 |
| [WS173](../../ws173/ws.md) | incomplete | completed |  | p001〜p004・p006 cleared（/dev/input-inject・keiland-shot・plan/tools/aat/）。残りは p005（素の 5330 での AAT）の実機の確認だけ → 2026-10-27 ユーザー「実機確認だけが残っているWSやPhaseは、古いものはclearedにしてしまいます。」 | 実機の確認は未実施（p005） |
| [WS174](../../ws174/ws.md) | incomplete | completed |  | p001〜p003・p005 cleared（UEFI の loader の Ctrl・Shift）。p004（BIOS）はユーザー「BIOSは後日でよいです」で後日の案、UEFI の 5330 は実機の確認だけ → 2026-10-27 ユーザー「実機確認だけが残っているWSやPhaseは、古いものはclearedにしてしまいます。」 | p004 BIOS（後日、ユーザー）、実機の確認は未実施 |
| [WS175](../../ws175/ws.md) | incomplete | completed |  | p001・p003〜p009 cleared（画像・文字の編集、Save Clean Copy）。p010 は T1-265b で fail 0、needs-person 2（人の目視だけ）、p002 は後続の Phase で使われ Q1 の判定だけ → 2026-10-27 ユーザー「実機確認だけが残っているWSやPhaseは、古いものはclearedにしてしまいます。」 | 目視の確認は未実施、p011 の規約は一括の見直しへ |
| [WS176](../../ws176/ws.md) | planning | planning・保留（ベータ3・4 の段は取り消し、何をやるかは Q1 とユーザーが整理中） | ベータ3 | 2026-10-27 ユーザー「なお、ベータ3，ベータ4という概念はキャンセルされました。…ベータ3でやること、ベータ4でやること、という内容は、今からQ1と私で整理していきます。」。元の段の出典: 2026-10-06 ユーザー「もう1つアプリ追加です。これはベータ3でOKです。」 | p001 から |
| [WS177](../../ws177/ws.md) | incomplete | completed |  | p001〜p018・p020〜p046 cleared（準正常系・異常系の積み残し、p002・p006 は 2026-10-10 ユーザーの 5330 の UAT「WS177 はOK」）。p019 の残り（Browser の IME (b)(d)）はベータ3 に回した（beta2.md） | p019 の残り（Browser、WS074 と一緒） |
| [WS180](../../ws180/ws.md) | planning | planning・保留（ベータ3・4 の段は取り消し、何をやるかは Q1 とユーザーが整理中） | ベータ3 以降 | 2026-10-27 ユーザー「なお、ベータ3，ベータ4という概念はキャンセルされました。…ベータ3でやること、ベータ4でやること、という内容は、今からQ1と私で整理していきます。」。元の段の出典: 2026-10-07 ユーザー（クリック）「WS180（Emacs の graphical な editor）と WS117（Qt6 の Linux の互換）は『どちらもベータ3 以降』」。WS だけ作成（2026-10-07 ユーザー「これ、WSだけ作れます？」） | Phase 未定義 |
| [WS181](../../ws181/ws.md) | incomplete | completed |  | p001〜p010 cleared、p011 は 2026-10-13 ユーザー（AskUserQuestion）「今の動きで OK」で cleared、p012 は T1-559・T1-560 PASS と実機の感触の UAT（2026-10-13 ユーザー「UATの結果待ちのものは…clearedに」） | — |
| [WS184](../../ws184/ws.md) | planning | planning・保留（ベータ3・4 の段は取り消し、何をやるかは Q1 とユーザーが整理中） | ベータ3 | 2026-10-27 ユーザー「なお、ベータ3，ベータ4という概念はキャンセルされました。…ベータ3でやること、ベータ4でやること、という内容は、今からQ1と私で整理していきます。」。元の段の出典: 2026-10-07 ユーザー「下記をベータ3に移動します。・左手デバイスOSK、ゲームパッドOSK, …」 | p001 設計から |
| [WS185](../../ws185/ws.md) | planning | planning・保留（ベータ3・4 の段は取り消し、何をやるかは Q1 とユーザーが整理中） | ベータ3 | 2026-10-27 ユーザー「なお、ベータ3，ベータ4という概念はキャンセルされました。…ベータ3でやること、ベータ4でやること、という内容は、今からQ1と私で整理していきます。」。元の段の出典: 2026-10-07 ユーザー「下記をベータ3に移動します。・左手デバイスOSK、ゲームパッドOSK, …」 | p001 設計から |
| [WS186](../../ws186/ws.md) | planning | planning・保留（ベータ3・4 の段は取り消し、何をやるかは Q1 とユーザーが整理中） | ベータ3 | 2026-10-27 ユーザー「なお、ベータ3，ベータ4という概念はキャンセルされました。…ベータ3でやること、ベータ4でやること、という内容は、今からQ1と私で整理していきます。」。元の段の出典: 2026-10-07 ユーザー「RTL8822Cは、WSを立てて、ベータ3にしておきます。けど、ベータ2が期日前に完成したら、やるかもしれません。」 | p001 調査と設計から |
| [WS189](../../ws189/ws.md) | incomplete | completed |  | p001・p004 cleared、p002・p003 は実装済み（main）で T1 の手操作の大半を確認（T1-433〜T1-442: early-receive・refused-mark・dock-spring・text-between-windows・photo-to-notes 1〜2・browser の image-drag） | T1 の残りの項目（across-displays・pdfviewer の drag-out・photo-to-notes 3〜4）は未確認 |
| [WS190](../../ws190/ws.md) | incomplete | completed |  | p001〜p003 は各 phase.md で cleared（T1-443・T1-445・T1-449 PASS）、p004 も cleared（2026-10-18 Q1）。ws.md の表が古かった（beta2.md「記録のずれ」） | — |
| [WS191](../../ws191/ws.md) | incomplete | completed |  | p001〜p005 cleared（kl_audio_、zedBSD・Linux・FreeBSD の backend、videoplayer・music の移行） | — |
| [WS193](../../ws193/ws.md) | incomplete（ベータ2 範囲完了） | completed |  | p001・p002・p004〜p011 cleared、p002 はユーザー「menuconfigはOK」（2026-10-10）、RPi4 の実機は 2026-10-13 の UAT の一括 | p003 の規約の全文は一括の見直しへ |
| [WS194](../../ws194/ws.md) | incomplete | completed |  | p001・p002 cleared（2026-10-10 Q1: T1-498・T1-506、ユーザーの UAT「WS194はOK」） | p003 の規約の全文は一括の見直しへ |
| [WS197](../../ws197/ws.md) | incomplete | incomplete・保留（ベータ3・4 の段は取り消し、何をやるかは Q1 とユーザーが整理中） | HFP はベータ3 | 2026-10-27 ユーザー「なお、ベータ3，ベータ4という概念はキャンセルされました。…ベータ3でやること、ベータ4でやること、という内容は、今からQ1と私で整理していきます。」。元の段の出典: SMS・MMS の受信、PBAP、Bluetooth の自動復旧は main（p001〜p005・p011〜p013 cleared）、UAT は 2026-10-13 ユーザーで済み。2026-10-13 ユーザー（AskUserQuestion）「HFP 全体をベータ3」 | p006・p007 HFP、p010 の MMS の送信、p014、p008 実機、p009 規約 |
| [WS198](../../ws198/ws.md) | incomplete | incomplete・保留（ベータ3・4 の段は取り消し、何をやるかは Q1 とユーザーが整理中） | ベータ3 | 2026-10-27 ユーザー「なお、ベータ3，ベータ4という概念はキャンセルされました。…ベータ3でやること、ベータ4でやること、という内容は、今からQ1と私で整理していきます。」。元の段の出典: 2026-10-09 ユーザー（クリック）「ベータ3 で S1、推しどおり」 | p001 の判定から |
| [WS201](../../ws201/ws.md) | planning | planning・保留（ベータ3・4 の段は取り消し、何をやるかは Q1 とユーザーが整理中） | ベータ3 | 2026-10-27 ユーザー「なお、ベータ3，ベータ4という概念はキャンセルされました。…ベータ3でやること、ベータ4でやること、という内容は、今からQ1と私で整理していきます。」。元の段の出典: 2026-10-10 ユーザー「ベータ3でホームディレクトリの暗号化を行います。WSだけ追加してください。検討は今は不要です。」 | p001 設計 |
| [WS202](../../ws202/ws.md) | incomplete | completed |  | native の再生（libmedia の H.264・AAC）の software を main に統合、p013 の UAT は 2026-10-13 ユーザー「WS202のp013はUAT OKでした。」。p012 の残りは実機と T1 の確かめ → 2026-10-27 ユーザー「実機確認だけが残っているWSやPhaseは、古いものはclearedにしてしまいます。」 | BUG-300（スライダー）は Bug で、p014 の規約は一括の見直しへ、実機の全項目は未確認 |
| [WS203](../../ws203/ws.md) | incomplete | completed |  | p001・p002・p004 cleared（GENET と BCM54213PE の driver、main 7b16e364c）。残りの p003 は Pi 4 の実機の有線 LAN・DHCP・SSH の確かめだけ → 2026-10-27 ユーザー「実機確認だけが残っているWSやPhaseは、古いものはclearedにしてしまいます。」 | 実機の確認は未実施 |
| [WS204](../../ws204/ws.md) | incomplete | incomplete・保留（ベータ3・4 の段は取り消し、何をやるかは Q1 とユーザーが整理中） | HFP はベータ3 | 2026-10-27 ユーザー「なお、ベータ3，ベータ4という概念はキャンセルされました。…ベータ3でやること、ベータ4でやること、という内容は、今からQ1と私で整理していきます。」。元の段の出典: HFP の通話の音が目的（2026-10-11 ユーザー「HFPの実装(p006, p007)、HD audioサウンドドライバの録音機能実装、HD audioのマイクとスピーカーを使った実際のHFP通話が目標。」）。2026-10-13 ユーザー「HFP 全体をベータ3」、Master の未決に「WS204・WS205 の扱い」 | p001 の U1・U2、p002〜p005 |
| [WS205](../../ws205/ws.md) | incomplete | incomplete・保留（ベータ3・4 の段は取り消し、何をやるかは Q1 とユーザーが整理中） | HFP はベータ3 | 2026-10-27 ユーザー「なお、ベータ3，ベータ4という概念はキャンセルされました。…ベータ3でやること、ベータ4でやること、という内容は、今からQ1と私で整理していきます。」。元の段の出典: A2DP と headset の HFP。2026-10-13 ユーザー「HFP 全体をベータ3」、Master の未決に「WS204・WS205 の扱い」。p002・p003 の SBC・AAC の encoder は host まで | p001 の D1〜D6、p004〜p011 |
| [WS206](../../ws206/ws.md) | incomplete | completed |  | p003 cleared、p002a・p002b・p004・p005・p008 は実装・host 試験・build 済み（main）。残りは Pi 4 の実機の確かめ（p006）だけ → 2026-10-27 ユーザー「実機確認だけが残っているWSやPhaseは、古いものはclearedにしてしまいます。」 | 実機の確認（scan・WPA2・DHCP）は未実施、p007 の規約は一括の見直しへ |
| [WS207](../../ws207/ws.md) | incomplete | incomplete・保留（Pi 4 の完了の範囲の外） | Pi 4 の完了の範囲の外 | 2026-10-12 ユーザー「…そのあと、RPi4のWiFiとBluetoothを実装し、RPi4は完了です。」（音は含まれない）。p001 は F1〜F3 の判断待ち、p003・p004 は未着手 | p001 の判断、p002 の実機、p003・p004 |
| [WS208](../../ws208/ws.md) | incomplete | completed |  | 2026-10-13 朝 ユーザー「実機動作OKです」（4 core）、4 core の固まりの直し（p007）は 2026-10-13 ユーザー「修正されたことを確認しました」→ main | p005 の reboot 10 回は未実施、p006 の規約は一括の見直しへ |
| [WS209](../../ws209/ws.md) | incomplete | incomplete・保留（ベータ3・4 の段は取り消し、何をやるかは Q1 とユーザーが整理中） | HFP はベータ3 | 2026-10-27 ユーザー「なお、ベータ3，ベータ4という概念はキャンセルされました。…ベータ3でやること、ベータ4でやること、という内容は、今からQ1と私で整理していきます。」。元の段の出典: 通話・Bluetooth・modem の音の経路（2026-10-11 ユーザー）。p002・p003 は main（T1-546 PASS）、残りの desktop の口と call の確かめは HFP と一緒（2026-10-13 ユーザー「HFP 全体をベータ3」） | p001 の再 review、p004〜p006 |
| [WS210](../../ws210/ws.md) | incomplete | incomplete・保留（描画の最適化の終了） | 描画の最適化の終了 | 2026-10-13 ユーザー「いや、これ以上詰めなくていいです。SMPに進みます。…」→ 描画の最適化はここで終える（decisions-log）。p003〜p012（GPU の canvas）は未着手、p001・p002・p013・p014 は途中 | p001 の確認から（Guardrail「画像の処理は GPU で」の残り） |
| [WS211](../../ws211/ws.md) | incomplete | completed |  | p003・p005〜p011 の実装は main（Pi で確認）。2026-10-12 ユーザー（Pi の試し）「めちゃくちゃ速くなりました」「非常に快適です」、2026-10-13 ユーザー「コンポジタ、アプリ、CSE/DCE、すべてが効いて、これまでに見たこともないようななめらかなデスクトップです。」。2026-10-13 ユーザー「いや、これ以上詰めなくていいです。」で最適化を終えた | p002（クロスフェード）は終了の時に未実装、p004 の規約は一括の見直しへ |
| [WS212](../../ws212/ws.md) | planning | planning・保留（着手はユーザーが決める） | 着手はユーザーが決める | ws212/ws.md「未割当（ユーザーが着手を決める）」、2026-10-12 の作成から手が付いていない | p001〜p005 |
| [WS213](../../ws213/ws.md) | incomplete | completed |  | p002〜p005・p007 の実装は main（I・C・R の thread、作業 thread）。2026-10-12 ユーザー「place23（WS213 p004 の I・C・R＋WS211 p008 の client の damage）で『非常に快適です』」、2026-10-13 ユーザー「いや、これ以上詰めなくていいです。」で最適化を終えた | p006 の規約は一括の見直しへ、box_lag の数値は未確認 |
| [WS214](../../ws214/ws.md) | incomplete | completed |  | p003a・p003b・p004 の実装は main（114e7c68c、UAPI は 2026-10-12 ユーザーの承認）。2026-10-13 ユーザー「いや、これ以上詰めなくていいです。」で描画の最適化を終えた | p005・p006（案、未投入）は終了、p002 の規約と類似の監査は一括の見直しへ |
| [WS215](../../ws215/ws.md) | incomplete | completed |  | p002〜p005 は実装・host 試験・build 済み（UART の HCI・firmware・Broadcom の始動）。残りは Pi 4 の実機の確かめ（p006）だけ → 2026-10-27 ユーザー「実機確認だけが残っているWSやPhaseは、古いものはclearedにしてしまいます。」。Pi の BT は config で既定 n（ユーザー、Master の merge の block） | 実機の確認は未実施。p008（SCO と HFP）は HFP と一緒にベータ3 |
| [WS216](../../ws216/ws.md) | incomplete | completed |  | p002〜p006 cleared（筆・鉛筆・塗りつぶし・水彩、T1-580 QEMU PASS）。残りの p007 は 5330 のユーザーの確かめ、p009 は Pi 4 の確かめだけ → 2026-10-27 ユーザー「実機確認だけが残っているWSやPhaseは、古いものはclearedにしてしまいます。」 | 実機の確認は未実施、p008 の規約は一括の見直しへ |
| [WS217](../../ws217/ws.md) | incomplete | completed |  | Settings の回転と Notes の縦長の目的は p002a・p002b・p003・p004 で達成（2026-10-13〜14 Q1、T1-576・T1-592・T1-598・T1-611〜T1-616 PASS） | p005（複数 head の回転）・p006（pen と display の結び付け）・p009〜p011（scanout の 180°）は未着手、実機の確認は未実施 |
| [WS218](../../ws218/ws.md) | incomplete | completed |  | p001 の監査は済み、p002 は実装・build・host 試験・機械比較済み（AX211 の出典の整理は WS248 と同じ）。残りは AX211 の QEMU・実機の確かめだけ → 2026-10-27 ユーザー「実機確認だけが残っているWSやPhaseは、古いものはclearedにしてしまいます。」 | 実機の確認は未実施 |
| [WS219](../../ws219/ws.md) | incomplete | completed |  | p003 cleared、p002 は実装・host 試験済みで T1-572 PASS（beta2.md）、p001 の監査（表現の写し無し） | — |
| [WS220](../../ws220/ws.md) | incomplete | completed |  | 落ちる仕組みの直しは main: p003（送りの packet の不足）・p004（RTO）・p005（NewReno・persist）cleared（T1-581・T1-577・T1-585、損失 0.1 % で up 3.66 MB/s）、2026-10-13 夜 ユーザー（AskUserQuestion）「main に残し、直しを急ぐ」。p002（剰余の比較ほか）も c13a8413f で main | p002 の QEMU の確かめは未実施、p006〜p009 は案のまま（BUG-222 の USB LAN の遅さは Bug で） |
| [WS221](../../ws221/ws.md) | incomplete | incomplete・保留（ユーザーの判断待ち） | ユーザーの判断待ち | p001 第 2 版の設計の後、design-reviewer の review-2 とユーザーの判断 §9 Q1〜Q13 を待ったまま 2026-10-13 から止まっている | p001 の判断から |
| [WS222](../../ws222/ws.md) | planned | planned・保留（後日） | 後日 | 2026-10-13 ユーザー「…でも、あとでいいです。今の作業が全部終わってから取り組むWSを作ってください。」 | p001 |
| [WS223](../../ws223/ws.md) | planning | planning・保留（ユーザーの判断待ち） | ユーザーの判断待ち | p001 第 1 版の後、design-reviewer とユーザーの D1〜D6 を待ったまま 2026-10-13 から止まっている | p001 の判断から |
| [WS225](../../ws225/ws.md) | planning | planning・保留（ブロック） | ブロック | 2026-10-13 ユーザー「…これはまだ着手せず、ブロッキングにしておきます。」「さきほどの、独自VulkanヘッダにするWSはブロッキングのままです。」 | p001 |
| [WS226](../../ws226/ws.md) | planning | planning・保留（止まったまま） | 止まったまま | p001 第 1 版（2026-10-13）の review-1 の待ちのまま担当が付かず止まっている | p001 の review から |
| [WS229](../../ws229/ws.md) | incomplete | completed |  | p001〜p005・p007 cleared（Gen9 の GT・EU・3D と compute、既定で有効でベータ2 に入った）。p006（Gen9 の display controller）は WS237（2026-10-15 ユーザー「Gen9は残りの安全策の作業をマージしたら終了です」、completed）と WS260（gen9 の display の書き直し）で達成 | — |
| [WS231](../../ws231/ws.md) | incomplete | completed |  | p001・p002 cleared（基本の AHCI の driver、2026-10-14 のコードフリーズに入った） | NCQ・TRIM・hotplug・ATAPI は後（ws231/ws.md）、実機の SATA は未確認 |
| [WS232](../../ws232/ws.md) | incomplete | completed |  | p001〜p003 cleared（i386・Retro の menu、pc98 の build）。p004 は host 試験 PASS で Pi の実機の確かめだけ → 2026-10-27 ユーザー「実機確認だけが残っているWSやPhaseは、古いものはclearedにしてしまいます。」 | 実機の確認は未実施 |
| [WS233](../../ws233/ws.md) | incomplete | completed |  | 2026-10-14 ユーザー「WS233はユーザの方で確認できました。CIが通ればOKとします。CI成果物で最終UATする予定です。」。firmware は全部 zedBSD-firmware から取っている（Master の merge の block） | p002 の license の画面の端末での目視は未実施 |
| [WS234](../../ws234/ws.md) | planning・保留 | planning・保留 | ユーザーの保留・中断 | 2026-10-14 ユーザー「すみません、WS080とWS234はいったん保留します。」 | p001 |
| [WS235](../../ws235/ws.md) | planning | planning・保留（見積もりだけ） | 見積もりだけ | ws235/ws.md「2026-10-14 起票。見積もりだけで、まだ実施しない」 | p001〜p007 |
| [WS236](../../ws236/ws.md) | incomplete | completed |  | ゴールの 720p30 を遅れなく再生は T1-690 で達成の扱い（ws236/ws.md、2026-10-15 ユーザーが Pi 4 の再生の限界を 720p30 と決めた）。VCHIQ・VIDEO は既定で on。p012 は 2026-10-15 ユーザーの決定で canceled | p001〜p005・p007・p011 の Phase の印、1080p・60 fps は範囲の外 |
| [WS238](../../ws238/ws.md) | incomplete | completed |  | p001 の実装（ESP・FAT32 ZEDBSD・UFS・swap の layout）は main。U1 の UAT（/boot/boot0・/boot/efi、Windows・macOS で ZEDBSD の区画）は実機の確かめだけ → 2026-10-27 ユーザー「実機確認だけが残っているWSやPhaseは、古いものはclearedにしてしまいます。」 | 実機の確認は未実施 |
| [WS239](../../ws239/ws.md) | incomplete | completed |  | p001 の実装（/dev/system の KERN_SYSTEM_SHOW_CONSOLE、sessiond の失敗の時の console への戻し）は main（include/uapi/system.h・src/drivers/generic/system-device.c・userland/desktop/sessiond/main.c） | p002 の QEMU の確かめは未実施 |
| [WS240](../../ws240/ws.md) | planning | planning・保留（開始しない） | 開始しない | 2026-10-15 ユーザー「50はまだ開始しないでください。」 | — |
| [WS241](../../ws241/ws.md) | incomplete | canceled（WS291 へ） |  | RTX の描画の速さは WS291（2026-10-26 23:08 ユーザー「WS291にさらにフェーズを追加して、DebianマシンでGPUを入れ替えて、NVIDIAでも同様に、デスクトップの60fpsと、モデルの50fpsをやります。」）の p010・p011 に引き継いだ | WS291 p010・p011 |
| [WS242](../../ws242/ws.md) | incomplete | incomplete・保留（止まったまま） | 止まったまま | p001 は 2026-10-15 に P2 の途中の WIP、p002 は host・build まで。実機（RTX 30/40）は「ユーザーの指示で無し」、2026-10-15 から止まっている | p001 の再開点 1〜4、GTX 16・RTX 30・40 の既定の有効（ユーザーの決定、未実装） |
| [WS243](../../ws243/ws.md) | incomplete | incomplete・保留 | ユーザーの保留・中断 | 2026-10-15 ユーザーの指示で一時停止（ws243/ws.md「p001 を途中で一時停止、実機の debug は WS241 の後」） | p001 の design.md から |
| [WS246](../../ws246/ws.md) | incomplete | completed |  | p001 cleared、2026-10-22 ユーザーの指示で compiler/sm75/ の 14 file に分け直した（d73ca1f65）、出典の comment は ws247-p003 で済み（Master の WS の一覧） | p002 の latency の表への直しは残す、p003 の規約は一括の見直しへ |
| [WS248](../../ws248/ws.md) | incomplete | completed |  | p001・p002 cleared（main 285c55e39、T1-698・T1-700）。残りは 5330 の素の起動での接続（U9）の実機の確かめだけ → 2026-10-27 ユーザー「実機確認だけが残っているWSやPhaseは、古いものはclearedにしてしまいます。」 | 実機の確認は未実施 |
| [WS249](../../ws249/ws.md) | incomplete | completed |  | p001・p002 cleared（main 331d314be）。残りは Pi 4 の実機の WiFi の確かめだけ → 2026-10-27 ユーザー「実機確認だけが残っているWSやPhaseは、古いものはclearedにしてしまいます。」 | 実機の確認は未実施 |
| [WS250](../../ws250/ws.md) | incomplete | incomplete・保留 | ユーザーの保留・中断 | 2026-10-15 夜 ユーザーの即時中断で止めた（ws250/ws.md）、「nvrtx の取り消しと復帰（WS250）は止めたまま」（decisions-log、2026-10-15 夜） | p001 の設計（草稿は main 64f05b2c7） |
| [WS251](../../ws251/ws.md) | incomplete | incomplete・保留（作業対象になった時） | 作業対象になった時 | p002（GPU ドライバ）cleared。2026-10-15 ユーザー「開発管理はすべてのソースから削除しますが、今は作業対象になったときに削除するだけでいいです。…独立したWSを作って、あとで…」。src/drivers の gpu/ 以外は WS296 に移した（2026-10-27 00:22） | p001 の棚卸し、drivers の外の群 |
| [WS252](../../ws252/ws.md) | incomplete | completed |  | p001〜p006・p008 は実装・host 試験・5330 の回帰 PASS（2026-10-26 q1599 P4）。残りの p007 は RKL・ADL-S・ICL の実機の確かめだけ（実機が無い） → 2026-10-27 ユーザー「実機確認だけが残っているWSやPhaseは、古いものはclearedにしてしまいます。」 | 実機の確認は未実施 |
| [WS253](../../ws253/ws.md) | incomplete | completed |  | p001 の 2 段の起動・late・disk の待ちの短縮は main（4e3ded77f・d454d4bd3・cf638a523）、QEMU で約 9 s → 約 4 s。残りは 5330 の起動の時間の確かめだけ → 2026-10-27 ユーザー「実機確認だけが残っているWSやPhaseは、古いものはclearedにしてしまいます。」 | 実機の確認は未実施、p002 の規約は一括の見直しへ |
| [WS254](../../ws254/ws.md) | incomplete | completed |  | p001（DHCP の失敗の後 60 s おきに down → up → dhcpc）を QEMU で確かめて main（5cb63ff2a）。実機は確かめだけ | 実機の確認は未実施、p002 の規約は一括の見直しへ |
| [WS255](../../ws255/ws.md) | incomplete | incomplete・保留（BUG-328 待ち） | BUG-328 待ち | p001 の手引きと QEMU は PASS。static の default route と DNS が起動の後に入らない BUG-328（tracking、未割当）待ちで 2026-10-16 から止まっている | BUG-328 の直し |
| [WS256](../../ws256/ws.md) | incomplete | completed |  | VF2 の移植は板で動く: 2026-10-20 ユーザー「画面がつきました！」（HDMI・USB keyboard の login、p015）、LAN は 2026-10-20 ユーザー「pingはOKです。」（p018）、NVMe p024・MSI p026 cleared（2026-10-26）、p006 規約 cleared。残りの p019（音）・p023（coherency）・p022 は実機の確かめだけ → 2026-10-27 ユーザー「実機確認だけが残っているWSやPhaseは、古いものはclearedにしてしまいます。」 | 実機の確認（音・coherency・PERST）は未実施、p018 の ssh |
| [WS257](../../ws257/ws.md) | planning | planning・保留（今は取り組まない） | 今は取り組まない | 2026-10-18 13:25 ユーザー「Haswell, Broadwellへの対応はWSを作っておいて、今は取り組みません。」 | — |
| [WS259](../../ws259/ws.md) | incomplete | completed |  | p001 の表の書き換え（13 配列とも値が同一、merge ee2de1f9d）と license の package の外し、p002 の規約 cleared。残りは実機の WiFi の確かめだけ → 2026-10-27 ユーザー「実機確認だけが残っているWSやPhaseは、古いものはclearedにしてしまいます。」 | 実機の確認は未実施、package の directory の削除は Q1 |
| [WS260](../../ws260/ws.md) | incomplete | completed |  | p001 cleared、p002〜p015 は実装済み（gen9-*.c を xd に書き直し）。残りは T1-710 の SV7・5330 の実機の回帰だけ（U24・U25） → 2026-10-27 ユーザー「実機確認だけが残っているWSやPhaseは、古いものはclearedにしてしまいます。」 | 実機の確認は未実施 |
| [WS269](../../ws269/ws.md) | incomplete | completed |  | VF2 の板で Keiland の desktop が PowerVR で動く（beta2.md U36、2026-10-21: desktop・3 window・lockup 無し・vkmatrix 36/36、gpu-selftest 13/13）。2026-10-21 ユーザー「表示内容自体は完璧です」 | p004〜p017 の Phase の印と実機の行の確かめ（実機の確認は未実施の行あり）、速さは WS291（PowerVR の model 50 fps） |
| [WS270](../../ws270/ws.md) | incomplete | completed |  | 2026-10-22 ユーザー（main e6b12bba9 の kernel と Keiland の VF2 の desktop を操作して）「すばらしい。実用スピードです。」→ ws270-p007 の目的は到達。desktop の 60 fps は WS291 で「ws270-p007 で済み」 | 速さの残り（drag の acquire など）は WS291（PowerVR）、p002・p004・p006a の Phase の印 |
| [WS273](../../ws273/ws.md) | incomplete | incomplete・保留 | ユーザーの保留・中断 | 2026-10-19 22:22 ユーザー「…RG350はきりのいいところでラップアップしましょう。途中までのコードをコミットしてOKです。」（p001・p002 cleared、p003 途中 main 7adce0d6d） | 再開は ws273/phase003/phase.md、実機 U26 |
| [WS275](../../ws275/ws.md) | incomplete | incomplete・保留 | ユーザーの保留・中断 | 2026-10-19 22:22 ユーザー「sun4uはバグの原因がわからなければBUGチケットに積んでいったんやめます。」→ BUG-337 で停止。p001〜p005 は main 50bd10c5f | p006（BUG-337）、p007 |
| [WS277](../../ws277/ws.md) | incomplete | completed |  | DC8200 の zero copy の flip と hardware cursor が板で動く（beta2.md U36 の 2026-10-21 09:55）。p002・p003・p005 は実装済みで実機の行の確かめだけ → 2026-10-27 ユーザー「実機確認だけが残っているWSやPhaseは、古いものはclearedにしてしまいます。」 | U0〜U11 の行の確認は未実施 |
| [WS278](../../ws278/ws.md) | incomplete | completed |  | Pi 5 の image で起動し desktop・USB の mouse が動く（WS279 の 2026-10-22 のユーザーの確かめ）。D1〜D3 は 2026-10-21 ユーザーが回答済み（「D1: serialは不要です。 D2: 分けましょう。 D3: Pi5 2GBです。…」、Master の「D1〜D3 待ち」は古い）。BT は 2026-10-22 ユーザー「BTはPi4でも動作していないため、いったんドライバを無効にしてください。」で無効 | p001〜p009 の Phase の印（実機の行の確かめは未実施）、BT（無効）、BUG-345 |
| [WS279](../../ws279/ws.md) | incomplete | completed |  | Pi 5 で Keiland が GPU で動く: 2026-10-22 ユーザー「System Monitor を開いて、中身が出ました。」「マウスを動かして、cursor が十分滑らかです。」、2026-10-23 ユーザー「Notesは問題なかったです。」（stencil）。D1〜D4 は 2026-10-21 ユーザー「Pi5のdecisionは推しでOKです」で決定済み（Master の「D1〜D4 待ち」は古い） | GPU の速さ（Master の優先順「Pi 5 の残り: ユーザーの指示の時」） |
| [WS280](../../ws280/ws.md) | planning | planning・保留（後日） | 後日 | 2026-10-21 ユーザー「…H.264のソフトウェアデコーダを、あとで実装するWSを作っておきましょう。…まだ実装しなくていいです」 | — |
| [WS281](../../ws281/ws.md) | planning | planning・保留（後日） | 後日 | 2026-10-22 ユーザー「mallocは速くないと思います。これはWSを作って、後日取り組みましょう。」 | — |
| [WS282](../../ws282/ws.md) | incomplete | incomplete・保留（設計だけ、実装は後日） | 設計だけ、実装は後日 | 2026-10-23 ユーザー「おっと、意外と工数がかかりそうですね……。設計だけ残してラップアップして、後日実装することにします。」 | p002〜p011（設計 版 3 は済み） |
| [WS283](../../ws283/ws.md) | incomplete | incomplete・保留（設計だけ、実装は後日） | 設計だけ、実装は後日 | 2026-10-23 ユーザー「…設計だけ残してラップアップして、後日実装することにします。」（WS282〜WS285） | p002〜p014 |
| [WS284](../../ws284/ws.md) | incomplete | incomplete・保留（設計だけ、実装は後日） | 設計だけ、実装は後日 | 2026-10-23 ユーザー「…設計だけ残してラップアップして、後日実装することにします。」（WS282〜WS285） | p002〜p008 |
| [WS285](../../ws285/ws.md) | planning | planning・保留（後日） | 後日 | 2026-10-23 ユーザー「…設計だけ残してラップアップして、後日実装することにします。」（WS282〜WS285） | p001 設計 |
| [WS287](../../ws287/ws.md) | planning | planning・保留（後日） | 後日 | 2026-10-23 ユーザー「USBは後回しにして、full speed, low speedはWSを作ってあとで対処し、今はFPGAブロックを動作させるということをゴールにして最短で進みましょう。」 | — |

## 保留の一覧（元の段ごと、Q1 とユーザーの整理用）

| 元の段 | WS | 内容の残り |
| --- | --- | --- |
| BUG-328 待ち | [WS255](../../ws255/ws.md) | BUG-328 の直し |
| DOC-54 | [WS009](../../ws009/ws.md) | DOC-54（docs/ に /dev/gpu の参照の文書は無い） |
| Future Work F-002 | [WS013](../../ws013/ws.md) | F-002（runtime の隔離・CLI・service container） |
| HFP はベータ3 | [WS197](../../ws197/ws.md) | p006・p007 HFP、p010 の MMS の送信、p014、p008 実機、p009 規約 |
| HFP はベータ3 | [WS204](../../ws204/ws.md) | p001 の U1・U2、p002〜p005 |
| HFP はベータ3 | [WS205](../../ws205/ws.md) | p001 の D1〜D6、p004〜p011 |
| HFP はベータ3 | [WS209](../../ws209/ws.md) | p001 の再 review、p004〜p006 |
| Pi 4 の完了の範囲の外 | [WS207](../../ws207/ws.md) | p001 の判断、p002 の実機、p003・p004 |
| ブロック | [WS225](../../ws225/ws.md) | p001 |
| ベータ3 | [WS001](../../ws001/ws.md) | p041〜p044 の guest 試験、p045 POSIX の header、p046〜p051、p040 mesg（BUG-067） |
| ベータ3 | [WS026](../../ws026/ws.md) | Phase 未定義 |
| ベータ3 | [WS052](../../ws052/ws.md) | p001・p002 の HAL の差分の承認、p005〜p012 の実機・QEMU |
| ベータ3 | [WS115](../../ws115/ws.md) | p010（GTK の起動、ld.so の上限は WS140 で解決済み）、p003 |
| ベータ3 | [WS116](../../ws116/ws.md) | p001〜p005（WS117 の後） |
| ベータ3 | [WS126](../../ws126/ws.md) | p005 の guest の受け入れ（test-wait）、p006 規約 |
| ベータ3 | [WS152](../../ws152/ws.md) | p001 の U1〜U12 の判断 |
| ベータ3 | [WS158](../../ws158/ws.md) | p002〜p005 の T1・ユーザーの日本語の review、p006 |
| ベータ3 | [WS176](../../ws176/ws.md) | p001 から |
| ベータ3 | [WS184](../../ws184/ws.md) | p001 設計から |
| ベータ3 | [WS185](../../ws185/ws.md) | p001 設計から |
| ベータ3 | [WS186](../../ws186/ws.md) | p001 調査と設計から |
| ベータ3 | [WS198](../../ws198/ws.md) | p001 の判定から |
| ベータ3 | [WS201](../../ws201/ws.md) | p001 設計 |
| ベータ3 以降 | [WS117](../../ws117/ws.md) | p001〜p006 |
| ベータ3 以降 | [WS180](../../ws180/ws.md) | Phase 未定義 |
| ベータ4 以降 | [WS119](../../ws119/ws.md) | p001 要件の案から |
| ベータ4 以降 | [WS144](../../ws144/ws.md) | p001 から |
| ベータ4 以降 | [WS146](../../ws146/ws.md) | p001 から（WS150 の後） |
| ベータ4 以降 | [WS147](../../ws147/ws.md) | p001 から（WS150 の後） |
| ユーザーが検討中 | [WS153](../../ws153/ws.md) | U2〜U15 の決定の後に p002 以降 |
| ユーザーの保留・中断 | [WS080](../../ws080/ws.md) | p002 は agent/p1 の a06d5d75d・6c7c991b1（未 merge）、p003〜p012 |
| ユーザーの保留・中断 | [WS234](../../ws234/ws.md) | p001 |
| ユーザーの保留・中断 | [WS243](../../ws243/ws.md) | p001 の design.md から |
| ユーザーの保留・中断 | [WS250](../../ws250/ws.md) | p001 の設計（草稿は main 64f05b2c7） |
| ユーザーの保留・中断 | [WS273](../../ws273/ws.md) | 再開は ws273/phase003/phase.md、実機 U26 |
| ユーザーの保留・中断 | [WS275](../../ws275/ws.md) | p006（BUG-337）、p007 |
| ユーザーの判断待ち | [WS221](../../ws221/ws.md) | p001 の判断から |
| ユーザーの判断待ち | [WS223](../../ws223/ws.md) | p001 の判断から |
| 今は取り組まない | [WS257](../../ws257/ws.md) | — |
| 作業対象になった時 | [WS251](../../ws251/ws.md) | p001 の棚卸し、drivers の外の群 |
| 優先度低め、ユーザーの review 待ち | [WS171](../../ws171/ws.md) | p002 のユーザーの review、p003 の適用 |
| 実装はベータ3 | [WS172](../../ws172/ws.md) | p004b の TPM の設計、p005 OpenSSL の置き換え、p007・p011 の試験、p006 規約 |
| 後日 | [WS222](../../ws222/ws.md) | p001 |
| 後日 | [WS280](../../ws280/ws.md) | — |
| 後日 | [WS281](../../ws281/ws.md) | — |
| 後日 | [WS285](../../ws285/ws.md) | p001 設計 |
| 後日 | [WS287](../../ws287/ws.md) | — |
| 描画の最適化の終了 | [WS210](../../ws210/ws.md) | p001 の確認から（Guardrail「画像の処理は GPU で」の残り） |
| 止まったまま | [WS226](../../ws226/ws.md) | p001 の review から |
| 止まったまま | [WS242](../../ws242/ws.md) | p001 の再開点 1〜4、GTX 16・RTX 30・40 の既定の有効（ユーザーの決定、未実装） |
| 着手はユーザーが決める | [WS212](../../ws212/ws.md) | p001〜p005 |
| 要検討 | [WS082](../../ws082/ws.md) | ユーザーの判断（study.md §10） |
| 要検討・ブロック | [WS098](../../ws098/ws.md) | p001 設計（WS095 の後、corpus と license はユーザー） |
| 要検討・ブロック | [WS150](../../ws150/ws.md) | p001 から |
| 見積もりだけ | [WS235](../../ws235/ws.md) | p001〜p007 |
| 設計だけ、実装は後日 | [WS282](../../ws282/ws.md) | p002〜p011（設計 版 3 は済み） |
| 設計だけ、実装は後日 | [WS283](../../ws283/ws.md) | p002〜p014 |
| 設計だけ、実装は後日 | [WS284](../../ws284/ws.md) | p002〜p008 |
| 開始しない | [WS240](../../ws240/ws.md) | — |

## master に反映する変更（P4 が新しい形の master に取り込む）

P3 は master.md を書き換えていない（Q1 の指示 2026-10-27）。

| 場所 | 今の記述 | 直し |
| --- | --- | --- |
| Workstream registry（未完の 150 行） | incomplete・planning・planned | 上の「WS ごとの判定」の新しい状態と「残り・制限」。completed 86・canceled 3 は plan/workstreams-closed.md へ（P4 の形）。WS068 は継続（空き時間だけ）、WS074 は継続（Codex のセッション） |
| registry の WS278・WS279 の再開点 | 「判断 D1〜D3 待ち」「ユーザーの判断 D1〜D4 待ち」 | 回答済み（2026-10-21 ユーザー「D1: serialは不要です。 D2: 分けましょう。 D3: Pi5 2GBです。…」「Pi5のdecisionは推しでOKです」）。両 WS とも completed |
| Focus block | 「fg020 VF2 は保留」 | fg020 は達成: WS256・WS269・WS270・WS277 が completed（2026-10-22 ユーザー「すばらしい。実用スピードです。」）。音・coherency の実機の確かめは未実施 |
| Focus block | 「Pi 5 の仕上げ（WS278・WS279）: 残りは BT・BUG-345・GPU の速さ」 | WS278・WS279 は completed。残りは BUG-345（Bug）、BT は 2026-10-22 ユーザー「BTはPi4でも動作していないため、いったんドライバを無効にしてください。」で無効、GPU の速さは優先順の「ユーザーの指示の時」 |
| Current Focused Goals の fg018 | 「WS114 … p007・p008 まで達成」で今の goal の扱い | WS114 は 2026-10-06 夜 ユーザー「…WS114はComplete、はすでにできており、Completeにしてください。」で completed。fg018 は達成の表へ |
| fg019 の内容の表 | ベータ2 の区分ごとの WS と状態 | fg019 は達成（2026-10-17 OSC）。表は履歴へ。WS の状態は registry が正 |
| Upcoming Work Outlook | 2026-10-23 の WS286 の線（WS286 は 2026-10-23 completed） | 今の線: P1 WS290（GHCI 2.0）、P2 WS291（RDNA → NVIDIA → PowerVR）、WS296（drivers の comment）、BUG-346。その後に保留の 60 件の整理（Q1 とユーザー） |
| Milestone の進捗の文 | MG001「WS009・WS026 が残る」、MG002「WS001、WS034、WS042 が進行中」、MG003「WS028 が残る」、MG005「WS005・WS033 が残る」、MG007「5OS package/CI配布はWS112で計画のみ」、MG008「WS036・WS027・WS044 が残る」 | MG001: WS009・WS026 は保留。MG002: WS034 canceled、WS042 completed、WS001 は保留。MG003: WS028 canceled（2026-10-05）。MG005: WS005・WS033 completed。MG007: WS112 completed（Debian 13・Ubuntu 26.04・RPi OS の 3 つの deb）。MG008: WS036 completed、WS027 canceled、WS044 closed |
| 工数の見積もり（残り、LW） | 2026-10-05〜06 のベータ2 までの見積もり | 古い。削除して git の履歴へ（ベータ3・4 の段は 2026-10-27 ユーザーの決定で取り消し） |
| ベータ3・ベータ4 の記述（Target・段） | 各所の「ベータ3」「ベータ4 以降」 | 2026-10-27 ユーザー「なお、ベータ3，ベータ4という概念はキャンセルされました。…」。段は Q1 とユーザーの整理の結果で置き換える |
| open-decisions | 「WS204・WS205 の扱い」 | WS197・WS204・WS205・WS209（HFP と音の経路）はまとめて保留、整理の対象 |

## ユーザーに質問（Q1 がまとめて聞く。短い問いと推し）

| # | 問い | 推し |
| --- | --- | --- |
| 1 | 各 WS の「規約の全文の見直し」の Phase（2026-10-08 ユーザー「規約の整形（各 WS の全文規約の Phase）はベータ3」で残した物。completed にした WS のうち約 60 件）を、1 つの WS にまとめて後でやるか | 1 つの WS（WS296 と同じ形）にまとめ、時期は整理で決める |
| 2 | WS223: 2026-10-13 の決定「Video Playerは、Game Modeをやめます。Game Modeでない全画面を実装します。」が code に無い（userland/desktop/wayland/scanout-rules.c は今も video を直の scanout にする）。D1〜D6 を推しで決めて実装するか | 推しで決めて実装（content の条件を game だけに、小さい） |
| 3 | WS255（試験機の static IP）: BUG-328（static の route・DNS が入らない）を直して続けるか、WS254 の DHCP の 1 分おきの取り直しで足りるので取り下げるか | 取り下げ（BUG-328 は Bug として残す） |
| 4 | WS210（app の描画を GPU の canvas へ）: 保留か取り下げか | 保留 |
| 5 | WS068（desktop GL 3.3〜4.6）: 2026-10-05 の「保留を解除…やることがないときに」のまま継続か、保留に戻すか | 保留 |
| 6 | WS009 DOC-54（/dev/gpu の UAPI の文書）: いつ書くか | GHCI 2.0（WS290）の後に書く、それまで保留 |
| 7 | WS242（RTX 30/40）・WS243（NVDEC）・WS250（nvrtx の取り消しと復帰）: WS291 の NVIDIA（p010・p011）の後にまとめるか | 保留のまま、WS291 の NVIDIA の後 |
| 8 | WS207（Pi 4 の音）: 2026-10-12「RPi4のWiFiとBluetoothを実装し、RPi4は完了です。」の範囲の外。保留か取り下げか | 保留 |
| 9 | WS221（Jumbo frame）・WS226（Vulkan 1.3 を実装しきる）: 設計の後に止まったまま。続けるか | 保留 |

## 記録の食い違い（壊れた記録として直した物・直す物）

| 記録 | 食い違い | 扱い |
| --- | --- | --- |
| plan/queue.md | 2026-10-26 以前の Queue の行 91 件が pending・in-progress・実行中・active のまま（担当は終わっている） | 結果の欄を「finished・uncleared（2026-10-27 P3 の精査 q1608 …）。旧: …」に直した。WS288 の行は「WS288 の終了で取り下げ」、q1599 は WS252 の実装済みで finished。q1594 以降（今の作業）は触っていない。古い節の「Status: active」「、active）」も finished に |
| 各 ws.md の Status 行（148 件） | 「incomplete」のまま code と確かめでは済んでいる、ユーザーが止めたのに incomplete、など | 新しい状態・根拠・旧の値の 1 行に置き換えた（形は変えていない） |
| ws190/ws.md の表 | p002・p003 が test-wait、phase.md は cleared（beta2.md も「記録のずれ」と記録） | WS190 を completed（phase.md が正） |
| ws046 の phase.md | p007・p014 が uncleared、decisions-log は 2026-10-13 夜 ユーザー「満たしたとみなす」と T1-050 で cleared | WS046 を completed。phase.md は Phase の後始末で直す |
| ws112/ws.md の受け入れの表 P1〜P5 | 5 target・rpm・Arch の旧い受け入れが残る（2026-10-06 夜 ユーザーが 3 つの deb に絞った） | WS112 を completed。旧い表は完了の形にする時に消す |
| ws129/ws.md | 「ベータ1 のリリース作業」のまま。公開は 2026-10-17 のベータ2（rev.5 まで公開、2026-10-27 ユーザー「実質的にこれがベータ2でした」） | WS129 を completed |
| ws037・ws039 の Phase | desktop が実機で動くのに p001・p003〜p008（WS037）、p006 ほか（WS039）が in-progress | completed。Phase は後始末の一覧 |
| registry の WS246 | 「出典の comment は ws247-p003 で済み」と ws.md の p002（出典）が planned | completed（2026-10-22 ユーザーの指示で compiler/sm75/ に分け直し済み） |
| WS162 | 「WS172 に吸収、以後実行しない」なのに incomplete | completed（WS172・WS199 で FIDO2 の login を達成、2026-10-10 ユーザーの UAT） |

## Phase の後始末の一覧（completed にした WS の、表で cleared でない Phase の行）

Q1 の指示（2026-10-27）で今は直さない。completed の WS は AGENTS.md の規則で完了の形（Phase の dir の削除）にする時に閉じる。扱いの推し: 実機の確かめだけの物は cleared（10-27 の原文）、規約の全文は一括の見直しへ移す、未着手の付け足しは WS の外（Future Work か新しい WS）。

| WS | 件数 | Phase（表の状態の頭） |
| --- | --- | --- |
| WS004 | 6 | ws004-p005: Partial; automatic runtime cleared; ws004-p021: Uncleared (`q052`; automatic/runtime mil; ws004-p025: Planned physical checkpoint; depends on ; ws004-p034: Implemented, test-wait (2026-10-09 P1: c; ws004-p035: Planned/deferred; nonblocking; not queue; ws004-p038: Uncleared (`q066`); automatic and exact- |
| WS033 | 2 | p003: planned; p002: planning |
| WS037 | 9 | p001: in-progress（文書と review の反映済み、判断の項目を 2026; p002: planned（host 10.0.30.5 に Linux を入れて方式 B、; p003: in-progress（実装・build・host 試験済み、実機は T1 待ち; p004: in-progress（4a〜4e 実装・build・host 試験済み、実機（; p005: in-progress（R5〜R7・R7g は実機 PASS。2026-10-1; p006: in-progress（2026-10-15: P8a 実機 PASS。P8b ; p007: in-progress（2026-10-15 P3: 段 P9g（`nvrtx.; p008: in-progress（2026-10-15 P3: 段 A・B・C・E を実装; p009: planning |
| WS046 | 5 | ws046-p007: uncleared（q411-i01。主因は libc の allocator ; ws046-p011: uncleared（q420-i01。compile と link は速くなった; ws046-p008: uncleared（q421-i01。expat・zlib は guest で最; ws046-p014: uncleared（q434-i01。private の file の mapp; ws046-p005: planning |
| WS050 | 1 | ws050-p006: planned（実機の確認の分は OK（2026-10-13 ユーザー「UATの |
| WS051 | 2 | ws051-p005b: in-progress / 実機待ち（2026-10-08 P1 q877: r; ws051-p006: planned（全文規約の分はベータ3、2026-10-08 ユーザー） |
| WS075 | 7 | ws075-p006: in-progress（増分 1〜5 済み、後退を修正。増分 6 は実機が未実施; ws075-p007: planning（2026-10-07 設計、design-reviewer の; ws075-p010: planning; ws075-p017: uncleared（2026-09-28。6 回の起動で再現せず、原因は未特定）; ws075-p018: uncleared（2026-09-29 の試み: 着手前の計測で、8 app ; ws075-p022: uncleared、patch は保留、再検討は命令の並べ替えを入れるとき（20; ws075-p031: uncleared（2026-09-30、P1。ユーザーの指示で描画の高速化をラ |
| WS079 | 1 | ws079-p017: in-progress（2026-10-06 P2 が実装、build warn |
| WS081 | 3 | ws081-p017: in-progress（2026-10-13 P3 q957: 試験の file; ws081-p007: planning; ws081-p009: planning |
| WS083 | 1 | ws083-p009: planning（ベータ3、2026-10-08 ユーザー） |
| WS084 | 1 | ws084-p004: in-progress（2026-10-07 P2: 表・案・host 試験、判 |
| WS089 | 1 | ws089-p018: planning（最後） |
| WS090 | 4 | ws090-p009: test-wait → Q1 の判定待ち（q817 で実装。T1-261 で f; ws090-p012: planning（全文規約はベータ3、2026-10-08 ユーザー）; ws090-p018: test-wait → Q1 の判定待ち（T1-233 files-regres; ws090-p026: in-progress（2026-10-08 P1 q879: 層 1・2・4  |
| WS094 | 4 | ws094-p009: uncleared（2026-09-30、QEMU。(a) 1954・(c) 9; ws094-p012: planned（L1 のユーザーの操作の分は OK（2026-10-13 ユーザ; ws094-p015（条件つき）: planning（p012 の実機の値と main の判断が要る）; ws094-p007: uncleared（q588 source/host/build部分はclear |
| WS095 | 3 | ws095-p009: planning（WS074 はこの session の対象外。引継ぎか保留をユ; ws095-p010: planning; ws095-p011: planning |
| WS099 | 2 | ws099-p006（案）: planning（要る判断: C6 50 ms を 10/10 に保つか見直すか; ws099-p022: planning（最後。p019〜p021 の後） |
| WS101 | 4 | ws101-p017: uncleared（2026-09-30。libGLESv2 の device ; ws101-p012: planned; ws101-p018: planned; ws101-p013: in-progress（2026-10-18 Q1: phase.md に合わせ |
| WS102 | 4 | ws102-p011: in-progress — test-wait（2026-10-13 P2 q9; ws102-p012: planned; ws102-p013: planned; ws102-p014: planned |
| WS112 | 2 | ws112-p001: uncleared・canceled（2026-10-07 superseded; ws112-p007: planned |
| WS113 | 3 | ws113-p009: planned（全文規約の分はベータ3、2026-10-08 ユーザー。Linu; ws113-p014: test-wait（T1-370 QEMU で機能は ok、試験の期待は Q1 ; ws113-p010: planning（後回し。Linux・FreeBSD の作業は 10/13 以降 |
| WS121 | 1 | p003: planning（後に回した: 今は loader で全体を 64 MiB まで |
| WS129 | 5 | p005: planning（機能の一覧は各 WS の成果を待つ、10/13 頃。今書ける手; p006: planning; p013: in-progress（q715、P2。下書き済み、Q1・ユーザーの revie; p007: planning; p008: planning |
| WS130 | 1 | p009: planning |
| WS131 | 2 | p002: in-progress（2026-10-03 ユーザーのレビュー済み、q630 ; p024: planning |
| WS132 | 1 | ws132-p006: planning |
| WS134 | 2 | p009: test-wait（実機の温度の UAT の分は OK（2026-10-13 ユ; p010: in-progress（実機の p003・p007・p009 とデモの分は OK |
| WS141 | 13 | p001: in-progress（q691、文書と review 済み、判定待ち）; p002: in-progress（q695、実装済み。骨格版のT1-092 PASS、20; p003: in-progress（i11 allocator/登録/copy presen; p004: in-progress（V1〜V10/native runnerを実装、固定XM; p005: in-progress; p006: in-progress（compiler/native prepared job; p013: in-progress（hardware blend・compiler の NO; p014: in-progress（実機で 60fps、ユーザー確認）; p015: planning（第 3 版）; p016: planning（設計の第 1 版、review 前）; p018: planning（候補の記録、計測で方式を決める）; p017: in-progress（2026-10-22 P2、Pi 5 で VKSTENC; p019（案）: planning |
| WS143 | 5 | ws143-p003: in-progress（i01 は T1-402 PASS（2 回目、1 回目は; ws143-p005: in-progress（i01a T1-419 PASS、i01b T1-421; ws143-p006: test-wait（q896 P1 実装、T1-438 一部 PASS（差 2 ; ws143-p007: 実装済み・T1 の再試験待ち（2026-10-14 P3: T1-604 は文が; ws143-p009: planned（全文規約はベータ3、2026-10-08 ユーザー） |
| WS145 | 2 | ws145-p005: in-progress — test-wait（2026-10-13 P2 q9; ws145-p006: planning |
| WS155 | 1 | ws155-p005: planning（ベータ3、2026-10-08 ユーザー「コーディング規約によ |
| WS156 | 2 | ws156-p005: uncleared（5330 の実機の UAT の分は OK（2026-10-1; ws156-p006: in-progress / test-wait（2026-10-13 P3 q9 |
| WS157 | 9 | ws157-p002: uncleared・canceled（置き換え）; ws157-p003: uncleared・canceled（置き換え）; ws157-p011: in-progress（UAT の部分は OK、2026-10-13 ユーザー。; ws157-p012: in-progress（review・直し済み、設計は承認）; ws157-p013: in-progress（実装・host 済み、T1 待ち。UAT の部分は OK; ws157-p014: in-progress（実装・host 済み、T1 待ち。UAT の部分は OK; ws157-p015: in-progress — test-wait（実装は 2026-10-11 P; ws157-p016: in-progress（2026-10-11 P1 a1d5efa46: 実装・; ws157-p017: in-progress — test-wait（2026-10-13 P2 q9 |
| WS159 | 1 | ws159-p005: planned（実機の UAT の分は OK（2026-10-13 ユーザー「U |
| WS161 | 1 | ws161-p007: planned（ベータ3、2026-10-08 ユーザー） |
| WS162 | 4 | ws162-p001: planning（第 1 版、第 2 版 §9 q771。K1〜K3 は問わない; ws162-p002: planned; ws162-p003: planned; ws162-p004: planned |
| WS164 | 1 | ws164-p003: planned |
| WS165 | 1 | ws165-p004: planned（実機の UAT の分は OK（2026-10-13 ユーザー「U |
| WS169 | 2 | p006: planning（2026-10-06 夜 ユーザー「今は IMAP/SMTP ; p007: planning |
| WS170 | 1 | p005: planning |
| WS173 | 1 | p005: planned（起動の方法は 2026-10-13 ユーザー（AskUserQu |
| WS174 | 2 | ws174-p004（案、後日）: planning（ユーザー「BIOSは後日でよいです」）; ws174-p005: p003 の実装（p004 をやるならその後） |
| WS175 | 3 | ws175-p002: p002a・p002b 実装済み（2026-10-06、host PASS）; ws175-p010: in-progress（2026-10-18 Q1: phase.md に合わせ; p011: 未作成 |
| WS177 | 42 | ws177-p001: 案 E: compositor の wl_surface.enter・leave; ws177-p002: 案 F: i915 の TC の legacy の PHY の待ちを sleep; ws177-p003: 案 G の host の分: UCSI の誤りの理由・CANCEL・PPM_RE; ws177-p004: 案 D: desktop の UI の小物（kl_field の limit・K; ws177-p005: 案 A: 通知とメールの通知の口の堅さ（client の去り・UTF-8・速さ・; ws177-p006: 案 I の 45 行: fidoctl の PIN の入力（端末で echo を; ws177-p007: 案 B: Welcome の準正常系（印の失敗・Files の失敗・Networ; ws177-p008: 案 C: Files の Recents の仕上げ（止めた一覧の表示、Clear; ws177-p009: 案 H: 手書きの頑健さ（tap の note、templates の壊れ・空・; ws177-p010: 案 J: keiland-preview の仕上げ（libpdf の memor; ws177-p011: 案 K3: Notes の Save Clean Copy の仕上げ（form・; ws177-p012: 案 K（Notes の側）: 文字の box が zoom・scroll・窓に付; ws177-p013: 案 K の 9: libkeiland の field・text area の ; ws177-p014: 案 N4: Browser の sign-in code（欄が無い時は clip; ws177-p015: 案 N2: Mail の基本の操作（TLS の失敗の理由と自己署名の信頼、Tra; ws177-p016: 案 N: Mail の IMAP・SMTP の互換（ISO-2022-JP・Sh; ws177-p019: 案 O: Browser の IME・form と shell の fd（sur; ws177-p020: 案 M の 1: 音楽の collection と cover（絵のある .mp; ws177-p021: 案 M の 2: 音楽の再生の失敗と key（decode・読み・file の消; ws177-p022: 案 Q の 1: printd の同時の送信（printer ごと 1・全体 4; ws177-p023: 案 Q の 2: backend の printd の寿命の表（ACCEPTED; ws177-p024: 案 Q の 3: 設定の file の writer の thread、comp; ws177-p025: 案 Q の 4: Settings で printer の名前・IPP の pa; ws177-p026: 案 A2: Settings の Notifications の頁（Mail・C; ws177-p027: 案 T の 1: mediafile の fragmented MP4（mvex; ws177-p028: 案 T の 2: MPEG-TS の reader（PAT/PMT、PES、H.; ws177-p029: 案 T の 3: Ogg の reader（Opus・Vorbis・Theora; ws177-p030: 案 T の 4: AVI の reader（RIFF・hdrl/strl・mov; ws177-p031: 案 T の 5: videoplayer・Music を libmedia へ（; ws177-p033: 案 U の 1: 上端の帯の長押し（500 ms で press を流し直す、a; ws177-p034: 案 U の 2: 全画面の app へ上端の帯の press を流し直す（tou; ws177-p035: 案 U の 3: 整列の枠より大きい最小の大きさの窓は外して詰め直す、大きさの固; ws177-p036: 案 U の 4: 出力の大きさの変更で整列の枠を計算し直す、メニューの間の ne; ws177-p037: 案 U の 5: 整列の記憶（同じ desktop で同じ形を選び直すと前の窓を; ws177-p038: 案 U の 6: Home が開く・閉じる途中の bar の fade、Home; ws177-p040: 案 L の 1: libpdf の頁の文字に form XObject の中の文; ws177-p041: 案 L の 2: PDF Viewer の検索の一致の規則（行・ハイフン・空白、; ws177-p042: 案 L の 3: 選択（頁をまたぐ、語・行・全て、指の長押しと handle、回; ws177-p043: 案 L の 4: titlebar の無い時の窓の中の検索の欄、Enter の後; ws177-p044: 案 R2: IPv6 の libc と道具（AI_ADDRCONFIG を in; ws177-p045: 案 R: networkd の IPv6（net commit の消えた add; ws177-p046: 案 R: dhcpc の DHCPv6（Rebind・Release・Decli |
| WS181 | 1 | p012: in-progress（実機の感触の UAT の分は OK（2026-10-13 |
| WS189 | 2 | p002: test-wait（T1-433・436・437・439・442 で early; p003: test-wait（T1-434・437・439 で text-between- |
| WS190 | 2 | p002: test-wait（T1-443、未実行）; p003: test-wait（T1-445、未実行） |
| WS193 | 1 | p003: planning |
| WS194 | 1 | p003: planning |
| WS202 | 16 | ws202-p001: uncleared（software出力あり、whole未達）; ws202-p002: uncleared（software出力あり、whole未達）; ws202-p003: uncleared（software出力あり、whole未達）; ws202-p004: uncleared（software出力あり、whole未達）; ws202-p005: uncleared（software出力あり、whole未達）; ws202-p006: uncleared（software出力あり、whole未達）; ws202-p007: uncleared（software出力あり、whole未達）; ws202-p008: uncleared（software出力あり、whole未達）; ws202-p015: uncleared（software出力あり、whole未達）; ws202-p016: uncleared（software出力あり、whole未達）; ws202-p009: uncleared（software出力あり、whole未達）; ws202-p010: uncleared（software出力あり、whole未達）; ws202-p011: uncleared（software出力あり、whole未達）; ws202-p012: planned（ユーザーの UAT の分は OK（2026-10-13 ユーザー; ws202-p014: planned（T1/User UAT/whole規約待ち）; ws202-p017: uncleared（software接続/build済み、実機pixel待ち） |
| WS203 | 1 | ws203-p003: uncleared |
| WS206 | 8 | p001: in-progress（第 3 版、design-reviewer 待ち）; p002a: in-progress（実装・host・build 済み dc2504ff2、T; p002b: in-progress（実装・host・build 済み、Pi の実機と T1 ; p004: in-progress（2026-10-13 P4、実装・host 試験・bui; p005: in-progress（2026-10-13 P3、実装・host 試験（rad; p006: planned; p007: planned; p008: in-progress（2026-10-13 P3、実装・host 試験（ASa |
| WS208 | 7 | p001: in-progress（設計、D1〜D6 は 2026-10-11 全部承認）; p002: in-progress（実装済み、T1 待ち）; p003: in-progress（実装済み、T1-537 一部 FAIL の判定は Q1）; p004: in-progress（main を統合 b44b2420d、T1 待ち）; p005: planning; p006: planning; p007: in-progress（実装済み、Pi の再現待ち） |
| WS211 | 11 | p001: planning（第 4 版、review-4 待ち）; p002: planned; p003: 段 1 実装（未 merge）、段 2 は床の試しから; p005: in-progress（実装済み、Pi 未確認）; p006: in-progress（cursor の present の間隔の直しは mai; p007: in-progress（実装、place16 で Pi 待ち）; p008: in-progress（実装 db55254d4、place23 で Pi 待ち; p009: in-progress（実装 e6e7b1a9b・4ec9f16d1、place; p010: in-progress（実装 agent/p4、place25 で Pi 待ち）; p011: in-progress（1 は main、2 は agent/p3、host 試; p004: planned |
| WS213 | 7 | p001: planning（第 6 版、review-5 で S2 条件付き可、Q1 の確; p002: in-progress（main 41bd7a5a4 まで、Pi・T1 の確認待; p003: in-progress（実装済み、T1・Pi の確かめ待ち）; p004: in-progress（段 I・C・R 実装済み。place23 の box_l; p005: in-progress（実装済み agent/p4 09e429a9a、host; p006: planned; p007: in-progress（K-D 実装済み agent/p9 45ae288d7、 |
| WS214 | 4 | p001: planning（第 3 版、review-2 を反映、Q1 の確かめの前）; p005: 案（未投入）; p006: 案（未投入）; p002: planned |
| WS215 | 8 | p001: in-progress（第 2 版 + 第 3 版の補い（review-2 への; p002: in-progress（build warning 0・host 試験 PASS; p003: in-progress（P2、build warning 0・host 試験 P; p004: in-progress（package・host 試験 PASS・本文の一致を確; p005: in-progress（P2、build warning 0・host 試験 P; p006: planned; p007: planned; p008: planned |
| WS216 | 4 | p001: in-progress（設計 2、review-2 の待ち）; p007: in-progress（T1-580 の QEMU は PASS（2026-10; p008: planned; p009: planned |
| WS217 | 9 | p001: planning（第 4 版 2026-10-13 P4、review-3 の全; p009-1: **planned（review-3 の判定で開始可）**; p009: 案（review-4 の後。p002 と並行可。実機 5330・5320 で受け; p010: 案; p005: 案; p011: 案（Pi の実機で受け入れ）; p006: 案（Q1 の判断で p006a/p006b に割ってよい）; p007: 案; p008: 案 |
| WS218 | 2 | p001: in-progress（監査は済み、2026-10-13 のユーザーの決定で p; p002: in-progress（実装・build・host 試験・機械比較済み、T1・実 |
| WS219 | 2 | p001: in-progress（2026-10-13 P2 実施。表現の写しは無し、Go; p002: in-progress（2026-10-13 P2 実装・build warni |
| WS220 | 6 | p001: in-progress（T1 の基線待ち）; p002: in-progress（2026-10-13 P4 c13a8413f、実装・模; p006（案）: planning; p007（案）: planning; p008（案）: planning（候補）; p009（案）: planning |
| WS229 | 4 | p002: in-progress — merge 済み（93787f2be）、host の; p003: in-progress — merge 済み（28d6deaae）、host の; p004: in-progress — merge 済み（42431971b）、host の; p006: planned（今日の範囲外） |
| WS232 | 1 | p004: in-progress（host 試験 PASS、T1 の Pi の実機待ち） |
| WS233 | 2 | ws233-p001: in-progress（P1 の実装と host の確かめは済み。Q1 の pu; ws233-p002: in-progress（P1 の実装と host の確かめは済み。端末での目視は |
| WS236 | 1 | ws236-p011: 案 1: decoder の出力を GPU の texture に、表示を GP |
| WS238 | 1 | ws238-p001: in-progress（実装と host の確認まで。T1・5330 待ち） |
| WS239 | 2 | ws239-p001: in-progress（実装・build・host 試験済み、Q1 の判定待ち）; ws239-p002: planned |
| WS246 | 3 | p001（cleared）: planned; p002: planned（2026-10-15: 定数・関数ごとの referenced:; p003: planned |
| WS252 | 6 | p002: in-progress（2026-10-26 q1599 P4: f8b65c8; p003: in-progress（2026-10-26 q1599 P4: 既定で att; p004: in-progress（2026-10-26 q1599 P4: e683619; p005: in-progress（2026-10-26 q1599 P4: 4e316fe; p006: in-progress（2026-10-26 q1599 P4: 557c9de; p007: planned（実機） |
| WS253 | 2 | p001: in-progress（q1221、実装済み・5330 の確認待ち）; p002: planned |
| WS254 | 2 | p001: in-progress（q1226、QEMU PASS、Q1 の判定待ち）; p002: planned |
| WS256 | 7 | p010: in-progress（(a)・(c) 済み。(b) sysroot.mk の ; p005: planning; p015: in-progress（2026-10-20: ユーザーの VF2 の実機（20; p018: in-progress（2026-10-20 ユーザーの VF2 の実機: li; p019: in-progress（板の前の code と host 試験 58 PASS ; p022: planning; p023: in-progress（試験の cdev と core は済み（86e86aa0 |
| WS259 | 1 | p001: in-progress（2026-10-19 P2: 表の書き換えと値の比べ（1 |
| WS260 | 14 | p002: in-progress（q1253、P1）; p003: in-progress（q1253、P1）; p004: in-progress（q1253、P1）; p005: in-progress（q1253、P1）; p006: in-progress（q1253、P1）; p007: in-progress（q1253、P1）; p008: in-progress（q1253、P1）; p009: in-progress（q1253、P1）; p010: in-progress（q1253、P1）; p011: in-progress（q1253、P1）; p012: in-progress（q1253、P1）; p013: in-progress（q1253、P1）; p014: in-progress（q1253、P1）; p015: in-progress（q1253、P1、T1 の結果待ち） |
| WS269 | 30 | p004: in-progress（2026-10-19 P2、q1330。実装・build; p005: in-progress（2026-10-19 P2、q1332 前半。実装・bu; p006: in-progress（2026-10-19 P2、q1337。実装・build; p007: in-progress（2026-10-19 P1 q1333 前半、P2 q1; p008: in-progress（2026-10-20 P2、q1345 前半: USC・; p009: in-progress（2026-10-20 P2、q1352: capset（; p010: in-progress（2026-10-20 P1 q1370: 版 2.1（r; p010a: in-progress（2026-10-20 P1 q1364: stage.c; p010c: in-progress（2026-10-20 P1 q1368: image-s; p010d: in-progress（2026-10-20 P1 q1373: render-; p010n: in-progress（2026-10-20 P1 q1375-i01: 部分。; p010e: in-progress（2026-10-20 P2 q1378-i01 前半: ; p010f: in-progress（2026-10-20 P2 q1379-i01 前半: ; p010o: in-progress（2026-10-20 P2 q1381-i01 前半: ; p010g: in-progress（2026-10-20 P2 q1382-i01 前半: ; p010-compiler: in-progress（2026-10-20 P2 q1363、p013・p01; p010h: in-progress（2026-10-20 P2 q1374: descrip; p010i: in-progress（2026-10-20 P2 q1384-i01 前半: ; p010j: in-progress（2026-10-20 P2 q1387-i01 前半: ; p010k1: in-progress（2026-10-20 P2 q1386-i01: lib; p010p: in-progress（2026-10-20 P2 q1389-i01: R32; p010k2: planning（2026-10-20 P1 q1391-i01: host の; p010l: in-progress（2026-10-20 P2 q1390-i01: ext; p010m: planning（同）; p011: in-progress（2026-10-20 P2 q1392-i01・q139; p013: in-progress（2026-10-20 P2: 最初の試行 q1363（記; p014: in-progress（2026-10-20 P2: 最初の試行 q1363（記; p015: in-progress（2026-10-21 Q1: 板で連続 10 回と電源の; p017: in-progress（2026-10-21 P1 q1528-i01: 設計は; p012: in-progress（2026-10-20 P1 q1396-i01: P1  |
| WS270 | 4 | p002: in-progress（2026-10-19 P1、q1336・q1338。試し; p004: in-progress（2026-10-20 P1、q1355。image と手; p006a: in-progress（2026-10-20 P1、q1356: 2a の ho; p007: in-progress（2026-10-21 Q1: q1538 P1・q153 |
| WS277 | 3 | p002: in-progress（2026-10-20 P1 q1348-i01: 実装・; p003: in-progress（2026-10-20 P1 q1351-i01: 実装・; p005: in-progress（2026-10-20 P1 q1353-i01: 実装・ |
| WS278 | 10 | p001: in-progress（2026-10-21 P3 q1542、設計を書いた、Q; p002: in-progress（2026-10-21 image まで済み、実機の確認待; p003: in-progress（2026-10-21 実装と host の確認済み、実機; p004: in-progress（2026-10-21 実装と host の確認済み、実機; p004b: in-progress（2026-10-21 実装と host の確認済み、実機; p005: in-progress（2026-10-21 実装と build、実機は一括の試; p006: in-progress（2026-10-21 実装と build、実機は一括の試; p007: in-progress（2026-10-21 実装と build、実機は一括の試; p008: in-progress（第 1・2 部 2026-10-21 済み、rpi4 の; p009: in-progress（2026-10-22 実装と板の確認済み、Q1 の判定待 |
| WS279 | 8 | p001: in-progress（2026-10-21 P2 q1544、設計の第 1 版; p002: in-progress（2026-10-22 P2、実機で V0〜V4 PASS; p003: in-progress（2026-10-22 P2、実機で self-test ; p005: in-progress（2026-10-22 P2、compositor の q; p006: in-progress（2026-10-22 P2、P0〜R3 実機 PASS、; p007: in-progress（2026-10-22 P2、`rpi5gpu.start; p008: planning; p009: planning |
