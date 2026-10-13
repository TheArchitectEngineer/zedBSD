# Gen9 の実機の試験の手順（ws229-p005、ユーザーが 6・7・8 世代の laptop で行う）

2026-10-14 P1 が書き、同じ日に P3 が「1 回書いて 1 回起動」に直した（ユーザー「ws229は全部入りのテストを作ってください。」）。
image は `plan/ws229/tests/gen9-images.sh` が作る **1 つだけ**で、1 回の起動で全部の試験を順に流す。ユーザーが手で USB から
起動する laptop（6・7 世代など）のための手順である。8 世代の Let's Note SV7 は 5330 と同じ passthrough の自動の試験（試験ごとの
scenario、P1 が用意する）で行い、この image は使わない（2026-10-14 ユーザー）。結果は SSH で読み戻し、
`plan/ws229/tests/gen9-collect.sh` が判定の行を出す。network が無い時は画面の写真で判定する。

## 1. 何を確かめるか

Gen9（Skylake）と Gen9.5（Kaby Lake・Coffee Lake 系）の iGPU で、i915 の GT と EU が動き、Vulkan の試験の読み戻しが PASS すること
（ユーザーの決定 D1）。画面は firmware（GOP）の console のまま（display の driver は p006）。Gen9 は boot の指定 `i915.gen9=on` で
有効にする（既定は off、U1）。image はこの指定を入れてある。Gen9 の間は suspend を断る（U2）ので、蓋を閉じず AC 電源で行う。

1 回の起動で、次の 6 つが上から順に走る（scenario `gen9`、`src/drivers/gpu/i915/tests/execution/sequence.c`）。

| 順 | 試験 | 何を見るか | その試験の行 |
| --- | --- | --- | --- |
| 1 | `eu` | GT で 1 thread の EU の store（固定の Gen9 kernel）と、同じ batch の 5 回の再投入。node の公開の前に走る | `i915: EU-TEST PASS:` |
| 2 | `vkcs` | compute の pipeline と dispatch（zedBSD の EU compiler の Gen9 の出力） | `i915: vkcs: verdict PASS (n of n steps passed)` |
| 3 | `vkx` | Vulkan の executor（描画の基本） | `i915: vkx: verdict PASS (…)` |
| 4 | `vke1` | blend・uniform buffer・複数の sampled image | `i915: vke1: verdict PASS (…)` |
| 5 | `vke2` | 行列・整数・loop・16 の varying と attribute、compiler の境界（最後の UNDEF は GPU を止めうる） | `i915: vke2: verdict PASS (…)` |
| 6 | `vkc` | shader compiler の出力を GPU で | `i915: vkc: verdict PASS (…)` |

- 前の試験が FAIL しても次へ進む。
- `eu` が FAIL（hang を含む）したら、全部の engine を 1 つずつ reset して再開させてから 2 以降を流す。再開できなければそこで止め、
  2〜6 は `NOT RUN` になる。
- 2〜6 のどれかが render engine を hang させたら（試験の後 3 秒で idle にならない）、driver に公開後の hang の回復が無いので、
  その後の試験は `NOT RUN` になる（理由が log に出る）。
- 最後に試験ごとの要約の行と全体の判定の行が出る:

  ```text
  i915: gen9 summary: eu PASS
  i915: gen9 summary: vkcs FAIL rc=5 (see its verdict line)
  i915: gen9 summary: vke2 NOT RUN (the render engine hung in an earlier scenario)
  …
  i915: gen9: verdict PASS (pass=6 fail=0 skip=0 not_run=0)
  ```

  **全体の判定は `i915: gen9: verdict PASS` の時だけ PASS**（6 つ全部 PASS）。1 台で PASS したら、残りの世代の laptop で同じ
  image を流す。

## 2. 用意する物

- laptop: 6 世代（Skylake、例 i5-6200U・6300U）、7 世代（Kaby Lake、例 i5-7200U・7300U）、8 世代（Kaby Lake R の i5-8250U・8350U
  か Whiskey Lake の i5-8265U・8365U）。どれも PCI ID は WS229 の表にある。
