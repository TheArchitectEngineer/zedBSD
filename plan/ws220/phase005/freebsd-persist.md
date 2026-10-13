# FreeBSD の窓 0 の相手の扱い（2026-10-13、Q1 が起動した調べ、ws220-p005 review-1 M7 の材料）

FreeBSD stable/15（61afe54265dc、2026-09-21）の source を読んだ。行番号はこの版。既定の stack は base の "freebsd"（tcp_subr.c:365）。Linux は tcp(7) と RFC だけ（source は読まない）。【確】は source・man・RFC、【推】は推測。

## 1. persist timer
- 定数【確】（tcp_timer.h）: TCPTV_PERSMIN 5000 ms（l.82）、TCPTV_PERSMAX 60000 ms（l.83）、sysctl net.inet.tcp.persmin・persmax（ms、tcp_timer.c:76-84、初期値 tcp_subr.c:1492-1493）。TCP_MAXRXTSHIFT 12（l.117）。tcp_backoff[13] = {1,2,4,8,16,32,64,128,256,512,512,512,512}、tcp_totbackoff 2559（tcp_timer.c:255-258）。net.inet.tcp.retries（V_tcp_retries）既定 12（1〜12、tcp_timer.c:201-220）。
- tcp_setpersist（tcp_output.c:1744）【確】: 間隔 = TCPT_RANGESET(tt, t × tcp_backoff[t_rxtshift], persmin, persmax)、t = ((t_srtt>>2)+t_rttvar)>>1（RTO 程度）。TT_PERSIST を起動し t_rxtshift < V_tcp_retries なら t_rxtshift++（l.1772-1773）。persist は再送と同じ t_rxtshift を数えに使い上限 12。【推】LAN では clamp が効き約 5 s から 60 s で頭打ち。
- tcp_timer_persist（tcp_timer.c:486-545）【確】: 諦めの条件（2）→ close 後の諦めの条件 → tcp_setpersist → TF_FORCEDATA で tcp_output が 1 byte を強制送信。
- persist に入る時 t_rxtshift = 0【確】: tcp_output.c:501（窓 0 で未送信）・749（REXMT も PERSIST も無く未送信）・1617（長さ 0 の送信の後）。
- 抜ける時も 0【確】: tcp_output.c:392-393（TF_FORCEDATA で sendwin != 0 なら PERSIST を止め t_rxtshift = 0）、1590-1592（data を送り REXMT へ移る時 PERSIST を止め 0）、tcp_input.c:3700 の tcp_xmit_timer（RTT の標本）でも 0。

## 2. 窓 0 の相手を諦める条件
- 【確】tcp_timer.c:512-521: progdrop = tcp_maxunacktime_check(tp); if (progdrop || (t_rxtshift >= V_tcp_retries && (ticks - t_rcvtime >= tcp_maxpersistidle || ticks - t_rcvtime >= TCP_REXMTVAL(tp) × tcp_totbackoff))) → drop。
- tcp_maxpersistidle【確】: tcp_timer.c:158 の大域変数、sysctl 無し、tcp_subr.c:1488 で TCPTV_KEEP_IDLE（2 時間）に固定。persmax は probe の最大間隔で idle の上限ではない。
- close 後【確】（l.525-532）: t_state > TCPS_CLOSE_WAIT かつ ticks - t_rcvtime >= TCPTV_PERSMAX（60 s）で drop。
- 鍵は t_rcvtime【確】: tcp_do_segment（tcp_input.c:1576）が受けた segment ごとに t_rcvtime = ticks。probe に ACK で答え続ける相手は idle にならず、通常の条件でも close 後の条件でも永遠に落ちない。
- 答え続ける相手を落とす唯一の手段は TCP_MAXUNACKTIME【確】: setsockopt で per-socket（秒）か sysctl net.inet.tcp.maxunacktime（ms）。既定 0（無効、TCPTV_MAXUNACKTIME 0、tcp_timer.h:89、tcp(4)「default zero = no progress checks」）。t_acktime は data が入った時か acked > 1 の ACK で更新（tcp_input.c:2963-2966）、全部 ACK で 0。設定してあれば最後に進んでからその時間で RST。
- 答えない相手の既定の時間【確＋推】: 12 回の probe（5〜60 s）の後 idle ≥ min(2 h, REXMTVAL × 2559) で落ちる。【推】LAN（t_rttmin 30 ms）で 2559 × 30 ms ≈ 77 s、RTO 1 s で ≈ 43 分、上限 2 h。
- RFC【確】: RFC 6429 は RFC 1122 §4.2.2.17（答える相手との接続を閉じてはならない）の解釈を明確にし、OS・アプリの資源管理として persist 中の接続を abort してよく、しなければ DoS になると明記。FreeBSD base は既定ではこの資源管理を持たない。

