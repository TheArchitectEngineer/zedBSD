<!-- awesome-plan project=zedbsd record=ws223-p001-review-1 -->

# ws223-p001 review-1（design-reviewer、第 1 版への敵対的レビュー）

対象: [phase.md](phase.md) 第 1 版（P3、commit 133ebbf64）と [ws.md](../ws.md)。照合した tree: `/home/awe/zedBSD-claude1` の main 1827d98b3（review 中に main は 21f99c56d へ進んだが、照合した path の差分は無い: `git diff 1827d98b3 HEAD --stat` で wayland・videoplayer・libkeiland・plan/ws223・plan/ws217・tests・plan/tools/aat に変更無し）。
`.internal/` と GPL の source は読んでいない。文書と source は直していない（この file だけを書いた）。

件数: 致命的 0、重大 2、中 6、軽微 10。

判定（p002 を始めてよいか）: **今の版のままでは不可。** 第 2 版で S1（WS217 の既存の host 試験を p002 の範囲に入れる）と M1（WS217 p002a の merge で古くなった事実の直し）を入れ、ユーザーの D1・D2 が決まれば p002 を始めてよい。S2（全画面の動画の上に cursor が残る）は新しい問い D7 として D1〜D6 と一緒にユーザーへ出し、**p002 を main へ統合するのは D7 の答えの後**にする（統合の時点で 5330・Venus の全画面の動画から cursor の自動の消えが無くなるため）。

---

## 重大

### S1. WS217 の既存の host 試験 `host-scanout-rules` を p002 が壊す。「game mode の host 試験は無い」は誤り

- 根拠: `plan/ws217/tests/host-scanout-rules.c:93-95` が `facts.content_type = KWL_SCANOUT_CONTENT_VIDEO;` と置き、`check(answer == KWL_SCANOUT_DIRECT, "a still fullscreen video goes straight")` を確かめている。`host-scanout-rules.sh:214-217` が `userland/desktop/wayland/scanout-rules.c` と一緒に compile する。この試験は WS217 p002a の commit `b58ff4b06`（2026-10-13 19:45）で main に入り、ws217-p002a は in-progress（T1 の結果待ち、`plan/ws217/phase002a/phase.md:6`、`:83` が PASS の証拠に挙げる）。phase.md の commit `133ebbf64`（19:50）は、この merge より前の tree を見て書かれたと読める。
- 起こる状況: p002 の §3.3 のとおり `KWL_SCANOUT_CONTENT_VIDEO` を消すと、`host-scanout-rules.c` は compile できなくなる（`-Werror`、未定義の識別子）。定数を残しても、規則を変えると `:95` の check が FAIL する。WS217 の回帰（p002b 以降で行を足す予定の表、`plan/ws217/phase001/phase.md:650`）が壊れたまま進む。p002 の受け入れ（build と AAT だけ）はこれを捕まえない。
- phase.md の誤り: §2.7「game mode の host 試験 **無い**（… WS217 §12.1 の『host-scanout-rules』は計画だけ）」、§4.1 の新しい `plan/tools/compositor/scanout-rules-host-test`、§5 の p003 の「host 試験はまだ無いので build と自分の review」。どれも main の今の状態と合わない。新しい試験を別に作ると、同じ `kwl_scanout_decide` の表の試験が 2 つになり、WS217 p012 の UNROTATE の行をどちらに足すかが割れる。
- 修正案:
  1. p002 の範囲に `plan/ws217/tests/host-scanout-rules.c` の直し（`:93-95` を「video は `KWL_SCANOUT_CONTENT`」に、`test_rotated` の video の行は無い）を入れ、p002 の受け入れに `sh plan/ws217/tests/host-scanout-rules.sh` の PASS（plain と ASan/UBSan）を足す。WS217 の path なので、Q1 経由で WS217 の担当（P4）と調整する（AGENTS.md の修正可能範囲）。
  2. p003 の「新しい host 試験」はやめ、同じ試験を回帰として `plan/tools/compositor/` へ移すこと（移すのは Q1 の判断、master の Tools に登録、WS217 p002a の phase.md:118 の「WS の完了時に Tools へ移す候補」と合わせる）と、photo・none・game・video の content の行を足すことに置き換える。WS217 p012 はその 1 つの表に UNROTATE を足す。
  3. ws.md の Phase の表の p003 の行と §5 の p002・p003 の受け入れを直す。

