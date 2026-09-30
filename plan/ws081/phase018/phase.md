<!-- awesome-plan project=zedbsd record=ws081-p018 -->

# ws081-p018: Windows の QEMU 用の demo の image

Status: cleared（2026-09-30、サブエージェント P4、worktree `ws090-widgets`（branch `wt/ws090`、main を merge した上）。Linux の QEMU で確かめた。
Windows の機械は未実施（ユーザーが複写して使う））
Disposition: normal
Parent: [WS081](../ws.md)
Queue: なし（2026-09-30 Q1 の割り当て「ws081-p018: Windows の QEMU 用の demo の image」）

## 範囲と受け入れ

- demo の構成（`plan/ws075/demo/config-demo-hdmi.mk` の app の一式、Noct の accel、gpudemo）に touchlog を足し、Windows の QEMU に要る差を入れた構成。
- main の最新で build した `build/ws081-demo-win/hdd-image.img`（main の checkout の `build/` には置かない。複写は Q1）。
- Linux の QEMU で boot-test.sh、Venus と ssh の 1 行で `touchlog --seconds=3` が SUMMARY を出す。
- `windows-touch.md` の「用意」（image の複写先、boot.bat の hostfwd を自分で確かめる形）。

## 作ったもの

- `plan/ws081/tests/config-amd64-demo-win.mk`: 5330 の demo の構成から Windows の QEMU に合わせた差:
  - GPU は i915 と firmware を外し Venus（`CONFIG_DRIVER_PCI_VENUS`）。`display=edp`（5330 の固有）を外した。
  - app は 5330 と同じ一式（Files・Browser・Notes・PDF Viewer・Image Viewer・Settings・audiod・Terminal・X server・zgears など）。
    App Home の一覧にある Text Editor（`textedit`）が 5330 の構成に無かったので足した（S6・S14 のため）。
  - Noct の accel、gpudemo、openssh、graphical boot、`/usr/bin/touchlog`。
- `plan/ws081/tests/build-demo-win.sh`: App Home の一覧、壁紙、demo の account（root/root、kei/kei）、guest の harness の鍵、touchlog を入れて build する
  （`plan/ws075/demo/build-demo-image.sh` と同じ形）。touchlog は toolchain の sysroot（`build/amd64/sysroot`）で build する（新しい BUILD には sysroot が無い）。
- `plan/ws081/tests/windows-touch.md`: 「用意」の節を書き直した（image の複写先、boot.bat の `hostfwd=tcp::2222-:22` の見方と足し方、確かめの 1 行と失敗の読み方）。

## 確かめ

- build（main の最新、`build/ws081-demo-win`）: exit 0。image **`build/ws081-demo-win/hdd-image.img`**（2.2 GB。rootfs に `/usr/bin/touchlog`・
  `/bin/textedit`・`/bin/notes`・`/bin/pdfviewer`・`/bin/noct`・`/usr/share/gpudemo`）。warning は Noct の既存の 1 件（interpreter.c の -Wreturn-type）だけ。
- boot-test.sh（image の複写で）: **PASS**（`build/ws081/boot-demo-win/login.png`。Venus の無い QEMU では sessiond が console の login に戻る）。
- Venus の QEMU（`zdesktop-guest.sh`、usb-net の user network の ssh の転送）: kei の session が起き（`SESSIOND HANDOFF go`）、desktop が出た
  （`build/ws081/demowin-desktop.png`）。**password `root` の ssh の 1 行**（`sshpass -p root ssh -p PORT root@127.0.0.1 "touchlog --seconds=3 …"`）で
  touchlog が `SUMMARY` を出した。
  - Linux の QEMU には `usb-multitouch` が無いので、`touchlog --seconds=3` だけでは「no touch screen」になる（正しい振る舞い）。
    `--device=/dev/input/event0`（usb-tablet）を付けて SUMMARY の出方を確かめた。
  - WS085 との差: 転送の port は harness の空いた port（Windows の boot.bat は 2222）、touch の device は無い、WHPX の代わりに KVM。

## 残り

- Q1: `build/ws081-demo-win/hdd-image.img` を main の `build/` に複写する。
- ユーザー: `windows-touch.md` の「用意」（Windows の `data\hdd-image.img` への複写、boot.bat の hostfwd の確かめ）の後に計測の 3 行。
- Windows の機械での起動・touch・S8・S9・S14 は未実施。
