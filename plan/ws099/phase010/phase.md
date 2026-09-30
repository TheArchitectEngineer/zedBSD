<!-- awesome-plan project=zedbsd record=ws099-p010 -->

# ws099-p010: BUG-122 compositor が落ちた後も graphical の login に戻る

Status: cleared（2026-09-30、サブエージェント P4、worktree `ws035-keiland`（branch `wt/ws035`）、main を merge した上。QEMU の Venus、実機は未実施）
Disposition: normal
Parent: [WS099](../ws.md)
Queue: なし（2026-09-30 Q1 の割り当て「BUG-122 を ws099 の次の Phase として直す」）
依存: p001（cleared）

## 範囲と受け入れ

- [BUG-122](../../bugs/BUG-122.md): compositor が落ちた後、greeter が 3 回失敗して文字の console に落ちる。compositor を kill して再現し、
  sessiond の log で理由を見る。
- 目標: デモ中に compositor が落ちても graphical の login に戻れる。
- IME の file と seat.c の IME の hook には触らない（触っていない）。

## 再現（QEMU の Venus、criteria の image `build-criteria-image.sh`）

- session の compositor（kei の `/bin/wayland`）を kill -9 → sessiond が greeter を起こし、greeter は出た（ここでは失敗しない）。
- greeter の compositor を、出るたびにすぐ kill -9 → 3 回目で `SESSIOND CONSOLE reason=greeter-failed failures=3`、文字の console の login に落ちた
  （5330 と同じ終わり方）。
- 理由: sessiond は起動から 20 秒以内に終わった greeter を失敗と数え、1 秒の待ちで 3 回続くと console に戻す。5330 の greeter は
  `ZWL EXIT error=21`（EOPNOTSUPP）で終わっていた（ws075-p025）。GPU の回復の時間に対して 3 回・1 秒は短い。EOPNOTSUPP の理由（i915 の
  状態）は QEMU では再現できない。

## 変更

- `userland/desktop/sessiond/main.c`: console に戻すまでの失敗を 3 → 6 回、待ちを 1・2・4・8・16 秒に（合わせて約 31 秒、`main_retry_delay`）。
  `SESSIOND GREETER retry failures= delay=` を log に出す。冒頭の説明も直した。
- `userland/desktop/sessiond/greeter.c`: greeter が失敗したら、greeter の log の最後の `ZWL EXIT` の行（無ければ最後の行）を
  `SESSIOND GREETER failed reason="…"` として sessiond の log に写す（`greeter_reason`）。5330 で次に起きたとき理由が sessiond の log に残る。
- `plan/ws099/tests/bug122-recovery.sh`（新）: 下の試験。
- 規約: `style-check.py main.c greeter.c` 0、`git diff --check` 0。build の warning 0。

## 試験

### `bug122-recovery.sh`（QEMU の Venus）

| image | 結果 |
| --- | --- |
| 直しの前（`build/ws099/p010.img`） | **FAIL**: greeter の 3 回目の kill の後に console、以後 greeter は出ず、kei は login できない（`build/ws099/bug122-before.log`） |
| 直しの後（`build/ws099/p010-new.img`） | **PASS**: session の compositor の kill の後に greeter、greeter の 5 回続けての kill の後も console に落ちず（retry 5 回、待ち 1・2・4・8・16 秒、reason 5 行）、greeter が出て kei が login できた（`build/ws099/bug122-after.log`、画面 `build/ws099-shots/bug122/greeter.png`・`greeter-after.png`・`session.png`、log `sessiond.log`） |

- kill -9 の greeter の reason は最後の行（`ZWL GLASS glyph …`）になる（signal で終わった compositor は `ZWL EXIT` を書かない）。
  5330 の EOPNOTSUPP のように自分で終わる compositor では `ZWL EXIT … error=` の行が入る。

### 回帰・boot（`build/ws099/p010-final.sh`、出力 `build/ws099/p010-final.out`）

- C1（`criteria.sh … C1`、sessiond が動かす替わり目）: p126 **PASS**（login・Log Out の替わり目）、c1-boot-shutdown **PASS**（起動・Shut Down で
  黒 0・文字 0、QEMU が止まる）。
- boot test: `build-ssh-image.sh build/amd64`（exit 0、sessiond の warning 0）→ `boot-test.sh` **PASS**（`build/ws099/boot-p010/login.png`）。

## 制限・残り

- 6 回続けて落ちる（約 31 秒の間 GPU が戻らない）ときは、今までどおり console に戻す（無限に試すと画面が何も出ないまま残るため）。
- 5330 での確認は未実施（WS099 の L2）。5330 の EOPNOTSUPP の原因（i915）は、次に起きたとき sessiond の log の reason で見る。