### S2. 合成の全画面の動画の上に pointer の cursor が残り続ける（game mode が唯一の「消える」経路だった）。「Game Mode でない全画面は既に実装済み」は、この点で成り立たない

- 根拠:
  - game mode に入ると `scanout_enter`（`scanout.c:280-309`）が `kwl_compose_output_close` を呼び、そこで `kwl_cursor_plane_close`（`compose.c:557`）、pass では `kwl_cursor_plane_hold`（`display.c:181-184`、`cursor-plane.c:363-366`「the plane is not presented then」）。直の scanout の間、画面には client の buffer だけが出て cursor は出ない。pointer が 2000 ms 止まると入る（`scanout-rules.c:64`）ので、今の Video Player の全画面（Venus で T1-244 の `direct=1`、i915 は BLOB あり）では「pointer を止めると約 2 秒で cursor が消える」動きになっていた。
  - 合成の経路には cursor を自動で消す規則が無い: `compose.c:3099-3101`・`:3163-3165`・`:3183-3185` が消すのは `cursor_hidden`（client が `wl_pointer.set_cursor` で surface 無しを頼んだ時、`seat.c:192`・`:1588`）と `pointer_unmoved`（起動から一度も動いていない時）だけ。
  - libkeiland には cursor を隠す API が無い（`userland/desktop/libkeiland/ui/*.c` に `wl_pointer_set_cursor` の呼びが無い。`keiland.h` に cursor の可視の API が無い）。Video Player も cursor を扱わない（`videoplayer/main.c` は bar を `VP_IDLE_MS` 等で隠すだけ、`:44-50`、`:1070-1090`）。
- 起こる状況: p002 の後、5330（i915）と QEMU Venus で Video Player を全画面にすると、bar（player の制御の帯）は消えても矢印の cursor が動画の上に残り続ける。動画の player として目に見える後退で、ユーザーの「Game Modeでない全画面を**実装します**」の「実装」の中身はここにある。Pi 4 は今も合成なので今日から同じ（後退ではないが、同じ欠け）。試験は捕まえない: AAT は log と撮影（撮影に cursor が写るかは未確認）、p004 は `late` と `KWL SCANOUT` の有無だけ。
- 修正案（新しい問い D7 として D1〜D6 と一緒にユーザーへ）:
  - (a) app の側: libkeiland に cursor を隠す API（例 `kl_window_set_cursor_hidden(window, hidden)`、中身は `wl_pointer.set_cursor(serial, NULL, 0, 0)`、KL_VERSION を上げる）を足し、Video Player は全画面で bar が消える時に隠し、pointer が動いたら戻す。Wayland の普通のやり方（mpv 等の player は自分で隠す）で、他の app の挙動を変えない。compositor は既に `cursor_hidden` を扱う。
  - (b) compositor の側: 全画面の窓の上で pointer が N 秒止まったら cursor を描かない規則（`KWL_SCANOUT_POINTER_IDLE_MS` に似た定数）。全ての全画面の窓（Terminal・Image Viewer・Notes）に効くので、ユーザーの決定が要る。
  - 推奨は (a)（範囲が Video Player と libkeiland の 1 API で閉じる）。どちらでも p002 か新しい段（p002b）に入れ、AAT に「pointer を止めて 3 秒後の撮影に cursor が無い」か log（例 `VIDEOPLAYER CURSOR hidden=1`）の check を足す。§3.4 の表（「cursor の plane … は普通の窓と同じ」）、§10 の R1 も直す。

---

## 中

### M1. WS217 p002a の merge（`b58ff4b06`）で、§2・§3.3・§4.1 の事実と行番号が古い

