<!-- awesome-plan project=zedbsd record=ws099-p012 -->

# ws099-p012: C1 の 5330 の目視と、5330 での BUG-119・BUG-122 の確かめ（L2）

Status: planned（2026-10-01 手順を追記。Queue なし）
Disposition: normal
Parent: [WS099](../ws.md)
Queue: なし
依存: p004・p009・p010 cleared、ws073-p043（BUG-119 の電源断）resolved。ユーザーの時間（5330 の前に座る 15 分）

## 範囲と受け入れ

- C1 の実機の部分: 5330 を USB の demo の image から素で起動し、起動 → greeter（自動の login が有効なら kei の desktop）→ Log Out → greeter →
  login → Log Out → Shut Down を 1 回通す。黒い画面と文字の console が 0 枚（ユーザーの目視）。
- BUG-119: greeter の Shut Down で「Shutting down...」の後、10 秒以内に電源が切れる（電源の LED が消える）。
- BUG-122: 通しの間に sessiond が greeter を起こし直していない（5330 の sessiond の log に `SESSIOND GREETER failed` が 0 行）。
- 範囲外: source の変更。問題が出たら記録して uncleared にし、直しは別の Phase（i915 の引き継ぎは WS084、電源断は ws073）。

## 手順（2026-10-01 追記）

エージェントの用意（repo の root で）:

```
mkdir -p build/ws099-p012
sh plan/ws075/demo/build-demo-image.sh build/ws099-p012-demo > build/ws099-p012/demo-build.log 2>&1; echo "exit=$?"
grep -E ':[0-9]+:[0-9]+: warning:' build/ws099-p012/demo-build.log | grep -vE '/packages/|^\.\./src/|userland/base/noct/noct/' | wc -l
OUTPUT=build/ws099-p012/boot plan/tools/boot-test.sh build/ws099-p012-demo/hdd-image.img; echo "exit=$?"
sha256sum build/ws099-p012-demo/hdd-image.img
```

- PASS: `exit=0`、warning の数 `0`、`boot-test: PASS build/ws099-p012/boot/login.png`。boot test の PNG をユーザーに見せる。
  （boot test の QEMU には i915 が無いので console の login に戻ることがある。PASS の判定は script の判定のまま。）
- 他の image の build が走っていないことを確かめてから（`pgrep -af 'make .*disk-image'` が空）。

ユーザーへ渡す手順（このまま送る。USB への書き込みと起動の一般の手順は [plan/tools/hw5330/README.md](../../tools/hw5330/README.md)）:

1. `build/ws099-p012-demo/hdd-image.img` を USB に書き、5330 を USB から起動する。電源を入れてから Kei の起動画面、greeter（か kei の desktop）までを見る。
2. desktop が出たら、App Home（左上）→ Log Out → greeter で kei を選び Enter（password 無し）→ desktop → もう一度 Log Out。
3. greeter の Shut Down を押し、「Shutting down...」の後に電源が切れるまでを見る。
4. 報告: 黒い画面・文字の画面が出たか（出たなら、どの替わり目で何秒くらい）、Shut Down から電源が切れるまでの秒数（切れなければ「切れない」）。

エージェントの確かめ（ユーザーが 3 の前に SSH を許すとき。5330 の IP と root の password `root` は README の手順で）:

```
ssh root@<5330の IP> "grep -E 'SESSIOND (GREETER|HANDOFF|CONSOLE)' /var/log/sessiond.log | tail -40" > build/ws099-p012/sessiond.txt
grep -c 'SESSIOND GREETER failed' build/ws099-p012/sessiond.txt
grep -c 'SESSIOND CONSOLE' build/ws099-p012/sessiond.txt
```

- 2 つの数がどちらも `0` なら BUG-122 の再発なし。sessiond の log はアプリの log であり、QEMU の console の log ではない。

## 完了の条件

- ユーザーの報告で、起動・login・Log Out・Shut Down の替わり目の黒い画面と文字の画面が 0 枚。
- Shut Down から電源断まで 10 秒以内（ユーザーの報告）。BUG-119 の ticket に「5330 で確認」を追記。
- `SESSIOND GREETER failed`・`SESSIOND CONSOLE` が 0 行（SSH が使えない場合は「未実施」と書く）。
- 結果を ws.md の Phase の表・段の表（L2 の C1）に記録。QEMU の証拠と実機の証拠を分けて書く。
- 1 つでも満たさなければ uncleared（理由・ユーザーの報告・次の担当の WS を書く）。