## 3. 資源
- 送りの buffer【確】: net.inet.tcp.sendspace 既定 32 KB（tcp_output.c:111、tcp_usrreq.c:162 の soreserve）。自動拡大（sendbuf_auto=1・sendbuf_inc=8K・sendbuf_max=8 MB、tcp_output.c:115-129）は snd_wnd/4*5 >= hiwat などが条件（tcp_sndbuf_autoscale l.2167-）で窓 0 では伸びない【推】、窓 0 の相手が握れるのはほぼ 32 KB。sb_mbmax = min(cc × sb_efficiency(8), sb_max)（uipc_sockbuf.c:668）。kern.ipc.maxsockbuf = SB_MAX 8 MB（sys/sockbuf.h:65、uipc_sockbuf.c:1886）。uid ごとの RLIMIT_SBSIZE（chgsbsize、l.662、既定無制限）。
- host 全体【確】（kern_mbuf.c:153-191）: maxmbufmem = realmem/2（上限 3/4）、nmbclusters = maxmbufmem / 2048 / 4。
- mbuf が尽きた時【確】: sosend は m_uiotombuf(..., M_WAITOK)（uipc_socket.c:2260・2491・2502）で寝る。tcp_output の ENOBUFS は cwnd を 1 MSS にして 0（tcp_output.c:1690-1693）。tcp_drain（tcp_subr.c:1397）は vm_lowmem・mbuf_lowmem に登録（l.1523-1524）されるが reassembly queue の flush と SACK report の掃除だけで、送りの buffer を返させることも接続を落とすこともしない。
- Sockstress への専用の対策は base に無い【確（不在）】。手段は opt-in の TCP_MAXUNACKTIME、uid の RLIMIT_SBSIZE、nmbclusters、firewall・application（【推】）。

## 4. Linux（tcp(7) と公開の文書のみ）
- 【確・man】tcp_retries2 = 15（established の再送の上限、13〜30 分）。tcp_orphan_retries = 8（自分が close した接続の相手を probe する回数の上限）。TCP_USER_TIMEOUT（ms、2.6.37 以降）: 送った data が未 ACK のまま、または窓 0 のため buffer の data が未送信のままこの時間を過ぎると強制的に閉じ ETIMEDOUT。
- 【推】答え続ける相手への zero-window probe は tcp_retries2 では切れず無期限が一般の理解。orphan では tcp_orphan_retries が probe の回数を絞る。

## zedBSD への示唆
- 今の弱さ【推】: pool 256 個、1 接続の上限 8 個で、窓 0 の相手が 32 接続あれば送りの pool が全部埋まる。1 接続の占有（16 KB）は FreeBSD の 32 KB より小さいが、pool が共有で小さいので全体を塞がれる危険は大きい。
- FreeBSD と同じにするなら: (1) persist を再送と分け、間隔 clamp(RTO × backoff[shift], 5 s, 60 s)、backoff {1,2,4,…,512,512,512,512}、shift 上限 12、persist に入る時と抜ける時（窓が開いた・再送へ移った）に shift = 0。(2) 答えない相手: shift ≥ 12 かつ now − last_rcv ≥ min(2 h, RTO × 2559) で drop、last_rcv は受けた segment ごとに更新。(3) close 後の persist: state が CLOSE_WAIT より後（FIN_WAIT_1・CLOSING・LAST_ACK）で now − last_rcv ≥ 60 s なら drop（答える相手には効かない）。(4) per-socket の TCP_MAXUNACKTIME（秒）と system の既定値（既定 0 = 無効）、t_acktime は data を積んだ時と 2 byte 以上の ACK で更新し全部 ACK で 0、期限で RST。
- FreeBSD を超える案（判断が要る）: (a) maxunacktime の system の既定を 0 以外、(b) pool が逼迫した時に persist 中で進みの最も古い接続から RST。1 接続の上限を pool の空きに応じて下げる、close 後の persist を tcp_orphan_retries 型（N 回で落とす）にする、も検討。

## ユーザーの決定（2026-10-13、AskUserQuestion）

「FreeBSD と同じ＋p006 で pool を切り離す (Recommended)」: p005 に (1)〜(4) を FreeBSD と同じ形で入れる（TCP_MAXUNACKTIME の既定は無効）。共有の pool を握られる問題は p006 の送りの buffer の切り離しで解く。(a)・(b) は入れない。
