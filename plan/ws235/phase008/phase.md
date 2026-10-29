<!-- awesome-plan project=zedbsd record=ws235-p008 -->

# ws235-p008 規約の全文の見直し

- 親: [WS235](../ws.md)。
- 状態: cleared（2026-10-29 11:1x P1、q1669-i04）。
- 依存: p002〜p007。

## 範囲

WS235 の C: `src/drivers/ethernet/rtl8125.c`・`include/drivers/ethernet/rtl8125.h` の全文、`src/drivers/pci/pci-pcat.c`・`pci.c`・`include/drivers/pci/pci.h`・Makefile の
WS235 の hunk だけ。plan/coding-style.md の全文と guardrail の「出典」「開発の管理情報」で、書き方だけを直す（動作は変えない）。

## 記録（8ff470285）

- conformance-reviewer（P1 の helper）が見直し、P1 が diff を読んで確かめた。
- rtl8125.c（約 40 か所）: 呼び出しの引数の中の呼び出し（log の引数の read32・profile の時間、read-modify-write の read8、sched_sleep の deadline）を変数に、
  条件演算子 3 か所を if に、呼び出しの結果を直に返す所（csi_read）を変数に、閉じ括弧の後の空行と段落の comment（約 14）、目的の comment の無い if・待ちの後の settle（約 12）、
  `set_phy_patch_mode` を 1 つの `phy_ocp_modify` に（同じ引数）、二重の空行。
- pci.c: MSI-X の disestablish の WS235 の hunk で条件の中の呼び出しを外し、comment を足した。pci-pcat.c: clamp の前に comment。rtl8125.h・pci.h・Makefile は直す所無し。
- 残した物: pci.c・pci-pcat.c の WS235 の hunk の外の既存の書き方（WS235 の行でない）。`receive_frames` の `if (empty)` の中の `packet != NULL` の確かめは
  empty の時 packet が必ず NULL で届かない（動作の問題でなく、見直しの範囲で消さない）。`reset_phy`・`advertise_all`・`configure_phy` の 1 つの comment の下の
  複数の read・check・write（各 check はすぐ後にある）。
- 出典は block ごと（file の先頭に無い）、source に WS・Phase・Queue・Bug の番号・日付・plan/ への参照は無い。

## 確かめ

- build: `make ZEDBSD_CONFIG=plan/ws235/tests/config-amd64-rtl8127-profile.mk BUILD=build/ws235/amd64-p build/ws235/amd64-p/vmunix`、
  `make ZEDBSD_CONFIG=plan/ws235/tests/config-amd64-rtl8127-perf.mk BUILD=build/ws235/amd64-n build/ws235/amd64-n/vmunix` とも exit 0・warning 0（11:06、編集の後）、
  amd64 vmunix check PASS、`git diff --check` clean、`plan/tools/style-check.py src/drivers/ethernet/rtl8125.c` の指摘 0（前は 25）。
- 未実施: 見直しの後の実機の非退行（書き方だけの変更。最後の実機の確かめは [p006](../phase006/phase.md) の final-1、ae8cb27df）。host 試験は無い（host で動く部分が無い）。

## WS の受け入れの最後の確かめ（2026-10-29 11:30〜11:41 P1、q1669-i05、worktree p1-ws235e、基点 main 054afe9a9）

見直し（8ff470285）の後の tree で、試験の config と server の config の kernel を build し、RDNA 機（10.0.10.3）の RTL8127 で `rtl-link.sh` を流した。
lock「P1 q1669-i05 start 11:3x end 12:15」（11:30 に取り 11:41 に返した）。判定は ssh の ifconfig・route show と、centris の ping・ssh・perf-peer.py で（console・serial の log は読んでいない）。

| 回 | kernel | build（両方 exit 0・warning 0・amd64 vmunix check PASS） | 結果（`build/tmp/` は worktree p1-ws235e の物） |
| --- | --- | --- | --- |
| (1) | 試験の config、sha256 b16d3b633c93b1f3（cksum 27980807） | `make ZEDBSD_CONFIG=plan/ws235/tests/config-amd64-rtl8127-perf.mk BUILD=build/ws235/amd64-f build/ws235/amd64-f/vmunix build/ws235/amd64-f/bin/tcp-perf` | `ws235-final-f-113345`: rge0 media 10000Mbps、ue0 を落として rge0 だけで ping 200/200、ssh、SSH の 100 MB 両向き、tcp-perf 30 s **send 5642.3・recv 3310.5 Mbit/s**（peer 5641.9・3310.6）、rge0 の RX・TX の errors 0・dropped 0、reboot で Debian に戻った |
| (2) | server の config（`config/ci/config-amd64-server.mk`、`CONFIG_DRIVER_PCI_RTL8125 := y`）、sha256 debbd4212e738689（cksum 3764759058） | `make ZEDBSD_CONFIG=config/ci/config-amd64-server.mk BUILD=build/ws235/amd64-s build/ws235/amd64-s/vmunix` | `ws235-final-s`: rge0 media 10000Mbps、rge0 だけで ping 200/200、ssh、100 MB 両向き、tcp-perf 30 s **send 4743.0・recv 3364.1 Mbit/s**（peer 4742.7・3364.4）、errors 0・dropped 0、reboot で Debian に戻った |

- 命令（両回とも）: `POWERCYCLE=1 LANTEST=2 SECONDS_PERF=30 PERF=build/ws235/amd64-f/bin/tcp-perf sh plan/ws235/tests/rtl-link.sh <vmunix> <OUT>`。
  (2) の kernel は NVMe の root（userland は 2026-10-28 の物）のまま kernel だけを替え、tcp-perf は (1) の build の物を scp した。
- 値は 1 回ずつ。p006 の既定の config（send 4935・recv 3360）と同じ程度で、send の (1)・(2) の差（5642・4743）は 1 回ずつの値で原因は未確かめ。
- 最初の 1 回（11:30、`ws235-final-f`）は無効: 確かめの命令の誤り（`mount ... || mkdir && mount` で p4 を 2 回 mount、umount 1 回）で p4 が ro で残り、`rdna-nvme.sh kernel` が失敗、
  `rtl-link.sh` は `| tee` で失敗を見落として P2 の kernel のまま zedBSD を起動した（rge0 無し）。`rtl-link.sh` を、install の終了の値を拾う形（`nvme_kernel`）に直した（6876e3953、host で偽の rdna-nvme.sh の exit 1・0 で確かめ）。
- 最後の状態: Debian 6.12.111（grub の saved_entry 6.12.111、next_entry 空）、RTX は vfio-pci、NVMe の vmunix は P2 の vmunix-va1（cksum 1463007375・sha256 6a06e796…、照合一致）、p4 の mount 無し。電源の入れ直し無し。
- 未実施: jumbo frame、長時間の転送、相手の switch の port の読み、試験の config と server の config の差の繰り返しの計測。
