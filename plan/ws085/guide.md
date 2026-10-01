# WS085 作業の手引き（2026-10-01）

WS085（Windows 版 WINQ-EMU の Venus で Kei のデスクトップを表示する）を、追加の調査なしで続けるための手引き。記録の正本は [ws.md](ws.md) と [p001](phase001/phase.md)。
「提案」の Phase は main（Q1）が ws.md に足すまで実行しない。Queue に入っていない Phase は実行しない（AGENTS.md）。
command は repo の root から。`<W>` は作業の名前（例 `ws085-p001b`）。build と回帰の一般の command は [WS104 の commands.md](../ws104/commands.md)。

**この WS の大部分は Windows の機械の上の作業で、エージェントは Windows を操作できない。** エージェントの仕事は、(1) guest の側（kernel の Venus の driver・libvulkan）の
変更と Linux の Venus での回帰、(2) Windows に渡す image の用意、(3) ユーザーの手順の 1 枚と結果の記録。

## ゴール

- 目標（ws.md、2026-09-29 ユーザー）: Windows 版 WINQ-EMU Alpha10（Venus 1.4、WHPX、SDL）で既定の amd64 の image のデスクトップを表示し、copy 表示の速度の問題を調べ、
  Windows の SDL の指の入力で人が touch の UI を debug できるようにする。**Linux の host の Venus（1.3）の経路を保つ**（同じ image が両方で動く）。HAL と toolchain は変えない。
  vendor（QEMU・virglrenderer の fork）の変更はユーザーがレビューして commit・push する。
- fg010 での役割: **デモの touch の土台**（2026-09-30 朝 ユーザー「タッチはWindows上のQEMUでやります」）。S4・S8・S9・S14 の touch の場面（WS081・WS102・WS090・WS094）を Windows の QEMU で見せる。
- p001 の受け入れ（phase.md「範囲と受け入れ」と「手順・制限」）で残っているもの:
  1. 表示の所有者の終了・切替（Log Out → greeter → login、app の終了）で QEMU が止まらない（[BUG-101](../bugs/BUG-101.md) の残り）。
  2. 継続のアニメーションの速度（合成の frame の間隔）。
  3. 物理の Windows の touch panel からの SDL の event が Kei に届く（WS081 の L2 と同じ確かめ）。

「完了」の具体: 上の 3 つをユーザーが Windows で確かめ（結果を p001 に記録）、BUG-101 を resolved にし、guest の側の変更の全文の規約の見直し（提案 p003）が cleared。

## 今の状態（2026-10-01）

| 項目 | 状態 | 証拠 |
| --- | --- | --- |
| Windows の renderer の strict・quiesce の契約（capset 168 B、magic `0x5a424453`） | 済み（Windows の UCRT64 の DLL build、`vkdemo --offscreen` の hash 一致） | [p001](phase001/phase.md)、[renderer の patch](phase001/winq-virglrenderer-alpha10.patch) |
| mapped blob の scanout（flags 15、host scanout bit 8） | 済み。WHPX の SDL で 1280×800 のデスクトップ（ユーザーの目視と QMP screendump）。合成 16〜34 ms/frame | p001 |
| guest の側の flags 15 の受け入れ | main に入っている: `src/drivers/gpu/venus/transport.c:36-39`・`:1845-1898`（`venus_strict_queue_find`）、`userland/desktop/libvulkan/context.c:164-197`（`copy_display`・OPAQUE・strict・quiesce）、`wsi-image.c`・`wsi-display.c:818` | merge dac0ed2b（ws.md「取り込みの後の確認」） |
| SDL の指 → 仮想 usb-multitouch（10 指、Scan Time、63 byte） | QMP の 2 指の注入で Home が開く。**物理の touch panel は未試験** | p001「Windows SDL multitouch」 |
| Files の起動の停止（共有画像の import の padding と fd の所有） | 直した（renderer の `proxy_server.c`・`proxy_context.c`）。Files の開閉・再起動・Terminal と同時 | p001「Files起動時の停止の修正」 |
| 配布の folder `C:\Work\winq-zedbsd`（`boot.bat`、`data/hdd-image.img`） | ユーザーの Windows の機械にある | p001「Windows配布フォルダ」 |
| Linux の Venus（1.3）の回帰 | merge の直後に CI の構成の image で desktop が出た。以後は WS099 の criteria・WS103 の V2 で毎回通っている | ws.md、[WS103](../ws103/ws.md) |
| WS103（2026-10-01、compositor の GPU の直の ioctl を libvulkan へ）の後の Windows | **未確認**。表示の claim・release が libvulkan の中に移った（BUG-101 の所有者の切替に関わる） | — |

