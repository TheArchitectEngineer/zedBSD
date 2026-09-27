# WS035 設計: グラフィカルログインマネージャ（検討、ws035-p082）

2026-09-27 ユーザー:「WS035に、グラフィカルログインマネージャの検討を追加してください。Waylandではなくて、Vulkanを直接叩くのかなあ。」
この文書は検討（設計のみ）。実装はしない。決定と書いたものも、ユーザーの判断が要る点（§8）が決まるまでは案である。

## 1. 今の事実（2026-09-27 のコード）

| 事柄 | 今 | 場所 |
| --- | --- | --- |
| login | root で動き、`getpwnam_r`・`getspnam_r`・`crypt` で照合（空の password、`!`・`*` の無効）、`fork` して `initgroups`・`setgid`・`setuid`、`HOME`・`USER`・`LOGNAME`・`PATH`・`SHELL` の環境で shell を `exec`、utmpx に USER_PROCESS・DEAD_PROCESS。PAM は無い | `userland/base/login/main.c` |
| 起動 | init が `rc.conf` の service を起こす（`getty_console`・`sshd` 等）。zdesktop は service ではなく、試験では root の shell から手で起こす | `userland/base/etc/rc.conf` |
| 表示 | `/dev/gpu0` の `GPU_DISPLAY_CLAIM` の lease（open ごと、open をまたげない）。lease を持つ open が閉じると kernel の文字 console が戻る。libvulkan の `VK_KHR_display` が lease を取る（zdesktop のウィンドウモード） | [graphics-design.md](graphics-design.md) §5、`src/drivers/gpu/`、`userland/base/libvulkan/wsi-display*.c` |
| device の権限 | devfs の既定: `/dev/input/event*` は 0640（root:wheel）、他の cdev（`/dev/gpu0` を含む）は 0666。`chown`・`chmod` は devfs に記録され、同じ名前の node の作り直しにも残る | `src/kern/devfs.c` |
| Wayland の socket | zdesktop は既定で `/tmp/wayland-0`（`--socket=` で変えられる） | `userland/base/zdesktop/main.c` |
| VT | 無い。表示の持ち主が居ない間は kernel の文字 console が画面に出る | |
| GPU | Venus（QEMU）と i915（実機）の両方が libvulkan の同じ WSI を通る。Venus は READY の時機が遅れることがある | |

## 2. ユーザーの仮説「Wayland ではなく Vulkan を直接」の意味

意味は 2 つに分かれる:

1. **login の画面の描画を Wayland の client にしない**（GDM・SDDM のような「compositor の上の greeter client」にしない）。
   → **賛成（推奨）**。理由: login の時点で Wayland の server（protocol の解析、client の画像の import）を立てる必要が無く、
   攻撃面と部品が減る。画面は 1 枚で、窓の管理が要らない。
2. **描画の道具が Vulkan の `VK_KHR_display`**（zdesktop と同じ）。→ 賛成。zdesktop の glass の描画（panel.frag、glyph、
   wallpaper、backdrop のぼかし）をそのまま使えば、login の画面と desktop の見た目が揃い、login から desktop への移りを
   同じ絵の続きにできる。CPU の framebuffer（`/dev/graphics`）でも描けるが、見た目を揃えるなら Vulkan。

ただし「greeter が root で Vulkan を直接叩く」形は避ける（§4）。libvulkan と GPU の userland driver、font の rasterizer、画像の
decoder は大きく、root で動かすと、そのどれかの欠陥が即 root の欠陥になる。

## 3. 案の比較

| 案 | 形 | 利点 | 欠点 |
| --- | --- | --- | --- |
| A | 独立の小さな greeter（`zdesktop-greeter`）が `VK_KHR_display` で直接描く | 部品が少ない | zdesktop の描画・入力・glyph を写すか共有の library に切り出す仕事が要る。見た目が別の実装になりやすい |
| **B（推奨）** | **zdesktop の greeter mode**（`zdesktop --greeter`）: 同じ binary が Wayland の socket を開かず、login の画面だけを描く。認証は別の小さな root の process（`zsessiond`）に socket で頼む | 描画・入力・glyph・wallpaper・ぼかし・animation を共有。login → desktop の移りを同じ絵の続きにできる。Wayland を開かない | zdesktop に mode が 1 つ増える（`main.c` の起動の分岐と、login の画面の描画 1 file） |
| C | 最小の zdesktop（kiosk）の上に Wayland の greeter client | 一般的（GDM・SDDM の形）。greeter を他の toolkit で書ける | login の時点で Wayland の server が要る（部品・攻撃面が最大）。ユーザーの仮説の 1 に反する |

