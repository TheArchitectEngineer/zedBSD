<!-- awesome-plan project=zedbsd record=ws094-p001 -->

# ws094-p001: 設計

Status: cleared（2026-09-29）
Disposition: normal
Parent: [WS094](../ws.md)
Queue: main の依頼（2026-09-29、worktree `.claude/worktrees/ws094-desktop`、branch `wt/ws094`、main cdde9518 から）

## 範囲と受け入れ

desktop の file の icon の設計を [design.md](../design.md) に書く: 誰が描くか、`~/Desktop` の監視、配置の保存、double click で開く（WS093）、
右 click の menu、Files との DnD、touch、WS090 との関係、Phase への分け方。source は変えない。

## 結果

- 調査（design §1）: compositor は layer-shell を持たない独自の実装で、壁紙と App Home を自分で描く。session.sh は compositor に exec する。
  Files は窓ごとの process で、開く・Always Open With・名前・Trash・Undo・context menu・DnD・touch の model を持つ。仮想 desktop が 4 つ。
- 決めたこと（既定、design §8 の J1〜J7）: **Files が背景の層の client として描く**（新しい protocol `keiland_desktop_v1`、compositor が
  `files --desktop` を token 付きで起こし、壁紙の上・全ての窓の下・4 つの仮想 desktop で同じ 1 枚）。右上から並べ、配置は
  `~/.config/keiland/desktop-layout`。WS090（libkeiui）は待たず、Files の Phase と WS090 の p009・p010 を重ねない。
- Phase: p002（compositor）→ p003（libkeiland の client と Files の骨組み）→ p004（選択・開く・保存）→ p005（menu・名前・Trash）→
  p006（drag・DnD・touch）→ p007（規約と回帰）。

## 見直し（誤り・欠落・矛盾）

初稿を見直して次を直した:
- token が Files から起こす app に継がれ、desktop が落ちた隙に role を取れる → Files が起動の直後に環境から消す（§3）。
- desktop に focus がある時の System Menu、最後の窓が閉じた時の focus が決まっていなかった → compositor の既定、click の時だけ（§3）。
- context menu の popup の親と DnD の対象に desktop surface を含めることが抜けていた → p002 に入れた（§3）。
- 白い文字に影は Kei の明るい壁紙で読みにくい → slate の文字に白い縁取り、選択は青の pill（§4）。
- App Home・Wiseview・lock・greeter の間の入力と描画が決まっていなかった → それらが取り、lock と greeter の間は描かない（§3）。
- 確かめ方が無かった → QEMU の Venus の guest と probe、pen の image、WS035 の回帰の該当（§7）。

## 判断が要る点（main・ユーザー）

- J1〜J7 は既定を選んだ（design §8）。変えるなら p002 の前に。
- **compositor（WS035 の source）と libkeiland（WS092・WS090 も触る）を WS094 の Phase で変えることの許可と、同じ file を同時に変えない順**（design §9）。

## 確認

設計の Phase のため build・試験は無し。source は変えていない。

## Resume point

p002（compositor の desktop surface）から。main の許可の後。
