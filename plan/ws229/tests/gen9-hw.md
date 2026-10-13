# Gen9 の実機の試験の手順（ws229-p005、ユーザーが 6・7・8 世代の laptop で行う）

2026-10-14 P1 が書いた。image は `plan/ws229/tests/gen9-images.sh` が作る（試験 1 つにつき image 1 つ）。結果は SSH で読み戻し、
`plan/ws229/tests/gen9-collect.sh` が判定の行を出す。network が無い時は画面の写真で判定する。

## 1. 何を確かめるか

Gen9（Skylake）と Gen9.5（Kaby Lake・Coffee Lake 系）の iGPU で、i915 の GT と EU が動き、Vulkan の試験の読み戻しが PASS すること
（ユーザーの決定 D1）。画面は firmware（GOP）の console のまま（display の driver は p006）。Gen9 は 2026-10-14 から既定で
有効（U1、実機 PASS の後。それまでは boot の指定 `i915.gen9=on` が要った）。image はこの指定を入れたまま（害は無い）。Gen9 の間は suspend を断る（U2）ので、蓋を閉じず AC 電源で行う。

| 順 | 試験（scenario） | 何を見るか | PASS の行 |
| --- | --- | --- | --- |
| 1 | `eu` | GT で 1 thread の EU の store（固定の Gen9 kernel）と、同じ batch の 5 回の再投入 | `i915: test eu: PASS`（と `i915: EU-TEST PASS:`） |
| 2 | `vkcs` | compute の pipeline と dispatch（zedBSD の EU compiler の Gen9 の出力） | `i915: vkcs: verdict PASS (n of n steps passed)` |
| 3 | `vkx` | Vulkan の executor（描画の基本） | `i915: vkx: verdict PASS (…)` |
| 4 | `vke1` | blend・uniform buffer・複数の sampled image | `i915: vke1: verdict PASS (…)` |
| 5 | `vke2` | 行列・整数・loop・16 の varying と attribute | `i915: vke2: verdict PASS (…)` |
| 6 | `vkc` | shader compiler の出力を GPU で | `i915: vkc: verdict PASS (…)` |

**1 台目は `eu` から。`eu` が PASS しない間は先に進まない**（後の試験は全部 `eu` の経路の上にある）。1 台で 6 つが PASS したら、
残りの世代の laptop で同じ順に流す（`eu` と `vkcs` だけでもよい。どこまでにするかは Q1 と相談）。

## 2. 用意する物

- laptop: 6 世代（Skylake、例 i5-6200U・6300U）、7 世代（Kaby Lake、例 i5-7200U・7300U）、8 世代（Kaby Lake R の i5-8250U・8350U
  か Whiskey Lake の i5-8265U・8365U）。どれも PCI ID は WS229 の表にある。
- USB メモリ（4 GB 以上。image は約 2.1 GiB で、書くと中身は全部消える）。
- **USB の有線 LAN の adapter**（CDC NCM・ECM のもの。5320 で使った RTL8156 は動いた）と DHCP のある network。laptop の内蔵の
  Ethernet（Intel I219）と内蔵の Wi-Fi は zedBSD に driver が無いので使えない。adapter が無ければ §5 の写真で行う。
- BIOS の設定: **Secure Boot を off**（zedBSD の loader は署名されていない）、UEFI の起動を on。Dell なら電源投入時の F2 で設定、
  F12 で起動の menu。

## 3. image を書く

Q1 が `build/ws229-gen9/<scenario>/hdd-image.img` を渡す（`gen9-images.sh` が作る。sha256 の先頭 16 桁を一緒に伝える）。

- Linux: `sudo dd if=hdd-image.img of=/dev/sdX bs=4M conv=fsync status=progress`（`/dev/sdX` は USB メモリ。`lsblk` で確かめる）。
- Windows: Rufus の「DD イメージ」mode、または balenaEtcher。

## 4. 起動して SSH で読み戻す

1. USB の LAN の adapter を挿し、AC 電源を繋ぎ、USB メモリから UEFI で起動する（F12 → USB の UEFI の項目）。
2. 画面には kernel の行が流れ、最後に console の login が出る（graphical login は無い）。GPU の試験は driver の起動の直後と、
   node の公開の後に走り、**起動から 3 分**で終わる。
3. laptop の IP を知る。DHCP の server（router）の表で見るか、console で `kei`（password `kei`）で login して `ifconfig` を打つ。
4. Q1 に「世代（例 8 世代 i5-8250U）、試験の名前、IP」を伝える。Q1（または T1）が centris から次を走らせる:

   ```sh
   plan/ws229/tests/gen9-collect.sh <IP> <scenario> <label>     # 例: gen9-collect.sh 10.0.30.7 eu kbl-r-8250u
   ```

   `ssh kei@<IP>`（password `kei`）で `/var/log/kernel.log` を読み、`build/ws229-gen9/logs/<scenario>-<label>.log` に保存し、
   GPU の行・試験の行・fault の行と、最後に `gen9-collect: <scenario> on <label> PASS` か `FAIL` を出す。
5. 自分で見る時は、SSH か console で:

   ```sh
   grep -aE 'i915: (test |EU-TEST (PASS|FAIL|HANG|ERROR)|[a-z0-9]+: verdict)' /var/log/kernel.log
   lspci | grep -i 8086        # GPU の PCI ID（記録用）
   ```

6. 終わったら `sudo poweroff`（または電源ボタンの長押し）。次の試験の image を USB に書き、1 から繰り返す。

判定: §1 の PASS の行があれば PASS。`FAIL`・`HANG`・`ERROR`、3 分経っても試験の行が無い、`panic` の行、画面が止まる、のどれかなら FAIL。
FAIL の時は log（`gen9-collect.sh` が保存した物）を Q1 が解析に回す。GPU が hang しても試験は engine を reset して node を出し続ける
ので、多くの場合 SSH は届く。届かなければ §5。

## 5. network が無い時（写真）

1. 起動の 3 分後、console で `kei`／`kei` で login する。
2. `grep -aE 'i915: (test |EU-TEST (PASS|FAIL|HANG|ERROR)|[a-z0-9]+: verdict)' /var/log/kernel.log` を打ち、画面を撮る。
3. FAIL なら続けて `grep -a 'i915:' /var/log/kernel.log | tail -40` も撮る。
4. 写真を Q1 に渡す。

画面が固まって login できない時は、固まった画面をそのまま撮る（kernel の行が出ている）。

## 6. 記録（Q1 が phase.md に書く）

世代と CPU の型番、GPU の PCI ID、試験ごとの PASS／FAIL、log の path（または写真）、image の sha256、main の commit。実機の証拠として
QEMU の証拠と分けて書く。
