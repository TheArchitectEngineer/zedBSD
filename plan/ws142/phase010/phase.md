<!-- awesome-plan project=zedbsd record=ws142-p010 -->

# ws142-p010: AAT のシナリオ・T1 の試験・規約の見直し（p008〜p009 の締め）

Status: cleared（2026-10-08 Q1 の判定: p010-guest.sh が T1-342（2026-10-07、status 0）と T1-359（status 0）で PASS）
Disposition: normal
Parent: [WS142](../ws.md)
Queue: q781 / q781-i01

## 範囲と受け入れ

- AAT のシナリオ（draft）: [desktop.windows.layout-mode-switch](../../../tests/scenarios/desktop/windows/layout-mode-switch.md)、[desktop.touchpad.wiseview-swipe](../../../tests/scenarios/desktop/touchpad/wiseview-swipe.md)、[desktop.touchpad.preview-swipe-step](../../../tests/scenarios/desktop/touchpad/preview-swipe-step.md)。touchpad の 2 つは 5330 の人の指（hardware・hands）。
- QEMU の自動の試験: [p010-guest.sh](../tests/p010-guest.sh)（pen の guest、touchinject の pad、wltest の新しい `--fixed`）。p008（mode・切り替え・前の窓・固定の大きさの中央）、p008b（中央の窓の撮影）、p009（TOP2・WiseView と switcher の 1 swipe 1 つ・下 swipe の確定・全画面の BOTTOM2）を通す。
- 既存の試験への影響: AAT の after に `back_to_windowed`（p008）。p005-guest.sh（q787 で直した物、T1-216b）・bug194-guest.sh 5.（T1-205b）は p008 の入った image で流す。
- 規約: WS142 の p008〜p009 で変えた C（layout.c・.h、swipe.c・.h、shell.c・protocol.c・touchpad.c・.h・switcher.c・.h・switcher-shell.c・input.c・zwl.h・apps-bar.c、試験の host-*.c、wltest）を coding-style.md と照らし、style-check は指摘 0（wltest/main.c の既存の goto 2 件を除く）。手での見直し: 宣言は関数の頭、条件の中の関数の呼び出しなし、Boolean は if で、return の前の comment、split した条件の行分け。

## 試験の補助（2026-10-06）

| 所 | 内容 |
| --- | --- |
| `userland/tests/wltest/{main.c,window.c,wltest.h,README.md}`・`userland/tests/acquire-fence/main.c` | `--fixed`: 最小と最大の大きさを `--size` にし、compositor の configure の大きさを受けない（大きさの固定の third-party の窓の代わり）。`wltest_window_open` に `fixed` の引数（acquire-fence は 0） |

## 確認

| 確認 | 結果 |
| --- | --- |
| wltest・acquire-fence-test の build（config-amd64-zdesktop.mk） | 成功、warning 0 |
| `check-scenarios.py` | PASS（84） |
| `sh -n p010-guest.sh` | ok |
| QEMU（p010-guest.sh） | 未実施。T1 に依頼（Q1 経由） |
| 実機 5330 | 未実施（touchpad の 2 つのシナリオ、ユーザーの UAT） |

## q795（2026-10-06 P2）: T1-224 の 4. front-docks の FAIL

- 切り分け: 試験の側。zedBSD の kernel は process の command line を 63 byte までしか持たず（`KERN_SYSTEM_PROCESS_COMMAND_MAX` 64）、`ps -o args` もそこまで。`/bin/wltest --windowed --size=400x280 --color=d0f4d0 --app-id=apps.b` は 68 byte で `--app-id=apps.b` が切れ、4. の `grep "[w]ltest .*app-id=apps.b"` が何にも当たらず kill されなかった（log に client=3 の CLIENT gone が無く、後の WiseView に client=3 の tile が残る、のとおり）。compositor の機能の問題ではない。
- 直し（cbf196c1）: `open_app` の `--app-id` を先頭に（`/bin/wltest --app-id=apps.b …`）、kill の grep を `[w]ltest --app-id=apps.b `、kill の後に `ZWL CLIENT gone client=B` を確かめる項目 `apps-b-gone` を足した。`sh -n` ok。流し直しは T1。

## q795-i02（2026-10-06 P2）: T1-224b でも 4. が FAIL、前の切り分けの訂正

- 前の推定（command line が 63 byte で切れる）は誤り。zedBSD の kernel は process の command に **argv[0] だけ**を持つ（`src/kern/exec.c` の `kern_strncpy(process->command, argv[0], …)`）。`ps -o args` は `/bin/wltest` としか出さず、引数の `--app-id=apps.b` で探す grep はいつも空で、`kill` は何もしなかった（kill.txt は「killed」だけ）。compositor の切断の検出の問題ではない（kill されていない）。
- 直し: `open_app` が起動した wltest の pid を guest の `/tmp/NAME.pid` に残し（`echo $! > /tmp/NAME.pid`）、4. はその pid を kill（kill.txt に ps のその行と pid）。kill の後の `apps-b-gone`（`ZWL CLIENT gone client=B`）で compositor の切断の検出も確かめる。`sh -n` ok。
