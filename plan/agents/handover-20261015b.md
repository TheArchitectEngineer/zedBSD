# 2026-10-15（夕）の Q1 の引き継ぎ

ユーザー:「ハーネスが壊れてきたようです。記録の整合性が壊れる異常があったため、しかたないです。一度ラップアップしましょう。」「Q1を（自分自身を）ラップアップしてください。」

新しい Q1 は AGENTS.md → この file → plan/master.md の先頭の block の順に読む。前の引き継ぎ [handover-20261015.md](handover-20261015.md) の「次にやる事」は、下の「残っている仕事」に引き継いだ。

## 最初に: この session で起きた異常

- Q1 は WS245 で、merge の失敗（「not something we can merge」）と試験の不在（「No such file」）を成功として報告し、記録した。対象は、実在しない design.md の Phase の p002〜p005 の cleared・D1〜D6・試験の件数。誤りは 485ad5be6 で取り消し、報告の page はユーザーの指示で削除した。queue.md の q992〜q995 は成果なしの uncleared として記録してある。
- **再発防止（memory の verify-merge-before-reporting）**: merge_one の後は毎回 `git log -1 --format='%h %p'` で merge の commit を確かめる。担当の報告の SHA は `git cat-file -t` で実在を確かめてから記録する。報告と出力が食い違ったら、説明で片付けず確かめ直す。
- 点検（2026-10-15 夕、Q1）: plan/ws245・ws247・ws242 と master の先頭の block に書いた commit は全部実在する（8ec351aeb・06f9e2830 は NVIDIA open-gpu-kernel-modules・Mesa の上流の commit で正常）。main の作業ツリーは clean、merge の途中ではない。
- 日付: host の時計は 2026-10-15（木）JST。10-15 04:36 以降の記録が 1 日進んだ 2026-10-16 と書かれていたので、ユーザーの指示で出来事の日付を 2026-10-15 に直した（予定の記述の 7 行、例: ベータ2 の期限「2026-10-16 夕」は残した）。

## 動いていた担当（この session の終わりで未完、結果は Q1 に届いていない）

| 担当 | 仕事 | worktree / branch | 基点 | 成果の置き場所 |
| --- | --- | --- | --- | --- |
| P1（kernel-and-driver-designer） | ws245-p001 の design.md の改訂: ユーザーの決定 1〜4 と design-review-1.md の指摘を反映（q991 の続き） | /home/awe/zedBSD-worktrees/p1、agent/p1-ws245 | 5a5a849be | plan/ws245/design.md を `git commit -m WIP` |
| design-reviewer | design.md（6bccaef57 の版）の 2 回目の敵対的レビュー | /home/awe/zedBSD-worktrees/p2、agent/p2-ws245 | 1d8cc4d19 | plan/ws245/design-review.md を `git commit -m WIP` |

14:46 の時点では、どちらの worktree も変更なし（作業中）。新しい Q1 は:
1. `git -C <worktree> log main..HEAD` と `git -C <worktree> status` を見る。commit があれば `git cat-file -t` で確かめてから merge する。
2. commit が無く未 commit の差分だけがあるなら、中身を読んで判断する（途中なら担当を新しい世代で再開）。何も無ければ作業は失われたので、同じ依頼で起こし直す（依頼の要点は下の WS245 の節）。
3. 終わった担当に SendMessage しない（memory の no-message-to-finished-agent）。

p3 の worktree（agent/p3-ws245、9b860c6d0）は使っていない。担当の build の出力（p1・p2・p3 の build/ws247-k）は Q1 が plan/tools/q1-clean.sh で消してよい（未実施）。

## この session の成果（確かめ済み）

### WS247 nvrtx の出典表記の整理（[ws.md](../ws247/ws.md)、incomplete）

