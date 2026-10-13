# P3 の Queue lane（2026-10-11 generation、Q1 投入）

承認: 2026-10-11 ユーザー（このセッション、N=3＋T1 の指示）「P3はRaspberry Pi 4のVC6ドライバの改善です。ssh kei@10.0.30.2で接続できます。パスワードはkeiです。…これを引き継いで、30fpsの33msが1つめの目標、60fsの16ms以下が最終目標です。」
Worktree: `/home/awe/zedBSD-worktrees/p3`、branch `agent/p3`、base codex/rpi4-gpu-init d27925686。agent type phase-runner（high）。

| Queue | Phase | State |
| --- | --- | --- |
| q936 | [ws141-p013](../../ws141/phase013/phase.md) | active |

---

以前の記録（履歴）:

# P3 Queue lane

| Queue / attempt | Phase | Scope | Approval | Timebox | State |
| --- | --- | --- | --- | --- | --- |
| q592 / q592-i01 | [ws114-p007](../../ws114/phase007/phase.md) | CSD/SSD の残り5点（新 attempt） | 2026-10-02 user「作業を開始しましょう。」 | 3h | finished / cleared |

| q597 / q597-i01 | [ws115-p001](../../ws115/phase001/phase.md) | 素の GTK4 移植の契約 | user 2026-10-02「まずは素のGTK4を移植してください」 | 4h | finished / cleared |
| q600 / q600-i01 | [ws115-p004](../../ws115/phase004/phase.md) | meson の cross 契約と host 道具 | 継続 dispatch | 4h | finished / cleared |
| q602 / q602-i01 | [ws115-p005](../../ws115/phase005/phase.md) | libffi・pcre2・glib | 継続 dispatch | 4h | in-progress |

Next（予約）: ws115-p006（libpng・freetype・harfbuzz・fontconfig）〜

## Merge requests

| P3-001 | q592 | 8856edf21（base 901037f9f） | plan/ws114 evidence/tests のみ | integrated debba7e9e |
| P3-002 | q592 | b2ebbfee6（前回 8856edf21） | plan/ws114 evidence/結果 | integrated d10ad2cf3 |
| P3-003 | q592 | 597841d2a（前回 b2ebbfee6） | plan/ws114 | integrated 3b985ae4c |
| P3-004 | q597 | 33e061c3b..be587f8ae（前回 597841d2a） | plan/ws115 | integrated 408a31586 |
| P3-005 | q600 | 503b94c80（前回 be587f8ae） | external.mk・packages/tools/gen-meson-cross.sh・devel/gperf・plan/ws115 | integrated 81f77738d |
| P3-006 | q602 途中 | 24c66c58e（前回 503b94c80） | packages/libs/pcre2・libffi・plan/ws115（proposed の libc 差分 2 つはユーザーの承認待ち） | integrated dea890041 |
| P3-007 | q602 wrap | c4e303ffa（前回 24c66c58e） | packages/libs/glib・plan/ws115 | integrated 698759738 |
| P3-008 | q602 | 150c2f51f（base 798ad97bc） | include/libc/libintl.h・sys/socket.h（ユーザー許可の libc 差分） | integrated 871777b34 |
| P3-009 | q602 | 31155f84d..958b6c060（前回 150c2f51f） | packages/libs/glib・plan/ws115 | integrated（cleared） |
| q605 / q605-i01 | ws115-p006 | libpng・freetype・harfbuzz・fontconfig | 継続 dispatch | 4h | finished / cleared |
| P3-010 | q605 | 19de99f8e..cbbc1607f（前回 958b6c060） | packages/libs/{libpng,freetype,harfbuzz,fontconfig}・plan/ws115 | integrated 3c33fb259 |
| q608 / q608-i01 | ws115-p007 | pixman・cairo・fribidi・pango | 継続 dispatch | 4h | finished / cleared |
| P3-011 | q608 | 53b5dbe6b..03f00921d（前回 cbbc1607f） | packages/libs/{pixman,fribidi,cairo,pango}・plan/ws115 | integrated db8f21553 |
| q610 / q610-i01 | ws115-p008 | gdk-pixbuf・jpeg・tiff・graphene・libepoxy・libxkbcommon | 継続 dispatch | 4h | finished / cleared |
| P3-012 | q610 | 8fe02c02b..581021c3e（前回 03f00921d） | packages/libs/{gdk-pixbuf,libjpeg-turbo,libtiff,graphene,libepoxy,libxkbcommon,xkeyboard-config}・plan/ws115 | integrated 8f4dcb417 |
| q613 / q613-i01 | ws115-p009 | libwayland-client ABI 互換・wayland-protocols | 継続 dispatch | 4h | finished / cleared |
| P3-013 | q613 | 7f6d5f4dd・d1fb4b2b1・ad993eb6a（前回 581021c3e） | libwayland・keiland/wayland の header・packages/desktop/wayland-protocols・plan/ws115 | integrated de9b0a4e2 |
| q614 / q614-i01 | ws115-p002 | GTK 4.18.6 本体 | 継続 dispatch | 4h | finished / cleared |
| P3-014 | q614 | b5abef788..34843813c（前回 ad993eb6a） | packages/desktop/gtk4・cairo・pango・wayland-util.h・plan/ws115 | integrated 41e878d09 |
| q615 / q615-i01 | ws115-p010 | gtk4-demo を zedBSD の QEMU で | 継続 dispatch | 4h | finished / uncleared（rtld の上限） |
| P3-015 | q615 wrap | 122aeb402（前回 34843813c） | plan/ws115 | integrated 03e9c9088 |

