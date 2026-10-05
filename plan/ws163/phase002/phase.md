<!-- awesome-plan project=zedbsd record=ws163-p002 -->

# ws163-p002: PIN の保存と lock の画面の PIN の unlock（mock）

Phase ID: `ws163-p002`
Parent: [WS163](../ws.md)
Status: in-progress（2026-10-05 P1 generation19 q769。実装と host 試験まで済み、T1 の QEMU の試験待ち）
Phase disposition: normal
Queue: q769

## 範囲

[p001 §9.1・§9.2](../phase001/phase.md) の mock: PIN の hash を利用者の `~/.config/keiland/pin` に置き、lock の画面（compositor）が自分で確かめる。
sessiond は変えない。greeter の PIN は G1 の答え待ちで範囲外。

## 実装（d9ab018d）

- `userland/desktop/wayland/pin-store.c`・`pin-store.h`（新規）: file の path、6 桁の判定、読み書き（mode 0600、一時 file から rename、folder は 0700 で作る）、
  設定（SHA-512 crypt、`$6$rounds=20000$`、salt は `/dev/urandom` の 16 文字）、削除、確かめ（合えば失敗の数を 0 に、違えば数を足して書く、
  5 回で無効＝EPERM）、password の unlock で数を戻す（forgive）。比べは長さを揃えて全 byte。crypt() は POSIX で、Linux・FreeBSD は `-lcrypt`。
- `userland/desktop/wayland/greeter.c`: lock の時に PIN の file を探し（`zwl_settings_home` + `zwl_pin_store_path`）、使えれば欄の案内を
  「PIN or password」に。Enter で 6 桁の数字なら PIN として compositor が確かめる（`ZWL LOCK unlocked pin`）。違えば「Wrong PIN. Try again.」、
  5 回目で「Too many wrong PINs. Use your password.」（以後の 6 桁は password として sessiond へ）。password の unlock で `forgive`。
  `ZWL LOCK locked` の行に `pin=0|1` を足した（今の試験は前方一致なので変わらない）。
- `userland/desktop/wayland/settings.c`・`zwl.h`: `settings_home` を `zwl_settings_home` として出した。
- Makefile（zedBSD・Linux・FreeBSD）に `pin-store.c`、Linux・FreeBSD の compositor の link に `-lcrypt`。

## 確認

- host: `plan/ws163/tests/pin-store-host-test.sh` PASS（path、6 桁の判定、設定・mode 0600・PIN が file に無い、合う・違う・数・5 回で無効・
  forgive・変更・壊れた file・削除）。
- build（warning 0）: zedBSD の `BUILD=build/ws163` の `bin/wayland`・`bin/settings`、Linux の `make keiland-linux`（`libcrypt.so.1` に link）。
  FreeBSD の build は未実施（host が無い）。
- QEMU: **未実施**。T1 に `plan/ws163/tests/pin-lock-guest.sh`（graphical の login の image、lock・PIN の unlock・5 回で無効・password で戻す・log に PIN が無い・
  mode 0600）を依頼する（Q1 経由）。zedBSD の libc の crypt() が openssl の `$6$salt$`（rounds 無し）を読めることもこの試験で確かめる。

## 残り

- T1 の結果。
- greeter の PIN の login（G1）。