- 根拠（今の main）:
  - 規則の順は NO_WINDOW → NOT_FULLSCREEN → CONTENT → BUFFER → **ROTATED（`scanout_rotation`・`turn_rotation`、`scanout-rules.c:48-51`）** → SIZE → OVERLAY → SHOT → POINTER → REFUSED。`kwl_scanout_decide` は `:26-71`（phase は `:26-60`）。答えは 0〜10 の 11 個で、名前の表も 11 語（`scanout-rules.c:18-20`、`scanout-rules.h:115-125`）。`struct kwl_scanout_facts` に `scanout_rotation`・`turn_rotation` がある（`scanout-rules.h:137-149`）。
  - `scanout_note` は `scanout.c:379-399`、video の条件は `:388`（phase は `:374-393`・`:382`）。`scanout_facts` は `:158-203`、`scanout_enter` は `:280-309`（claim は `compose->mode_width/mode_height`、`:294`）。`kwl.h` の game mode の field は `:1531-1542`、content type の注記は `:764-770`、宣言は `:2086-2090`（phase は `:1516-1531`、`:763-765`）。`scanout-zedbsd.c` の BLOB の検査は `:286-289`（phase は `:296-300`）。
- 起こる状況: §2.1 の答えの順、§3.3 の「答えの番号（0〜9）と facts は不変（WS217 p002a が ROTATED 10 を足す前提）」、§4.1 の「`kwl_scanout_reason_name` の 10 個」は今の source と合わない。p002・p003 の担当がこの表のとおりに試験を書くと ROTATED の行を落とす。video の窓は回転した出力でも CONTENT で ROTATED の前に止まる（log 無し）、という WS217 との関係（§3.10）の根拠も ROTATED の位置に依る。
- 修正案: 第 2 版で §2.1・§2.7・§3.3・§3.4・§4.1・§10 を今の main（ROTATED 入り）で書き直し、行番号を取り直す。S1 と合わせて host 試験の表に ROTATED と content の組み合わせ（video+rotated → CONTENT、game+rotated → ROTATED）を入れる。

### M2. 「Pi 4 の経路は変わらない」は不正確。今の Pi 4（と Linux・FreeBSD）は、全画面の動画の窓ごとに出力を一度全部閉じて開き直している

- 根拠: `scanout_enter` は `kwl_compose_output_close(server)` と `server->windowed = 0` を**先に**行い（`scanout.c:289-291`）、その後で `kl_backend_scanout_open` を呼ぶ（`:294`）。bcm2711 は BLOB を立てない（`src/drivers/gpu/bcm2711/display-device.c:698`）ので `scanout_display` が `ENOTSUP`（`scanout-zedbsd.c:286-289`）、Linux・FreeBSD の backend は常に `ENOTSUP`（`libkeiland-backend/drm/display-drm.c:120-133`）。失敗すると `scanout_refused = top` で、次の pass の `enter_window_mode` が開き直す。`kwl_compose_output_close`（`compose.c:532-570`）は R・C の thread の park、cursor の plane、**全ての head（`kwl_heads_close_all`、2 つ目以降の display）**、layers・scene・backdrop・targets・swapchain を閉じる。refused は surface が去るまで残る（`scanout.c:150-152`）ので、窓 1 つにつき 1 回。
- 起こる状況: (1) 今の Pi 4 で Video Player を全画面にして pointer を 2 秒止めると、出力全体の閉じ・開き直し（`reason=backend errno=ENOTSUP` の行と一度の止まり、外の display も一度消える）が起きている。p002 の後は video では起きない。つまり Pi 4 でもこの WS で挙動は変わる（改善）。p004 の Pi 4 の計測は「前後で同じ経路」の前提で書かれている。(2) game の client は p002 の後も Pi 4・Linux・FreeBSD で毎回これを払う。p003 の `desktop.game-mode.wltest` と p004 は `reason=backend` を合格の 1 つとして数え、この費用を見ない。(3) 2 画面の i915・Venus では、game mode の間 head（外の display）が閉じたままになる（`kwl_heads_close_all`）。今までは全画面の動画でも外の画面が消えていたはずで、この WS で video には起きなくなり、game には残る。phase は heads に触れていない。
- 修正案: 
  - §2.1・§3.5・§3.9・§0 の 4 と D4 の文を「今の Pi 4 は窓ごとに一度、出力を閉じて開き直してから合成に落ちる。p002 の後 video ではこれが無くなる」に直す。p004 の Pi 4 は「全画面にして 3 秒止めても `KWL SCANOUT` の行が無い、`KWL OUTPUT open` が増えない」を見る。
  - compositor の中だけで閉じる直し（delegated の技術判断の範囲）を p002 に入れるか Future Work に置くかを決める: 例 `kl_backend_scanout_open` が `ENOTSUP` を返したら `server->scanout_unsupported = 1` を立て、以後は全ての窓で `scanout_enter` を呼ばない（hotplug・output switch で消す）。backend の interface を変えずに、Pi 4・Linux・FreeBSD の game の client の不要な閉じ・開き直しを 1 回に減らせる。順序を「backend に問うてから閉じる」に変えるのは backend の API の追加なので別の段。
  - game mode で head が閉じることを §3.8 の WS113 の行と D1 の欠点に書き、Future Work（game mode の間も head を保つ、または 2 画面では game mode に入らない）に置く。