2026-10-03: ユーザーの判断で GTK は後回し、P3 generation2 は終了。再開は ws115-p010 の再開点（rtld の定数と `lookup_handle_graph` の bitmask の配列化、Q1 の許可済み）から。
| P3-016 | q628 | f6d7e391b..d502b1276 | plan/ws131（design.md・rename-map.md・phase003〜016） | integrated 769b46ac3（design-reviewer の review とユーザーのレビュー待ち） |

2026-10-03 / q632-i01（generation3）: host の memory の圧迫でラップアップ、uncleared。agent/p3 381b403b8..184600fd3 未統合。q632-i02（generation4）: user「N=1でP3のみを再開しましょう。」、ws131-p003 の「再開の手順」1〜6、4h、in-progress。

2026-10-03 / 5320 を FreeBSD に: user「Latitude 5320はP4で利用する実機ですが、P4は停止しており、P3を優先している状況ですので、FreeBSDのビルドやテストにSSH経由で利用してOKです。awe@10.0.30.3 です。」「5320にはFreeBSD 15.1がインストールされており、起動しています。」「ホストキーは更新してOKです。」→ Q1 が known_hosts の 10.0.30.3 を更新（ED25519 SHA256:SU3SAIyuzmOC97UBmW2veciwXDfHLA+C8AAKcZTiysE）、疎通を確認（FreeBSD 15.1-RELEASE、8 CPU、8 GB、cc・gmake・git・python3 あり）。P3 の WS131 p003 で FreeBSD の build と試験を再開（「書くだけ」を解除）。P4 の再開の時は P3 から 5320 を返す。

2026-10-13 夜 / q941（新しい世代）: user「N=2で、P1,P3を起動し、T1でテストする流れで、実行を再開してください。」→ [ws220-p003](../../ws220/phase003/phase.md) TCP の送りの未送信・retry の実装の再開。前の世代の書きかけは Q1 が agent/p3 398bbdebb（WIP）に救った（写し build/salvage-20261013/p3-1e0e3c3be.patch）。main を取り込み → §1 の設計どおり実装 → 模型・host 試験・build → Q1 に merge 依頼 → T1 の依頼は T1-574（基線）の後。State: active。
- q941 結果（2026-10-13 夜）: a82c9ba7b を敵対的 review（plan/ws220/phase003/review-1.md、重大 G1・中 M1・M2）→ 直し 37ed0dd97（EAGAIN は LOST で RTO・R2 へ、ACK の数は順序外だけ、timer の再送の拒みは 10 ms、tcp-stream-series.sh の停止の直し）→ main 282f46038。模型・amd64/rpi4 build warning 0。ws220-p003 は test-wait（T1-577、T1-574 の後）。