- USB メモリ（4 GB 以上。image は約 2.1 GiB で、書くと中身は全部消える）。
- **USB の有線 LAN の adapter**（CDC NCM・ECM のもの。5320 で使った RTL8156 は動いた）と DHCP のある network。laptop の内蔵の
  Ethernet（Intel I219）と内蔵の Wi-Fi は zedBSD に driver が無いので使えない。adapter が無ければ §5 の写真で行う。
- BIOS の設定: **Secure Boot を off**（zedBSD の loader は署名されていない）、UEFI の起動を on。Dell なら電源投入時の F2 で設定、
  F12 で起動の menu。

## 3. image を書く（1 回）

Q1 が `build/ws229-gen9/hdd-image.img` を渡す（`gen9-images.sh` が作る。sha256 の先頭 16 桁を一緒に伝える）。手で起動する laptop は
どの世代も同じ image でよい。

- Linux: `sudo dd if=hdd-image.img of=/dev/sdX bs=4M conv=fsync status=progress`（`/dev/sdX` は USB メモリ。`lsblk` で確かめる）。
- Windows: Rufus の「DD イメージ」mode、または balenaEtcher。

## 4. 起動して（1 回）SSH で読み戻す

1. USB の LAN の adapter を挿し、AC 電源を繋ぎ、USB メモリから UEFI で起動する（F12 → USB の UEFI の項目）。
2. 画面には kernel の行が流れ、最後に console の login が出る（graphical login は無い）。`eu` は driver の起動の直後に、2〜6 は
   node の公開の後に続けて走る。終わりの印は `i915: gen9: verdict` の行（時間は未計測。hang があると 1 step につき 10 秒待つ
   ので長くなる）。
3. laptop の IP を知る。DHCP の server（router）の表で見るか、console で `kei`（password `kei`）で login して `ifconfig` を打つ。
4. Q1 に「世代（例 8 世代 i5-8250U）と IP」を伝える。Q1（または T1）が centris から次を走らせる:

   ```sh
   plan/ws229/tests/gen9-collect.sh <IP> <label>     # 例: gen9-collect.sh 10.0.30.7 kbl-r-8250u
   ```

   `ssh kei@<IP>`（password `kei`）で `/var/log/kernel.log` を、verdict の行が出るまで 30 秒ごとに読み直し（既定で最大 900 秒、
   `WAIT=` で変える）、`build/ws229-gen9/logs/gen9-<label>.log` に保存し、GPU の行・試験の行（要約と判定）・fault の行と、
   最後に `gen9-collect: <label> PASS` か `FAIL` を出す。
5. 自分で見る時は、SSH か console で:

   ```sh
   grep -aE 'i915: (gen9[: ]|EU-TEST (PASS|FAIL|HANG|ERROR)|[a-z0-9]+: verdict)' /var/log/kernel.log
   lspci | grep -i 8086        # GPU の PCI ID（記録用）
   ```

6. 終わったら `sudo poweroff`（または電源ボタンの長押し）。

判定: `i915: gen9: verdict PASS` があれば PASS。`verdict FAIL`、verdict の行が出ない、`panic` の行、画面が止まる、のどれかなら FAIL。
どの試験が落ちたかは要約の行で分かる。FAIL の時は log（`gen9-collect.sh` が保存した物）を Q1 が解析に回す。render の試験が hang
しても node は出たままなので、多くの場合 SSH は届く。届かなければ §5。

## 5. network が無い時（写真）

1. 画面に `i915: gen9: verdict` の行が出たら（出ない時は起動の 15 分後）、console で `kei`／`kei` で login する。
2. `grep -a 'i915: gen9' /var/log/kernel.log` を打ち、画面を撮る（要約の 6 行と判定の行）。
3. FAIL なら続けて `grep -aE 'i915: (EU-TEST (PASS|FAIL|HANG|ERROR)|[a-z0-9]+: verdict)' /var/log/kernel.log` と
   `grep -a 'i915:' /var/log/kernel.log | tail -40` も撮る。
4. 写真を Q1 に渡す。

画面が固まって login できない時は、固まった画面をそのまま撮る（kernel の行が出ている）。

## 6. 記録（Q1 が phase.md に書く）

世代と CPU の型番、GPU の PCI ID、要約の行（試験ごとの PASS／FAIL／SKIP／NOT RUN）と判定の行、log の path（または写真）、image の sha256、main の commit。実機の証拠として
QEMU の証拠と分けて書く。