### M3. D4 は ws.md の完了の条件 3 を緩める決定であり、そう書いていない。Pi 4 の「滑らか」の行き先が無い

- 根拠: ws.md:20「全画面の再生が合成で滑らかに動く（5330・Pi 4 で確かめる）」。phase §3.9 は自分で「(3) だけで 1080p は 20 fps 未満、A4 の前には滑らかにならない」と見積もり、D4 で「計測の記録だけで受け入れる」とする。Video Player は毎 frame CPU で拡大縮小・BGRA 変換する（`videoplayer/main.c:910` `app_frame_scale`）。これは Guardrail「画像の処理は GPU で」（2026-10-12、`plan/guardrail.md` の同名の節: desktop の app は毎 frame の経路で画素を CPU で触らない、decode は除く）に当たる。
- 起こる状況: D4 を「記録だけ」と答えても ws.md の条件 3 はそのまま残り、WS223 は Pi 4 で完了できない（または黙って条件を外すことになる、Awesome Plan の「deferral cannot silently remove WS acceptance」）。Pi 4 の滑らかさを誰が取るか（WS141 p015 A4、Video Player の拡大縮小を GPU へ移す WS）が記録されない。
- 修正案: D4 を「ws.md の条件 3 の Pi 4 の部分を『合成の経路になり game mode の閉じ開きが無いこと、数字の記録』に変え、『滑らか』は WS141 p015 A4 と Video Player の拡大縮小の GPU 化（担当の WS を名指す。WS202 か新しい WS、Q1 が決める）へ移す」という受け入れの変更の問いとして書き直す。答えを Q1 が ws.md に反映し、移し先に reciprocal の link を置く。Guardrail の CPU の拡大縮小は既存の違反として行き先（WS と Phase）を書く。

### M4. Video Player の AAT の否定の check の設計が弱い（偽の PASS、時間の余裕）

- 根拠: §4.2 は scenario の文で「F11 の後 3 秒待っても `ZWL SCANOUT` の行は出ない」、helper の文で「3 秒の間に `KWL SCANOUT direct=1` が無い」と、見る行が食い違う。今の helper は `run.wait(..., mark, 8)`（`helpers_apps.py:688`）。規則は pointer の 2000 ms の静止の後（`scanout-rules.c:64`）で、入る時の claim は Venus で `switch_ms=678`（T1-244）、`direct=1` の行は claim の後に出る（`scanout.c:307`）。
- 起こる状況: (1) 「`direct=1` が無い」だけを見ると、規則が video を誤って通し `direct=0 reason=backend|refused|pointer` を出す退行を見逃す（video の窓は p002 の後、どの `KWL SCANOUT` の行も出さないのが正しい）。(2) 3 秒は「2 秒の静止 + claim」に近く、AAT の click から F11 までの 0.5 秒の扱い次第で退行の行が窓の外に落ちる。(3) 否定の check は、compositor の log が取れていない時にも PASS する。
- 修正案: helper は F11 の後、pointer を動かさずに 5 秒以上（例 6 秒）待ち、`mark` 以降に `KWL SCANOUT` で始まる行が 1 つも無いことを check する。同じ step で正の錨（`KWL CONFIGURE … fullscreen=1`、`VIDEOPLAYER PRESENTED width=<出力> height=<出力>`、session の始めの `KWL CONTENT surface=N type=video`）が出たことも check し、無ければ fail にする。scenario の文と helper の文を一致させる。

### M5. `desktop.game-mode.wltest` の手順が wltest の実際と合わない