2026-10-13 夜 / q943: ws220-p004 RTT の推定と RTO（RFC 6298、最小 200 ms はユーザーの決定）。同じ P3 の続き。承認: ユーザーの再開の指示と WS220 のユーザーの決定（2026-10-13「はい、推奨でいいです。」）。p003 の出力は main 282f46038（p003 の clearance は T1-577 の後）。State: active。
- q943 結果（2026-10-13 夜）: ws220-p004 設計 a52a81575 → design review（plan/ws220/phase004/review-1.md、重大 G1・G2）→ 実装 9ef6fd540・直し 6bc7fc367 → main 51f753f27。Q1 の決めから 2 点（即時の再送で計りを捨てるのは計っている segment の時だけ、回復の即時の再送は timeout の時の送った端まで）を模型の根拠でずらした、Q1 が採用。UAPI は entry の reserved → rtt_samples（大きさ・版は不変）。test-wait（T1-579、T1-577 の後）。

2026-10-13 夜 / q945: ws220-p005 fast retransmit と NewReno の回復、persist timer、窓が開いた時の再送を外す（p001 §4 の p005 の行）。同じ P3 の続き。承認: ユーザーの再開の指示と WS220 のユーザーの決定。p004 の出力は main 51f753f27。State: active。
（2026-10-13 Q1: P3 が報告した pcat の link の失敗（drv_xhci_isoch_* の未定義）は platform/pcat/vmunix.mk に pci-xhci-isoch.o を足して直した、config/ci/config-pcat.mk の vmunix は warning 0。）
- q945 途中（2026-10-13 夜）: ws220-p005 設計 6923b94e6・実装 8086543a1（模型 lan-loss-0.1 4.63→36.54 MB/s、lan-loss-1 0.46→12.16）。design-reviewer の結果待ちで merge 保留。削除の依頼は済み。
- q945 結果（2026-10-13 夜）: ws220-p005 設計 6923b94e6 → design review（plan/ws220/phase005/review-1.md、重大 G1・G2）→ 直し 14d230247 → M7 の FreeBSD と同じ persist と TCP_MAXUNACKTIME（ユーザーの決定）29056974d → main e7ccb1a1c。libc の tcp.h の注記は Q1 が直した（3fb62183e）。test-wait（T1 の依頼は下の q947 の後にまとめる）。P3 の世代はここで終わり（context 約 83 万 token）。

2026-10-13 夜 / q947（P3 の新しい世代）: WS220 の T1-577 の損失 0 の退行の調べと直し（longest_stall_ms 2〜8 → 178〜226 ms、再送 0、down・down-disk 88〜89 %）。ユーザー「main に残し、直しを急ぐ」（2026-10-13）。State: active。
- q947 結果（2026-10-13 夜）: WS220 の T1-577 の遅さは環境（T1-581、同じ session で k1 < k2 < k3）、code の変更なし。記録 f3ef3fc22・ed94f056f → main 0d4ac1421。p003・p004 は Q1 が cleared、p005 は T1-585。
- 予約（2026-10-13 夜 Q1、ベータ2 の残りの実装）: q955 [WS156](../../ws156/ws.md) p006（Linux の D-Bus の Notifications）→ q956 [WS051](../../ws051/ws.md) p005b（抜けの停止）→ q957 [WS081](../../ws081/ws.md) の残り（L3 の計測の script と係数）→ q958 [WS033](../../ws033/ws.md) p001（USB LAN の後挿し）。