既知の bug: [BUG-101](../bugs/BUG-101.md)（scheduled、WS085 p001: 所有者の終了・切替とアニメーションの速度が未検証）、[BUG-124](../bugs/BUG-124.md)（tracking: QEMU の Venus で Model viewer の swapchain が DEVICE_LOST。Windows のデモで出たら優先を上げる）。
関連: [WS088](../ws088/ws.md)（Kei-nightly.zip の CI 配布）、[WS081](../ws081/ws.md)（Windows の touch の計測）。

ws.md の `Queue: q499` は古い（q499 は終わっている。今の Queue は無い）。ws.md の頭に `awesome-plan-current` の印が無い（他の WS と形が違う）。main が直す。

## 次の作業の順番

1. **ws085-p001 の残り（ユーザーの Windows の確かめ）**。手順は [p001 の「手順（2026-10-01 追記）」](phase001/phase.md)。エージェントは最新の main の image を作り、
   Linux の Venus の回帰を流してから、ユーザーに手順の 1 枚を渡す。WS081 の L2（touch の計測）と同じ image・同じ起動で 1 回にまとめる（ユーザーの時間を節約）。
2. **ws085-p002（提案）BUG-101 の所有者の切替の直し（guest の側）**: 1 でユーザーが Log Out・login・app の終了で QEMU の停止か表示の失敗（`No such device`、`VKDEMO FAILED … result=-4`）を見た時だけ。
   - 調べる所: libvulkan の表示の lease の release（`userland/desktop/libvulkan/wsi-display.c`、`copy_display` の分岐 `:818`）、kernel の Venus の共有の scanout（`src/drivers/gpu/venus/share.c`）、
     compositor の OS の module（`userland/desktop/wayland/gpu-zedbsd.c`、WS103 で表示の claim・release を移した所）。
   - Linux の Venus（flags 7）の経路を変えない。直しは flags 15 の分岐の中だけ。Linux の回帰（下）を必ず流す。
   - QEMU の fork の側（`virtio-gpu-virgl.c` の mapped blob の表示面）が原因なら、vendor の source はこの checkout に無い（下の「注意」）ので、ユーザーに原因と修正の案を渡す。
3. **ws085-p003（提案）全文の規約と回帰**（必須の最後）: WS085 で変えた guest の source（`src/drivers/gpu/venus/internal.h`・`share.c`・`transport.c`、`userland/desktop/libvulkan/context.c`・`internal.h`・
   `wsi-display.c`・`wsi-image.c`、`userland/desktop/wayland/Makefile` の font、`git diff --stat dc339f71 dac0ed2b -- src userland/desktop` で一覧）を
   [coding-style.md](../coding-style.md) の全文で見直し、`plan/tools/style-check.py`、build（warning 0）、Linux の Venus の回帰、boot test。vendor の fork の source は範囲の外（ユーザーのレビュー）。

## 未知と調べ方