- 根拠:
  - wltest は既定で全画面で開く（`userland/tests/wltest/main.c:94` が `!options.windowed` を渡し、`window.c:140` が `xdg_toplevel_set_fullscreen`）。`--fullscreen-at=1` は冗長で、`not-fullscreen` → fullscreen の遷移を見るなら `--windowed --fullscreen-at=N` が要る。
  - wltest は keyboard を bind しない（`window.c`・`main.c` に `wl_seat` が無い、`window.c:260-300` は `wl_compositor` と `xdg_wm_base` だけ）。compositor が全画面から戻す key は F11 と Super+Down だけ（`shell.c:276-286`）。§4.2 の「Esc か `--unfullscreen-at` で `direct=0 reason=not-fullscreen`」の Esc は何も起こさない。
  - wltest は `--frames` 枚を描いて終わる（最大 3600、既定の delay 16 ms、`main.c:82`・`:115`・`:240-246`）。合成の Venus は 1 frame 約 100 ms（§3.9 の F-021）なので、600 枚の時間は game mode に入るか否かで数秒〜1 分と大きく変わる。shot・pointer・2 秒の待ち・unfullscreen・close の並びが frame の予算の中で終わる保証が無い。
  - wltest の README（`userland/tests/wltest/README.md`）は「標準の `wl_compositor` と `xdg-shell` だけ」「他の OS では xdg-shell の生成物と一緒に compile」と書く。content-type の protocol の生成物も要るようになる。
  - `config/ci/config-rpi4.mk:48` の `ZEDBSD_USER_PROGRAMS` に wltest が無い（videoplayer はある）。§4.3 の Pi 4 の `wltest --content-type=game` には Pi 4 の image の構成（AGENTS.md の試験の image の規則: `plan/ws223/tests/` の config.mk）が要る。5330 の image も同じ。
  - 新しい scenario の frontmatter の `paths` が書かれていない。§4.2 は Video Player の scenario から scanout.c・scanout-zedbsd.c を外すので、`select-scenarios.py` の `changed:RANGE`（`plan/tools/aat/select-scenarios.py:61-84`）で scanout の変更に当たる scenario が無くなる。
- 修正案: 手順を `wltest --windowed --content-type=game --fullscreen-at=N`（または既定の全画面のまま）、戻しは F11 か `--unfullscreen-at=M` に直す。frame の予算に依らない形（例 `--frames=3600` と AAT の側で窓を閉じる、または「`--hold` で close まで描き続ける」option を wltest に足す）にする。README と `Makefile.linux`・`Makefile.freebsd` の生成物を p003 に入れる。新しい scenario の `paths` に `scanout.c`・`scanout-rules.c`・`content-type.c`・`scanout-zedbsd.c` を入れる。p004 の image の config.mk（5330・Pi 4、wltest 入り）を `plan/ws223/tests/` に置く段を書く。

### M6. p002・p003 が触る file の持ち主が書かれていない

- 根拠: §4.2・§5 が変える `plan/tools/aat/scenarios/helpers_apps.py`、`plan/tools/aat/config-amd64-aat.mk`、`tests/suites/full.suite`、S1 の `plan/ws217/tests/`。AGENTS.md「subagent の修正可能範囲」は割り当ての WS の source と `plan/wsNNN/` だけ。
- 起こる状況: 担当がこれらを直に変えるか、変えずに受け入れの試験が FAIL するかが Phase の実行中に割れる。
- 修正案: §5 の各段に「Q1 に依頼する file」と「担当が変える file」を分けて書く（plan/tools の 2 つは Q1 の許可、`plan/ws217/tests/` は Q1 経由で WS217 の担当）。

---

## 軽微