- p001（q984〜q986、P1〜P3 の並列）・p003（q987〜q989、compiler の sm75.c・sm75.h）・p002（q990、規約の全文の見直し）は cleared、main に merge 済み。
- Q1 の確かめ直し（c0d34a981）: 352b53fee と main 1d8cc4d19 の間の nvrtx の 61 file を `cpp -fpreprocessed -P` で比べ、59 file は comment だけ。差は runtime.c・vk-node.c の log の段の名前の 5 行（D2 → renderer、D3 → executor、意図した改名）。build rc 0・warning 0、host 試験 sm75 287/0・desktop 138 compiled/0 disagreed・vk-d3 122/0・vk 60/0。
- **completed にする前のユーザーの判断 2 点**:
  1. 出典の 2 つ目以降の path の省略（約 73 か所）を認めるか、完全な path に直すか。
  2. sm75.c の「Value referenced: the SM75 code the CUDA compiler emits」（3 か所）。ユーザーは「In/Out だけを見たので問題なさそう」と考えている（未決）。Q1 の勧め: CUDA の EULA に依らないよう、NAK などの公開の source か RTX 2070 の実機の測定で裏付け、出典を書き換える。経緯（誰がどの道具で値を得たか）は plan/ws037 の記録で確かめる（未実施）。
- 範囲外で見つけた code の疑い: scanout.c が定義の無い番地 0x64013c を読んで捨てている（SOR の caps は 0x640144＋or*8）。WS037 の後続で（[phase001](../ws247/phase001/phase.md) の「出典の無い値」）。
- p002 で直さずに残した点（phase002 の「直さずに残した点」）: log の冗長（`devinit begin: devinit` など）、identify.c の path の無い出典、style-check.py の 716 件（大半は範囲外の code）。

### ユーザーの決定（この session）

- nvrtx の compiler の Collabora の著作権表示: 削除でよい（178e969d0 で削除済みを確かめた）。
- WS242: GTX 16・RTX 30・40 を既定で有効にする（[ws242/ws.md](../ws242/ws.md) に記録、実装は未着手）。
- WS245: 下の決定 1〜4。報告の page の削除。

### WS245 i915 Gen12（5330）display の書き直し（[ws.md](../ws245/ws.md)、planning）

- p001 の [design.md](../ws245/design.md)（P2、6bccaef57、main の merge 4625f0a3e）: 接頭辞 `xd`、Phase p002〜p012、36 LW。**敵対的レビューはまだ通っていない。**
- レビュー 1（[design-review-1.md](../ws245/design-review-1.md)、079d8080a、P2 の子の design-reviewer の報告を Q1 が要約）: blocker 6（B1 は記録の取り消しで解決済み）・major 19・minor 14。Q1 が元の資料で確かめたのは B2（rawclk 19200 kHz）・B6（PWR_WELL_CTL2 0x00000c03）・M1（crawl yes・squash no）・M8（tc-kern.c が実在）だけ。
- ユーザーの決定（5a5a849be、ws.md）: (1) 口 xd.h を p002 で決め、実装を旧の薄い adapter と xd でまるごと切り替える、(2) 対象は 5330 だけ（Tiger Lake は支えない）、(3) 手順は Linux の code を参考にする（code・comment・構造は写さず、block ごとに Value(s)/Procedure referenced:）、(4) PRM と Linux が食い違えば Linux を優先。
- 次: P1 の改訂と 2 回目のレビューを merge → レビュー 2 の指摘も反映 → もう一度レビュー → design.md 第 7 節の残りの判断をユーザーに → p002 から実装（Queue の承認が要る）。

## 残っている仕事（優先の順は新しい Q1 がユーザーに確かめる）

1. WS245 の上の続き。
2. WS247 の判断 2 点 → completed。
3. WS242: GTX 16・RTX 30・40 の既定の有効化の実装。plan/ws242/phase001 の再開点 1〜4（host 試験の追従を含む）。
4. 前の引き継ぎの残り: WS241（RTX の描画の速さ）の次の round を P1 に（boost を既定に、Terminal+Files の rec submit、streams の計数）。WS244（Gen9 の display を guardrail に沿って直す、P7 の棚卸しの続き）。
5. Master の open-decisions の既存の項目（WS037 の disp.own の既定、boot loader の GOP の mode、WS204・WS205 など）。

## 記録の場所

- Queue Board: plan/queue.md の「2026-10-15: WS247・WS245（q984〜q995）」。次の番号: BUG-323、WS248、q996、T1-698。
- 担当の台帳 plan/agents/registry.md はこの session で更新していない。