| 未知 | なぜ要るか | 調べ方 |
| --- | --- | --- |
| WS103・WS104 の後も Windows（flags 15）で表示・Files・Terminal が動くか | WS103 で表示の claim・release と buffer の import が libvulkan に移った。Windows の経路は誰も試していない | ユーザーの確かめ（p001 の手順 1〜2）。Linux の Venus では WS103 の V2 で通っている。差は flags 15 の分岐（`context.c:191-196` の `copy_display = VK_FALSE`、`wsi-image.c` の `copy_display == VK_FALSE` の 5 か所: `:65`・`:95`・`:132`・`:260`・`:312`）だけなので、まずそこを読む |
| 所有者の切替で QEMU が止まる原因（BUG-101） | デモで Log Out・app の終了をする | ユーザーに Log Out → login と、Files の開閉を 5 回してもらう。止まったら QEMU の画面（SDL）と PowerShell の QEMU の stderr（`boot.bat` の窓）の最後の行を送ってもらう（host の QEMU の log であり、guest の console の log ではない）。guest の側は SSH で `/run/user/1000/session.log` の `ZWL` の最後の行と `/var/log/sessiond.log` の `SESSIOND HANDOFF` を読む |
| vendor の fork の変更が commit・push されたか | Kei-nightly の base の zip（WS088）と再現の build | ユーザーに聞く（ws.md「vendorの変更はユーザーがレビューしてcommit/pushする」、WS088 の Resume point「fork の commit とユーザーの確認待ち」）。`awemorris/qemu-win32-vulkan`・`awemorris/virglrenderer`（WS088 の ws.md） |
| QEMU の fork の差分（mapped blob の表示面、SDL の指、usb-multitouch） | 不具合が QEMU の側の時の直し | この checkout に source が無い。renderer の差分だけ [p001 の patch](phase001/winq-virglrenderer-alpha10.patch) にある。QEMU の差分はユーザーの fork（WS088 の ws.md）。Windows の build の参考は Linux の `~/linux-pc98/scripts/build-qemu-win64.sh`（p001、存在を 2026-10-01 確認） |
| アニメーションの速度（合成の frame の間隔） | デモの見た目（S8・S9） | WS081 の p017 の session の `--log-frames` の image を使えば、`ZWL COMPOSE … at_ms=` の間隔で測れる（[WS081 の p017](../ws081/phase017/phase.md)） |
| guest の不具合の切り分け（Linux の QEMU で再現する時） | — | QEMU の console・serial の log では判定しない。kernel は gdbstub（`GUEST_RUNTIME=… python3 plan/tools/guest/guest.py kgdb --symbols <BUILD>/vmunix 'bt'`）、monitor・QMP（`plan/tools/qmp.py <runtime>/qmp.sock query-status`）、user は zdesktop の log |

## コマンド

### build

| 何 | command | 出力 |
| --- | --- | --- |
| CI と同じ構成の image（Windows の既定の image、Kei-nightly に入る物） | `make -j64 ZEDBSD_CONFIG=config/ci/config-amd64.mk BUILD=build/<W>-ci disk-image > build/<W>/ci-build.log 2>&1; echo "make exit=$?"` | `build/<W>-ci/hdd-image.img` |
| デモの app の一式の Windows 用の image（touchlog・Text Editor 入り、WS081） | `sh plan/ws081/tests/build-demo-win.sh build/<W>-demo-win > build/<W>/demo-win-build.log 2>&1; echo "exit=$?"` | `build/<W>-demo-win/hdd-image.img` |
| Kei-nightly の zip（任意） | `make BUILD=build/<W>-demo-win kei-nightly-zip` | `build/<W>-demo-win/Kei-nightly.zip`（network で base の zip を取る。`tools/release/kei-nightly.mk:48-55`） |

- 先に `mkdir -p build/<W>`。image の build は同時に 1 つ。必ず BUILD を渡す。warning の確かめは commands.md §1 の grep。
- `build-demo-win.sh` の前に `ls build/amd64/sysroot/usr/lib/crt1.o`（無いと `build/amd64` に image を作ってしまう。[WS081 の手引き](../ws081/guide.md) の注意）。
- ws.md の Linux の確かめは `make ZEDBSD_CONFIG=config/ci/config-amd64.mk BUILD=build/ws085-ci disk-image` だった（ws.md「取り込みの後の確認」）。

### Linux の Venus（1.3、flags 7）の回帰（guest の側を変えた時は必ず）