| # | 所 | 指摘 | 直し方 |
| --- | --- | --- | --- |
| L1 | §0 の 1、§2.3 の `shell.c:277-290` の行、§3.6 | 「Esc・F11 の戻り（BUG-194）は compositor が持つ」は誤り。compositor の key は F11 と Super+Down（`shell.c:276-286`）、Esc は Video Player 自身の key | 「F11・Super+Down（compositor、BUG-194）と Esc（player）」に |
| L2 | §2.5、§3.6、§4.2、`tests/scenarios/apps/videoplayer/fullscreen.md` の step 1 | log の prefix が `ZWL`。今の code は `KWL`（`scanout.c:131,299,307,398`、`content-type.c:117`）。helper は `KWL` で待っている | 新しい文は `KWL` で書き、scenario の直しで `ZWL` も `KWL` に |
| L3 | §3.3 の注記の表 | 「video or game」の注記の漏れ: `scanout-rules.c:9-10`・`:36`、`kwl.h:2086`、`libwayland/content-type-protocol.c:12`、`libkeiland-backend/drm/display-drm.c:116-117`。また `libkeiland/ui/window.c:893-895` は KL_VERSION 41、`keiland.h:3974` は 42（既存の食い違い） | 表に足す。window.c の版を 42 に揃える |
| L4 | D3 と p004 | D3 は「実機の direct=1 の確認は不要」、p004 は「wltest の game の direct=1（BUG-223 の i915 の証拠）」。BUG-223 を resolved にするなら、5330 の direct=1 は game mode の code の健全性の確認で、BUG-223 の証拠ではない | p004 の文を「game mode の i915 での確認（BUG-223 とは別）」に。BUG-223 の resolved の証拠は T1-237（F11・Alt+Enter の全画面）とユーザーの決定 |
| L5 | §2.2 の `keiland.h:3972-3984` の行 | 「KL_VERSION 42」は API が入った版。今の KL_VERSION は 92（`keiland.h:48`） | 「KL_VERSION 42 で入った」と書く |
| L6 | §3.6 の D2 の理由 (2) | 「AAT の `ZWL CONTENT … type=video` の行が残る」は理由として弱い。今の helper は CONTENT の行を見ていない（`helpers_apps.py` に `CONTENT` が無い） | M4 で正の錨として使うなら理由になる。使わないなら削る |
| L7 | ws.md:18 の完了の条件 1 | 「全画面の窓は compositor が合成して出す（直の scanout をしない）」は game の窓を除いていない。条件 2 と字面が矛盾する | 「`game` を求めない全画面の窓は」に（Q1） |
| L8 | §4.3 の 5330 の受け入れ | 「late が window の時と同じ桁」は判定の基準として曖昧 | 同じ run で window と全画面の `FRAMES shown= late=` を取り、全画面の `late/shown` が window の値 + 1% 以下、のように数で |
| L9 | §3.9 の 5330 | game mode をやめると 5330 では全画面の動画の毎 frame に合成の GPU の pass が増える（電池）。phase は遅れと switch だけを比べている | p004 に任意の記録（30 秒の再生の GPU の時間か電池の残量の差）を足すか、影響を推測として書く |
| L10 | §2.4 の package の行 | 「grep 0 件」の範囲（tree の patch だけか tarball の中もか）が書かれていない | gtk-4.18.6 の tarball の file の一覧に content-type の protocol の file が無いことは review で確かめた（下）。範囲を書く |

---

## 決めてほしい点 D1〜D6 の妥当性

| ID | 評価 |
| --- | --- |
| D1 (a) `wp_content_type_v1` の `game` | 妥当。今の実装・WS217 p012 の定義と合い、tree に `game` を付ける client は無い（下の確認）。ただしユーザーに次を明示してから問う: (1) `game` は protocol の上では hint で、どの client も付けられ、付けた全画面の窓は条件が揃えば自動で入る（ユーザーの「要求があったときのみ」を「hint を付けた時」と読む解釈）。(2) 入ると cursor が消え（S2）、入る時に出力の閉じ開きがあり（678 ms、M2）、2 画面では外の display が消える（M2 の 3）、BLOB の無い Pi 4・Linux・FreeBSD では閉じ開きだけ払う。SDL3 が全ての窓に `game` を付けるという §3.2 の記述は推測（tree に無く未確認）なので、推測と明記したまま (a) の欠点に入れる |
| D2 残す | 妥当（`video` に依る compositor の規則は p002 の後どこにも無い: `KWL_SCANOUT_CONTENT_VIDEO` の使用は `scanout-rules.c:37`・`scanout.c:388`・WS217 の試験だけ）。理由 (2) は L6 |
| D3 resolved | 妥当。L4 の食い違いを直す |
| D4 記録だけ | 不十分。M3 のとおり「完了の条件の変更」として問い直す |
| D5 Future Work | 妥当（WS214 の plane の条件との関係は未確認、下） |
| D6 guardrail の文 | 妥当。`plan/tools/gpu-boundary/v1-check.sh:11-14` の注記は変更後も正しい |
| 足りない問い | D7（S2: cursor をどう隠すか）。M2 の ENOTSUP の latch は技術の判断で、ユーザーの問いにしなくてよい |