**推奨は B**。A は B の「別 binary」版で、共有の library（描画の core）を切り出した後なら選べる（Future Work）。

## 4. 権限の分け方（推奨の形）

```
init ── zsessiond (root、小さい: 認証・device の持ち主・session の起動)
          │ socketpair（1 行の要求と答え）
          ├── zdesktop --greeter   (uid _greeter、表示と入力の device だけを持つ)
          └── (成功後) user の session: zdesktop (uid user) と、その client
```

- **zsessiond**（新、root）: `rc.conf` の service `greeter`。やることは (1) seat の device（`/dev/gpu0`、`/dev/input/event*`）の
  持ち主を「今の seat の user」に `chown`・`chmod 0600`、(2) greeter を `_greeter` として起こす、(3) greeter からの
  `AUTH user password` を `login` と同じ照合（`getspnam_r`・`crypt`、失敗ごとに 2 秒の遅れ、連続の失敗で遅れを延ばす）で答える、
  (4) 成功で greeter を終わらせ、device を user へ移し、`login` と同じ手順（`initgroups`・`setgid`・`setuid`、環境、utmpx）で
  user の `zdesktop` を起こす、(5) session の終わり（zdesktop の終了）で device を `_greeter` に戻し greeter を起こし直す、
  (6) halt・reboot の要求（greeter の電源の button）を受ける。**描画・font・画像・Vulkan を一切持たない**（数百行の C）。
- **greeter**（`zdesktop --greeter`、uid `_greeter`）: wallpaper とぼかし、時計、user の一覧（passwd の uid 1000 以上で login shell が
  `nologin` でない人）、password の欄、電源の button。Wayland の socket を開かない。password は zsessiond へ送ったらすぐ消す。
- **user の session**: 今の zdesktop をそのまま user の uid で。`XDG_RUNTIME_DIR` は zsessiond が user だけの directory（0700、user の持ち物。
  置き場は g3 で決める）を作って渡し、Wayland の socket はその中（`/tmp/wayland-0` の名前の奪い合いを避ける）。

**login との共有**: 照合の関数（shadow の読みと crypt と無効の印）を `login` と zsessiond で重ねて書かないよう、libc の内部か小さな
共有の source に切り出す（実装の Phase で決める）。

## 5. 表示の引き継ぎ（greeter → session → greeter）

- **最初の形（一通り）**: greeter が終わり lease が返ると kernel の文字 console が一瞬出て、user の zdesktop が claim すると消える。
  画面は「console の一瞬」を挟むが、動く。
- **継ぎ目の無い形（後）**: zsessiond が `/dev/gpu0` を open して lease を取り、その fd を greeter に、次に session の zdesktop に
  渡す（fork・exec での継承か SCM_RIGHTS）。lease は open file に付くので console に戻らない。要るもの:
  1. libvulkan が「既に開いた GPU の fd と lease」を使う入口（今は `/dev` を自分で列挙して open する）。WS068・libvulkan の持ち主と
     相談（`VK_KHR_display` の extension の外の zedBSD 固有の入口になる）。
  2. **取り上げ（revoke）**: greeter が fd を複製して持ち続けると、session の間も画面を覗ける・描ける。kernel に「open file の
     lease と入力の読みを失効させる」操作が要る（§7-1）。
- zdesktop の側では、greeter の最後の frame（ぼかした wallpaper）と session の最初の frame（desktop）を同じ wallpaper にすると、
  継ぎ目が見えにくい。

## 6. 入力

- `/dev/input/event*` は 0640 root:wheel。greeter・session が読むには zsessiond が seat の user へ `chown` する。hotplug の新しい
  node は既定の持ち主で現れるので、zsessiond が `/dev/input` を見張り、現れた node も移す（devfs の記録で名前の再利用にも残る）。
- keyboard の配列: greeter は zdesktop の US 配列（IME 無し、Future Work）。password の欄は表示しない文字（●）。
- 緊急の出口: greeter・zsessiond が N 回続けて起動に失敗したら、zsessiond は表示を離して終わり、init は `getty_console` の
  文字の login に戻る（`rc.conf` の fallback）。serial・ssh の login は常に残る。

## 7. Security の要点

1. **取り上げ（revoke）が無い**: 今の kernel では、前の持ち主が開いた `/dev/input/event*`・`/dev/gpu0` の fd は `chown` の後も
   使える。user A の session の残りの process が user B の session の key を読める（keylogger）・画面を取れる。
   **複数の user が使う前に kernel の revoke が要る**（device の世代を上げ、古い open を EIO にする等）。これは kernel の
   変更で、この WS の外（HAL の API ではないが kernel の設計の判断）。§8-2。