```
sudo modprobe vgem && sudo chmod 0666 /dev/dri/renderD128
sh plan/ws099/tests/build-criteria-image.sh build/<W>-criteria > build/<W>/criteria-build.log 2>&1; echo "exit=$?"
sh plan/ws099/tests/criteria.sh build/<W>-criteria/hdd-image.img build/<W>/criteria C1 C2 C9
grep -c ' FAIL ' build/<W>/criteria/results.txt
OUTPUT=build/<W>/boot plan/tools/boot-test.sh build/<W>-criteria/hdd-image.img; echo "exit=$?"
```

- PASS: build `exit=0`、grep が `0`（C9 の p076 だけの FAIL は [BUG-125](../bugs/BUG-125.md)、単独で流し直して併記）、`boot-test: PASS …`。約 30 分（[commands.md §5](../ws104/commands.md)）。
- libvulkan・Venus の driver を変えた時は GPU の境界の試験も（[commands.md §6](../ws104/commands.md) の forge・fence）。
- CI の image の desktop が出ること: `sh plan/ws035/tests/zdesktop-guest.sh start build/<W>-ci/hdd-image.img`、`sh plan/ws035/tests/zdesktop-guest.sh wait --timeout 240`、
  `python3 plan/ws035/tests/zdesktop-shot.py build/<W>/ci-desktop.png --runtime build/ws035-sq-run`、`sh plan/ws035/tests/zdesktop-guest.sh stop`。PNG を見て desktop（壁紙・system bar）を確かめる。

### Windows（ユーザーが行う）

- 起動: `C:\Work\winq-zedbsd\boot.bat`（自分の directory に移って QEMU を相対 path で起動。`-L share`、WHPX、Venus、SDL、`-device usb-multitouch,bus=xhci.0,port=1`、COM1 は stdio。p001「Windows配布フォルダ」）。
- image: `C:\Work\winq-zedbsd\data\hdd-image.img` を上書き（元を残すなら先に名前を変える）。
- SSH: `ssh -p 2222 root@127.0.0.1`（password `root`、boot.bat の `hostfwd=tcp::2222-:22` が要る: [WS081 の windows-touch.md](../ws081/tests/windows-touch.md) の「用意」2）。file は `scp -P 2222 root@127.0.0.1:<path> .`（PowerShell の `>` は UTF-16 で書くので使わない）。
- 元の `C:\WINQ-EMU\bin` は変えない（p001）。

## 実機（Dell Latitude 5330）

WS085 は 5330 を使わない（Windows の機械の QEMU）。5330 の一般の手順は [plan/tools/hw5330/README.md](../tools/hw5330/README.md)。
5330 の demo の image（`plan/ws075/demo/build-demo-image.sh`、i915）と Windows の image（Venus）は別の構成（`plan/ws081/tests/config-amd64-demo-win.mk` の先頭の説明）。

## 注意

- **エージェントは Windows を操作しない。** ユーザーの手順は短く（3〜5 行）、結果は SUMMARY・PNG・log の file で受け取る。
- vendor の fork（`vendor/winq-emu-qemu`・`vendor/winq-emu-virglrenderer`）は main の merge で submodule を外したので、この checkout に無い（2026-10-01 `ls vendor` で確認: `intel-vbt`・`raspberrypi-firmware` だけ）。
  fork の source を repo に戻すのはユーザーの判断。
- Linux の 1.3.269 の profile（flags 7）を変えない。flags 15 の分岐は「exact な値の一致」で選ぶ形（`context.c:172-196`、`transport.c:1886-1896`）を保つ。
- HAL と toolchain を変えない（ws.md）。HAL の API の変更は差分ごとの事前承認（AGENTS.md）。
- 共有の runtime `build/ws035-sq-run`（criteria.sh・zdesktop-guest.sh の既定）を他の試験と同時に使わない。
- 2026-10-01 から WS104 が libvulkan の周りと compositor の OS の module を動かす（`gpu-zedbsd.c` など）。guest の側を変える前に main に確かめる。
- 2026-10-10 ごろ以降は bug の修正と実機の調整だけ。
- commit は自分の path だけ `git commit -m WIP -- <path>...`。push しない。
