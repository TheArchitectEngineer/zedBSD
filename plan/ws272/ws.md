<!-- awesome-plan project=zedbsd record=ws272 -->

# WS272 MIPS64（mips64el）の QEMU malta への移植（framebuffer の console・VFS の段まで）

- ID: ws272、Master: [master.md](../master.md)、Primary Milestone: MG008
- Status: incomplete（p001 cleared 2026-10-19。旧: planning（2026-10-19 14:13 Q1 作成）
- 起点（2026-10-19 ユーザー）: 「MIP32とMIPS64のカーネルも作りたいです。ただ、ボードがないなあ。virtで作れそうですか？Anbernic RG 350と、Cobalt Qube 初代、EdgeRouter X はあります。MIPS Creatorは購入できそう。」→（クリック）始め方「malta の little endian」、toolchain「許可する」（LLVM に Mips の target、Q1 が行う）、最初の実機「RG350」
- 前提: QEMU の `qemu-system-mips64el -M malta（MIPS64r2 の CPU、little endian）`。画面は malta の PCI の VGA（Bochs の linear framebuffer）を HAL が設定して console を描く案（p001 で確かめる）。共有の LLVM に Mips の target が要る（Q1、ユーザーの許可 2026-10-19）。WS263〜WS266（riscv32・ppc32・ppc64・m68k）と同じ到達点と規則。
- 実機の候補（2026-10-19 14:14 ユーザー）: 「ちなみにQubeはPCI USBボードが使える状態です。DisplayLinkでX11が動いた実績はあります。今回は画面はスコープ外でいいですが。」 → Cobalt Qube（初代、QED RM5230、MIPS IV、little endian、Q1 の覚えで未確認）は PCI の USB の板が使える。Qube の画面（DisplayLink）は範囲外。Qube の実機の WS は着手の時に立てる。
- 浮動小数点（2026-10-19 14:15 ユーザー）: 「Octeonも入手可能です。softfloatをオプションで選べるようにしたいですね。まずはhardfloatでOKです。」 → まず hardfloat（FPU を使う ABI）。softfloat は後で build の option として選べる形にする（p001 の設計で option の置き場所を決めておく）。
- 実機の候補（2026-10-19 14:15 ユーザー）: Cavium Octeon も入手できる（MIPS64、big endian が多い。機種と endian は未確認）。
- Octeon の機種（2026-10-19 14:17 ユーザー）: 「Octeon は EdgeRouter Lite-3が入手できそうです。OcteonはTLB操作のSDKがありますが、そんなに非互換が強いんですかねえ。」 → Q1 の覚え（未確認）: EdgeRouter Lite-3 は Cavium Octeon Plus CN5020（cnMIPS64 の 2 core、big endian、U-Boot、内部の USB 記憶）。TLB は MIPS64r2 の標準の命令で扱える見込み。訂正（2026-10-19 14:19 ユーザー 「Linux も FreeBSD も、10年前に私が確認したときにはCaviumのSDKをマージしてました。今はどうかわかりません。」）: Linux は arch/mips/cavium-octeon/executive/ に Cavium の SDK（cvmx-bootmem・cvmx-sysinfo・cvmx-l2c・helper・PKO など、SoC の資源の調整と network）を取り込んでいた（v3.1 などの tree で確認）。今の mainline と FreeBSD の現状は未確認。SDK に頼る範囲と出典の license は着手の時の設計で確かめる。差の大きい所は FPU が無い（hardfloat の userland には kernel の FPU の emulation か softfloat の build が要る）、big endian、CIU の割り込み・Ethernet などの Octeon 固有の周辺、U-Boot の起動の情報の形。
- 設計（2026-10-19 P1、ws271-p001）: 共通の部分（QEMU malta の事実、toolchain、HAL の共通の層 `src/hal/mips/`、試験）と WS272 の差分は [WS271 の design.md](../ws271/design.md) の §1〜§6・§8。p001 は §8 の確認の計画だけ（LW 2）。QEMU の CPU は `MIPS64R2-generic` を案に（既定の 20Kc は R1、U4 でユーザーに聞く）。
- 決定（2026-10-19 14:29 ユーザー、クリック）: WS271 の判断（クリック）: U1 HAL の差分は「同じ形で事前承認」（WS263〜266 と同じ形、plan/ws271/proposed/hal-mips.md）、U2 toolchain は「LLVM の Mips を待つ」、U3 MIPS32 の hardfloat の ABI は「FPXX（FR=1）」、U4 MIPS64 の QEMU の CPU は「MIPS64R2-generic」。U5 画面は Q1 が既定の 640x480。
- 設計の改訂 2（2026-10-19 P1、q1277）: design-review-1 の M8（同じ RAM が物理 0 と `0x80000000` の 2 つを持つ）を [design.md](../ws271/design.md) §8 で確定: 正の物理の範囲は low の alias（0〜min(RAM,256MiB)）と `0x80000000+256MiB`〜`0x80000000+RAM`、image と ENVP は low 側で予約。
- 担当: 未割当（WS256 の後か、手の空いた時）。

## 完了の条件（案、p001 の設計で確定）

1. QEMU の上で kernel が起動し、framebuffer（画面）に console が出る。
2. kernel が HAL の初期化を終え、VFS の段まで進み、root が無いので mount できずに止まる。判定は boot-test.sh の画面（serial の log で判定しない）。
3. HAL の API の追加・変更（`include/hal/arch/*.h` など）は、差分ごとにユーザーの事前承認の後に入れる（[Guardrail](../guardrail.md)）。HAL → kernel は汎用の構造体と名前ごとの値だけ。
4. 新しい code は plan/coding-style.md の全文に合う。外部の code の参照は WS245 と同じ規則。

## Phase（案）

| Phase | 目的 | 状態 |
| --- | --- | --- |
| p001 | 設計: malta の memory map・PCI（GT-64120）・PIIX4・VGA、firmware（YAMON 相当の -kernel の直の起動）と handoff、mips64el の toolchain の triple と linker の emulation、HAL の構成、HAL の API の差分、試験の道具（boot-test.sh の mode）、Phase の表 | cleared（2026-10-19 Q1） |
| p002 | 64 bit の入口と console（共通の層を `HAL_ARCH_MIPS64` で、XKPHYS・CKSEG0、design.md §8 の正の物理の範囲（low の alias ＋ `0x80000000+256MiB` 以上）。依存: WS271 p002、LLVM の Mips。HAL の差分 D1・D3 は U1 で承認済み）、LW 8 | in-progress（q1286、P1、2026-10-19。実装 974ee1728、T1 の画面の確認待ち、[phase002](phase002/phase.md)） |
| p003 | XTLB refill・3 段の table・native atomic、self-test の 64 bit の版、VFS の段まで（依存: WS271 p004）、LW 10 | planning |
| p004 | 規約の全文の見直し、LW 3 | planning |

## 依存

- LLVM の Mips の target（Q1、[WS273](../ws273/ws.md) と共通）。