2. `/dev/gpu0` が 0666: 今は誰でも表示を claim できる（local の user が画面を乗っ取れる）。seat の user だけ（0600）にする。§8-1。
3. greeter は root で動かさない（§4）。zsessiond は描画・parse の大きな部品を持たない。
4. password: greeter の memory から送った直後に消す。zsessiond も照合の後に消す（login と同じ）。log に書かない。
5. 失敗の遅れ（2 秒、続けば延ばす）、utmpx・syslog に成功と失敗の記録。
6. Wayland の socket を user の `XDG_RUNTIME_DIR`（0700）に（`/tmp` の名前の奪い合いと他の user の接続を防ぐ）。
7. 画面の lock（後）: zdesktop の lock の画面が同じ zsessiond の `AUTH` を使う（session の中の zdesktop は user の uid なので、
   自分で shadow を読めない。zsessiond の socket が要る）。

## 8. 判断が要る点（既定の案を書く）

1. **`/dev/gpu0` の既定の権限**: 案: zsessiond が動くとき seat の user の 0600。zsessiond が無い構成（今の試験・手で zdesktop を
   起こす）は今の 0666 のまま。
2. **kernel の revoke**: 案: 複数の user の login を許す前の前提にし、それまでの graphical login は「1 人の user の機械」の用途に
   限ると明記する。kernel の Phase（別 WS）として起こすかを main が決める。
3. **既定の login**: 案: `rc.conf` の `greeter` は既定で無効（`enabled: false`）。有効にすると `getty_console` の代わりに画面を持つ。
   serial・ssh は変えない。
4. **自動 login**: 案: 設けない（最初は）。
5. **greeter の置き場**: 案 B（zdesktop の mode）。A（別 binary）にするなら描画の core の共有 library を先に切り出す。

## 9. Phase の案（実装は別の承認で）

| 案 | 中身 | 依存 |
| --- | --- | --- |
| g1 | zsessiond（認証・device の chown・session の起動と終わり・fallback）と、描画の無い試験用の greeter（文字の要求を送る） | §8-1、§8-3 |
| g2 | `zdesktop --greeter`（wallpaper・時計・user の一覧・password の欄・電源、socket を開かない）、QEMU で login から desktop まで | g1 |
| g3 | `XDG_RUNTIME_DIR` の用意と socket の置き場、session の終わりから greeter へ戻る | g1 |
| g4 | 継ぎ目の無い引き継ぎ（lease の fd の受け渡し、libvulkan の入口） | g2、libvulkan の持ち主と相談 |
| g5 | 画面の lock（zsessiond の AUTH を session から） | g1 |
| k | kernel の revoke（別 WS） | §8-2 |

試験: QEMU（Venus）で zsessiond を起こし、greeter の画面を撮る（QMP）、user を選び password を打ち（qmp-keys）、desktop が user の
uid で動くこと（`ps`）、log out で greeter に戻ること。i915 実機は任意。

## 10. 決定と実装（2026-09-28）

ユーザーの承認（2026-09-28）「ログインマネージャーの提案は承認します。1点だけ、コンソールログインでなくグラフィカルログインを
デフォルトにします。ブートローダにロゴを表示させます。カーネルパラメータでメッセージをコンソールに出さずにdmesgのような方法で
保存だけする指定をします。これにより完全なグラフィカル起動を実現します。これはデフォルトではありますが、カーネル自体の開発の
ときは無効にして、コンソールにメッセージを表示させ、コンソールログインにします。ブートローダはppmのようなシンプルな画像
ファイルを読みます。」

- §8-1〜8-2・8-4〜8-5 は案のとおり。§8-3 は変更: **グラフィカルログインが既定**（rc.conf の `greeter` は enabled、
  image の zedbsd.cfg に `login=graphical`）。「1 人の user の機械」に限る（revoke は別の WS）。
- 実装: p094（zsessiond、g1・g3）、p095（`zdesktop --greeter`・`--session`、g2。GPU の open を root か device の持ち主に）、
  p096（UEFI loader の PPM の logo）、p097（`kmsg=quiet`、lease の間の keyboard。HAL の早期 console は提案
  [proposed/hal-quiet-console.md](proposed/hal-quiet-console.md)）、p098（既定と切り替え: init の `replaces=`、
  `login=`・sysctl `kern.boot.login`、`ZEDBSD_GRAPHICAL_BOOT`（既定 y、kernel の開発は n）、試験の構成は n）。
- 残り: g4（継ぎ目の無い引き継ぎ）、g5（画面の lock）、kernel の revoke、BIOS の loader の logo。