---

## 段の分け方と依存

- p002 の受け入れに S1 の host 試験の PASS を足す。p003 から「新しい host 試験」を外し、Tools への移しと行の追加にする。
- p003 の wltest の `--content-type` は p002 に依存しない（並行してよい）。依存するのは video の対照の段だけ。
- p004 の 5330 の Video Player は p002（と D7 の段）だけに依存し、wltest の game は p003 に依存する。
- WS217 p012 と WS217 §12.2 の 7・9 は「ws223-p002・ws223-p003」に依存（WS217 にも p002・p003 があるので ID は WS 付きで書く）。
- D7 の答えで段（例 p002b: libkeiland の API と Video Player）を足すなら、p004 はその後。

---

## 確かめた点

- `scanout-rules.c:37` の条件（video か game）が content の唯一の入り口。同じ条件の写しが log の `scanout_note`（`scanout.c:388`）にある。content type は `content-type.c:115` でだけ書かれ、`wp_content_type_v1` だけから来る（`protocol.c:83` の global 28）。
- `kl_window_set_content_type` を呼ぶのは `videoplayer/main.c:240`（`KL_CONTENT_VIDEO`）だけ（userland・tests・plan/tools の grep）。`KL_CONTENT_GAME` を使う app は無い。`userland/packages/` に Wayland の client は GTK4 だけで、`build/distfiles/gtk-4.18.6.tar.xz` の file の一覧に content-type の protocol の file は無い。SDL・mpv は tree に無い。
- bcm2711 の QUERY は BLOB を立てない（`display-device.c:698`）。BLOB を立てるのは i915（`display/display.c:2750`・`:3360`、`capture.c:576`）と venus（`display.c:791`）。backend は BLOB が無いと `ENOTSUP`（`scanout-zedbsd.c:286-289`）。Pi 4 は今も game mode に入らない（ただし M2）。
- screen off の抑止（`sleep.c:401-405`）は fullscreen だけを見て content type に依らない。
- `fullscreen_whole`（`shell.c:723-735`、`:1556-1600`）の条件と、import の既定が opaque（`import.c:61-66`）。
- bar の overlay の条件（`scanout.c:252-253`）、shot（`:200`）、他の overlay（`:212-272`）は video を外した後の合成の経路では使われないだけで、他の規則への副作用は無い。
- log に依る試験: `KWL SCANOUT` を見るのは `helpers_apps.py:686-703` だけ。tests/scenarios・plan/tools の他の file、T1 の未実行の依頼に game mode の物は無い（T1-235・237・244 は済み）。
- `docs/` に game mode・content type の記述は無い。
- WS217 の `host-scanout-rules.{c,sh}` の中身と状態（S1）、p002a の phase.md の状態。
- `kwl_compose_output_close` が閉じる物（`compose.c:532-570`）。

## 確かめていない点（推測を含む）

- AAT の `aat shot` の撮影に cursor が写るか（S2 を撮影で捕まえられるか）。
- 5330 の i915 で game mode が実際に `direct=1` になるか（BUG-223 の残り、phase と同じく未確認）。S2 の「5330 での後退」は Venus の T1-244 と code からの推測。
- SDL3・mpv の content type の付け方（phase と同じく推測。source は読んでいない）。
- §3.9 の Pi 4 の各段の ms と「raster→UIF の CPU 変換 約 50 ms」（WS141 p015 の記述は読んだが、client の LINEAR の canvas の変換の数字は確かめていない）。
- Video Player の窓に libkeiland の glass の panel や sub-surface が付き `fullscreen_whole` が偽になるか（`style.glass = 0`、`main.c:245` から偽にならない見込み、phase と同じ）。
- WS214 p001 §5.5 の plane の条件（D5 の前提）。
- `ZEDBSD_USERLAND_PACKAGE` の 3 番目の引数 `amd64` の意味（wltest・videoplayer・wayland が同じ値で、wayland は rpi4 でも build されるので arch の制限ではないと推測）。
