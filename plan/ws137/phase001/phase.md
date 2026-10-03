<!-- awesome-plan project=zedbsd record=ws137-p001 -->

# ws137-p001: FreeBSD 15.1 の試験の guest の道具と backend の build・試験の道具

Status: cleared（q658、2026-10-04。Q1 判定）
Disposition: normal
Parent: [WS137](../ws.md)
Queue: q658 / q658-i01（2026-10-04 user「FreeBSDのVMイメージを用意して、T1,T2で使えるようにした上で、書くだけだったFreeBSDのテストも実行するようにお願いします。libkeiland-backendのビルドと実行のことです。」、Q1 の割り当て）
所有 path: `plan/tools/keiland-freebsd/`、`plan/ws137/`

## 目的と範囲

WS137 の完了の条件 1・2 の道具: 公式の FreeBSD 15.1 の VM image から試験の guest を作る `build-guest.sh`、Linux の guest.sh と同じ形の
`guest.sh`（`guest.py`）、guest の中で libkeiland-backend・libkeiland・compositor を native に build して host 試験を流す `backend-test.sh`、
README の手順。条件 3（WS131 p004〜p008 の FreeBSD の項目を T1・T2 で流す）は p002。

## 作った物

| file | 内容 |
| --- | --- |
| [build-guest.sh](../../tools/keiland-freebsd/build-guest.sh) | `[--force] [OUT]`、既定 OUT `build/keiland-freebsd/guest`、既存の `guest.qcow2` は再利用。公式の CHECKSUM.SHA256 の image の行を [q550 の記録](../../history/ws109/q550/CHECKSUM.SHA256) と比べ、取得物を sha256sum で確かめる。guest 専用の鍵と NoCloud の seed（root と kei（wheel・video）、鍵だけ）、24 GiB、初回の起動の完了を SSH で待ち（`/firstboot` が消え boottime が 30 秒不変）、`pkg install`、`packages.txt`・`versions.txt`・`pkg-install.txt`・`first-boot.png` を残して止める。準備は `OUT/prepare` と port 2236 |
| [guest.sh](../../tools/keiland-freebsd/guest.sh)・[guest.py](../../tools/keiland-freebsd/guest.py) | `start|stop|status|ssh|put|get|copy|shot`。起動ごとに `GUEST_RUN` に overlay（base は読み取り専用、他の checkout の image も `GUEST_DIR` で使える）、`127.0.0.1:2235`、QMP の screendump、serial は `null`（log を読まない）、KVM は `plan/tools/guest/qemu-accel.sh`、8 GiB・8 CPU。`copy DEST PATH...` は working tree の追跡中・無視されない file を tar で写す |
| [backend-test.sh](../../tools/keiland-freebsd/backend-test.sh) | guest が止まっていれば start、終われば stop。tree を `/root/keiland-src` に写し、build（exit 0・`warning:` 0）・install と header-dependencies・`native-build-audit.py`・`plan/ws131/tests` の host-seat-freebsd・host-session・host-power・`sync-rejected.c`・`dmabuf-export-rejected.c`。`summary.txt` に PASS/FAIL/SKIP、全 PASS で exit 0 |
| [README.md](../../tools/keiland-freebsd/README.md) | 先頭に WS137 の節（手順・変数・規則） |

## 確認（2026-10-04、host の QEMU+KVM、P3 の QEMU 1 つ。実機ではない）

| 確認 | 結果 |
| --- | --- |
| 公式の CHECKSUM.SHA256（`download.freebsd.org/releases/VM-IMAGES/15.1-RELEASE/amd64/Latest/`）の image の行 | q550 の記録と同じ（`e4ca4db889f8559c9b9dfcacc70405c038476f4b6d41649b152d3809a2ed9e1f`）。取得物（664,729,340 byte、Last-Modified 2026-06-12）の sha256sum: OK |
| `timeout 2400 sh plan/tools/keiland-freebsd/build-guest.sh` | exit 0、約 3 分。初回の起動は SeaBIOS・`-cpu host` で login prompt（`first-boot.png`）。`freebsd-version -ku` 15.1-RELEASE-p4 ×2、amd64、base clang 19.1.7、GNU Make 4.4.1、Python 3.12.14（`python3` の meta package）、meson 1.10.2、ninja 1.13.2、pkgconf 2.4.3、vulkan-loader/headers 1.4.356、libdrm 2.4.133、mesa-dri 26.1.3、seatd 0.9.3_1（46 package） |
| `guest.sh start` → `ssh`（root、`SSH_USER=kei`）→ `shot` → `status` → `stop` | 全て exit 0。kei は wheel・video。screendump は login prompt（`build/keiland-freebsd/console.png`）。stop で overlay を消す |
| `timeout 5400 sh plan/tools/keiland-freebsd/backend-test.sh`（1 回目） | build FAIL: copy の path に `userland/packages/fonts` が無かった（道具の不足、`userland/packages` 全体に直した） |
| 同（2 回目、tree c95ee35 と本 Phase の道具の未 commit の変更） | **PASS**: build（351 source、libkeiland-backend.a・libkeiland.so・libkeiui.so ほか 11 の .so と compositor `bin/wayland`・app・試験 app）、`warning:` 0、install、audit（PASS、362 membership・646 header・11 library）、host-seat-freebsd 13/13、host-session 31/31、host-power 17/17・6/6（ASan・UBSan、native の clang）、sync-rejected PASS、dmabuf-export-rejected PASS。全体で約 1.5 分 |

証拠（ignored、worktree p3）: `build/keiland-freebsd/guest/`（CHECKSUM.SHA256・packages.txt・versions.txt・pkg-install.txt・first-boot.png）、
`build/keiland-freebsd/backend-test/`（step ごとの log と summary.txt）、`build/keiland-freebsd/console.png`。

source の不具合: 見つからなかった（WS131 の FreeBSD の「書くだけ」の部分は native で warning 0 で build でき、host 試験は全て PASS）。

## 未実施・制限

- GPU・seat・窓の probe（README の i915 の節: dmabuf-native-flags・wsi-window・gpu-seat-fixture・main-apps）は GPU が無いこの guest では流せない（i915 の passthrough は使わない、WS137 の規則）。compositor の起動・表示は未確認。
- WS131 p004〜p008 の各 phase.md への結果の記入は p002（T1・T2）。
- `--force` の再作成、他の checkout の image を `GUEST_DIR` で読み取り専用に使う形、2 つの guest の同時の起動は手順だけで、未実施。
- 初回の起動の package の upgrade（firstboot）で版が 15.1-RELEASE-p4 より新しくなりうる（`versions.txt` に記録される）。

## 再開・次

Q1 が merge し、Master の Tools 節に行を足す（案は Q1 への報告）。p002 は T1・T2 がこの道具で WS131 p004〜p008 の FreeBSD の項目を流す。

## 結果（Q1、2026-10-04）

cleared。P3 の QEMU+KVM で build-guest.sh（公式の CHECKSUM が q550 の記録と一致、image の sha256 OK、15.1-RELEASE-p4、clang 19.1.7、46 package、約 3 分）、guest.sh の start・ssh・shot・status・stop、backend-test.sh（351 source の native build warning 0、native-build-audit PASS、host-seat-freebsd 13/13・host-session 31/31・host-power 17/17・6/6、sync-rejected・dmabuf-export-rejected）が全て PASS。GPU の probe は GPU の無い guest のため未実施。
