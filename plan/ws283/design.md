# WS283 の設計: bcm2711 の Vulkan の compute（V3D 4.2・7.1）

- 2026-10-23 P2（ws283-p001）。**第 2 版**: 第 1 版（`3d515b76e`）への design-reviewer の review（[phase001/review-20261023.md](phase001/review-20261023.md)、High 6・Medium 10・Low 7）の全部の指摘と、ユーザーの D1〜D4 の回答、Q1 の判断（D5・H1・M5）を入れた。指摘ごとの扱いは [phase001/phase.md](phase001/phase.md) の表。実装はこの文書の判定の後。
- 出典の記号: **M** = Mesa 25.3.6（MIT、`src/broadcom/`、作業の写しは commit しない）。**Z** = zedBSD の今の tree（main `61832637b`）。行の番号は M の 25.3.6 の物。
- license: M は MIT。register の値・bit の位置・命令の形・hazard の規則は M から事実として取り、文と code は自分で書く（M の file を写さない）。GPL の Linux の v3d は読んでいない。
- 推測と確かめた事を分けて書く。「推測」と書いた物は M の code・注記か Z の code からの推し量りで、文書・実機では確かめていない。各推測には確かめる試験（§3）を付けた。

## 0. まとめ

- **共有の前段（SPIR-V → IR）は compute の大部分を持つ**（Z `src/drivers/gpu/compiler/spirv-compute.inc`）: LocalInvocationId・WorkgroupId・NumWorkgroups・GlobalInvocationId、storage buffer の load・store・atomic、Workgroup の共有 memory、barrier、loop。**ただし 2 つ欠ける**: (1) `OpSpecConstant*` は前段に無く全 driver が断る（H1）。Q1 の判断で WS284 は spec constant を使わない（build の時の variant と push constant）ので、WS283 は今は足さない（§5 の後の項目）。(2) `NonWritable` は前段が捨てる（Z `spirv.c` 1813 行）。ldunifa（p011）に要るので、その Phase で前段に印を 1 つ足す（Q1 の了承が要る、§5）。
- **後段（IR → QPU）の欠けが一番大きい**: 分岐が全く無い（loop が通らない）、整数の乗算・比較・除算が無い、TMU の一般の読み書きが無い、**scheduler は fragment 専用**で compute は 1 語 1 op の source 順になる（Z `shader.c` 560〜577 行、H2）。block を持つ一般の scheduler に広げる仕事を独立の Phase にした。
- **安全の不変条件**: GPU の page table は全 session で 1 つ（Z `mmu.c` 9 行）。shader が計算する全ての address（storage・shared・ldunifa・vector）を binding の範囲に clamp する（`base + umin(offset, limit)`、robustBufferAccess が許す形）。省く最適化はしない（H5、§1.3）。
- **実行器**: queue の COMPUTE の flag、storage buffer の descriptor、compute pipeline、bind point、`vkCmdDispatch` → CSD の job。flag は build の設定 `CONFIG_DRIVER_BCM2711_COMPUTE`（既定 n）の下に置き、Pi 5 の実機の確かめが済んだ後の Phase で既定を y にする（M5、§2.6）。
- **進め方**: 7.1（Pi 5、唯一の実機と受け入れの対象）を先に作り、版に依らない部分は共有の file にし、4.2 は後の port の Phase で作る（M9、§1.12）。executor の直後に最初の実機の確かめを置く。
- 見込み: p002〜p014 で **77〜121 LW**（第 1 版の 51〜77 LW は、scheduler・lane の model・試験の program・4.2 の port を数えていなかった）。YOLOX-Nano 416×416 fp32 は Pi 5 で、block の scheduler と ldunifa・vector の load の後に **40〜120 ms（8〜25 fps）**、source 順のままなら 2〜4 倍遅い（§3.3、clock と TMU の速さが未知）。

## 1. compiler

### 1.1 今の形（Z、第 1 版の誤りを直した）

共有の前段 → `shader-analyze.c`（検査・生存区間）→ `shader-lower.c`（4.2）/`shader71-lower.c`（7.1）→ `shader.c`（記録・register の割り当て）→ `shader-schedule.c`（4.2、2360 行）/`shader71-schedule.c`（7.1、2068 行）→ `qpu.c`/`qpu71.c`（encoder）。stage は coordinate・vertex・fragment。`shader.c` の preflight は GLCompute の entry と Workgroup の storage class を断る。

- **scheduler は fragment だけ**: `bcm2711_shader_emit` は stage が FRAGMENT の時だけ `bcm2711_shader_schedule` を呼ぶ。他の stage と、scheduler が断った program は 1 語 1 op の source 順で、遅延は保守の NOP（`shader.c` 560〜577 行）。scheduler は tile の書き込みの tail（`schedule_tail`）と 1 つの直線の block を前提にし、規則の外の transaction があると黙って source 順に落ちる（`shader-schedule.c` 8〜20 行）。
- **register**: expression は rf4 から。4 thread は rf4〜rf27 の **24 個**、2 thread は rf4〜rf59 の **56 個**（`shader-private.h` 29〜31 行）。さらに `SCRATCH_RESERVE 4`・`CONSTANT_RESERVE 12` の余白を残すので、実際に長く生かせる数はもっと少ない。spill は無い。
- 後段が扱う IR の op（`shader-analyze.c` の `instruction_shape`）: NOP・SKIP_*・STORE_OUTPUT・CONST・BOOL・ICONST・LOAD_INPUT・LOAD_PUSH・LOAD_UBO・SAMPLE・SELECT・浮動小数点の演算と比較・AND/OR・IADD・ISUB・IAND・IOR・IXOR・SHL・SHR・ASR・SFU（RSQ・RCP・SQRT・EXP2・LOG2）・FABS・FLOOR・FRACT・NOT・FTRUNC・INEG・INOT・変換・MOVE・FROUND_EVEN。

compute で足す IR の op:

| IR の op | compute で | 節 |
| --- | --- | --- |
| `LOAD_SYSTEM`（local index・id、group id、num groups） | 必須 | §1.2 |
| `LOAD_STORAGE`・`STORE_STORAGE`・`STORAGE_SIZE` | 必須 | §1.3・§1.4 |
| `LOAD_SHARED`・`STORE_SHARED`・`BARRIER`・`FENCE` | reduction に要る（WS284 は使わない） | §1.9 |
| `LOOP_BEGIN`・`LOOP_END`、本物の `SKIP_*` | 必須（channel・tap の loop） | §1.6 |
| `IMUL`・`ILT`・`IGE`・`ULT`・`UGE`・`IEQ`・`INE`・整数の min・max | 必須 | §1.5 |
| `UDIV`・`UMOD`・`IDIV`・`IREM` | 必須（除数は定数とは限らない） | §1.5 |
| `PACK_HALF`・`UNPACK_HALF` | p011（D3: fp16 は保存だけ） | §1.10 |
| `ATOMIC`・storage image・subgroup | **範囲の外**（D4、ユーザーが推しを了承） | — |

encoder（`qpu.h`）に無く足す物: **branch**（cond・msfign・ub・相対の target、4.2・7.1 の形）、`MULTOP`・`UMUL24`、`BARRIERID`、`TMUWT`、`TIDX`。書き込みの口（waddr）は番号で書けるので変更は要らない: UNIFA 9、TMUD 11、TMUA 12、TMUAU 13、SYNC 16、SYNCU 17、SYNCB 18（M `qpu/qpu_instr.h` 105〜115 行、4.2・7.1 で同じ）。`UMIN` などは既にある。

### 1.2 compute の入口（payload と組み込みの値）

M `nir_to_vir.c` 3725〜3766・4783〜4832 行:

- payload: 4.2 は rf0（payload 0）と rf2（payload 1）、7.1 は rf3 と rf2。Z は rf0〜rf3 を expression に使わないので **移さずにそのまま読む**（L5。7.1 の rf0 は ldunif などの暗黙の書き込み先なので payload を置かない。今の rf3・rf2 は当たらない）。
- WorkgroupId: x = (payload0 & 0xffff) + base_x、y = (payload0 >> 16) + base_y、z = (payload1 & 0xffff) + base_z。**base は job ごとの uniform**（dispatch を分ける時の起点、§2.3。M と同じく CFG0〜2 の WG_OFFSET は使わない、M4）。
- LocalInvocationIndex = payload1 >> (32 − b)、b = log2(next_pow2(max(wg_size, 64)))。LocalInvocationId の x・y・z は index から作る: size が 2 の冪なら shift と and、それ以外は §1.5 の除算（index < 256 なので float の 1 段で正確）。
- NumWorkgroups: 前段は system set の storage buffer の 3 語として出す。後段は定数の offset のこの load を **uniform に置き換える**（dispatch の全体の数、分けた job でも同じ値）。
- wg_in_mem = (payload1 >> 16) & ((1 << (16 − b)) − 1)（size が 1×1×1 の時は mask しない）。共有 memory の base に使う（§1.9）。
- uniform の流れの address は CFG6。push・storage の base と limit・NumWorkgroups・group の base・shared の base・TMU の config・branch の巻き戻しの値は全部この流れに入る（kernel が job ごとに作る）。
- **divergence の性質**（§1.10 の ldunifa と §1.9 の barrier の loop が使う）: 定数・push・UBO の値・NumWorkgroups・group の base は全 lane で同じ。**WorkgroupId は wg_size が 16 の倍数の時だけ** 1 つの batch の中で同じ（それ以外は supergroup が 1 batch に複数の workgroup を詰める。M も `nir_divergence_multiple_workgroup_per_compute_subgroup` で divergent と扱う、`vulkan/v3dv_pipeline.c` 214 行）。size が 1 の軸の LocalInvocationId は 0 で同じ。

### 1.3 不変条件: address の clamp（H5）

GPU の VA の空間は全 session で 1 つで、Keiland の compositor の framebuffer も同じ page table にある。kernel の compute の job の検査は code と uniform の span しか見ない（Z `v3d-job.c` 215〜227 行）。だから shader の計算した address の clamp は「robustness」ではなく **session の隔離の要** で、次を不変条件にする:

1. 全ての shader の計算した memory の access（storage の load・store、shared、ldunifa の窓、vector の access、将来の atomic）は、その binding（shared は session の arena の中のその dispatch の範囲）の中に収める。**省く最適化はしない**（「範囲に収まると証明できる」場合でも、証明の誤りが隔離の穴になるので clamp を残す。1 access に 1 命令）。
2. 形: `address = base + umin(offset, limit)`、limit = range − access の byte 数（32 bit は 4、vec4 は 16、ldunifa の窓 K 語は 4K）。robustBufferAccess は範囲外の読みに buffer の中の任意の値を、書きに buffer の中への書き込みを許すので、述語や 0 の選択は要らない。umin は add の ALU の 1 命令で flag を使わない（M8 の「全 lane が無効の lookup」も減る、§1.4）。offset が負や巨大でも unsigned の umin で範囲に入り、境界をまたぐ 4 byte も limit = range − 4 で収まる。
3. range < access の byte 数（空・未設定の binding、Vulkan では UB）: kernel は base を **その session の dummy の page**（session の arena の中、他の session と共有しない。共有の page は session の間の情報の通り道になる）に向け、limit = 0 にする。
4. base と limit は kernel が descriptor の写し（session が持つ buffer の VA と range）から作る。shader に生の VA を計算させない。
5. compiler は kernel の中にあり、program は全部 kernel が生成する（利用者の QPU の code は無い）ので、この不変条件は compiler の lowering の 1 か所（storage・shared・unifa の address を作る関数）で守り、§1.8 の rule checker と lane の model の memory の検査（範囲外の access を失敗にする）で試す。

### 1.4 storage buffer（TMU の一般の access、M2・M8）

M `nir_to_vir.c` 43〜52 行（config）、252〜294 行（FIFO と flush）、590〜720 行（sequence）:

- **TMUA と TMUAU**: TMUA への書き込みは既定の config（regular・per-pixel・32 bit の 1 語）。それ以外（VEC2〜4 の load・store、op の違う access）は TMUAU に書き、次の uniform に config（`0xffffff00 | op << 3 | per-pixel | type`）を置く。
- **load**: address（clamp 済み）を TMUA/TMUAU に書き、結果を `ldtmu` の signal で受ける（vec4 は 4 回）。述語付きの load も **TMUA の書き込みは無条件** にする（範囲外・無効の lane も clamp 済みの範囲の中を読み、結果は捨てる）。IR の「述語の外の lane の dst は元のまま」は、temp に読んで条件付きの MOVE で作る。
- **store**: TMUD に data を 1〜4 回書き、最後に TMUA（1 語）か TMUAU＋VEC の config（2〜4 語）に address。述語付きの store は TMUD・TMUA の書き込みを IFA の条件付きにする（M と同じ）。**16 lane の全部で述語が偽の時に条件付きの TMUA の書き込みがどうなるかは文書が無い**（推測の risk、M8。M は active の lane が無い block を branch で飛ばすのでこの形をほとんど出さない）。だから述語付きの store の sequence は **ALLNA（msfign=P）の前向きの branch で囲み**、どの lane も書かない時は sequence ごと飛ばす（branch は p002 の encoder にあり、p004 の直線の compiler でも 1 つの前向きの branch として使える）。実機の HW5 で全 lane が偽の条件付きの store・load の振る舞いを確かめ、安全と分かれば p011 で branch を省く選択肢にする。
- **入力の FIFO**: 1 つの sequence の TMUD＋TMUA/TMUAU の書き込みの数 ≤ 16 / threads（4 thread で 4、2 thread で 8、M 700〜705 行）。vec4 の store（5 回）は 4 thread では不可なので、**vec2 の 2 つに分ける**（thread を落とさない。分けられない時だけ 2 thread）。
- **出力の FIFO**: 受けていない結果の語の合計 ≤ 16 / threads（4 thread で 4、2 thread で 8、M 258 行）。受けていない lookup は 8 個まで（M `MAX_TMU_QUEUE_SIZE`）。越える前に flush（thrsw → ldtmu、store は TMUWT）。2 thread でも vec4 の load は同時に 2 つまでしか出せない（§1.11 の性能に効く）。
- **sequence は不可分**: 最初の TMUD/TMUA の書き込みから終わりの TMUA/TMUAU までの間に、別の TMU の書き込み・ldtmu・thrsw・flush を置かない。sequence の operand が受けていない lookup の結果に依る時は、sequence を始める前に flush する（M 600〜609 行）。
- **block の終わりに持ち越さない**: branch の前・block の終わり・program の終わりで、受けていない lookup を全部受け、store があれば TMUWT（M 4523〜4528 行）。TMUWT を最後の命令に置かない（GFXH-1625、M `qpu_validate.c` 376〜379 行）。
- vector: IR は scalar。後段で「同じ base で offset が +0・+4・+8・+12 の LOAD_STORAGE」を 1 つの vec の lookup にまとめる（p011）。clamp の limit は vec の byte 数で取る。

### 1.5 整数の演算（M7）

- `IMUL`（下位 32 bit）: `MULTOP` と `UMUL24` の組（M `v3d_compiler.h` 1541 行）。rtop は 1 つで、間に thrsw を置かない（§1.8）。
- 比較: 減算・XOR で N・C・Z の flag を push し、条件付きの MOVE で Boolean を作る。
- **除算**: QPU に mul-high が無いので magic number の方法（32×32 の上位 32 bit が要る）は使えない（第 1 版の「定数は乗算と shift」は誤り）。
  - 除数が 2 の冪の定数: shift と and。
  - それ以外（定数・push・UBO の値・計算した値を問わない）: 一般の形。q0 = f2u(u2f(n) × rcp(u2f(d)))、r0 = n − q0 × d（IMUL、32 bit で wrap）、q1 = q0 + f2i(i2f(r0) × rcp_d)、r1 = n − q1 × d、最後に r1 ≥ d なら +1、r1 < 0（signed）なら −1。32 bit の全ての n と d ≥ 1 で正しい商と余りを出す見込み（rcp の相対誤差が 2^−16 以下なら 2 段で足りる、**SFU の RECIP の精度は推測**）。n < 2^24 が分かる時（LocalInvocationId の分解）は 1 段。約 20〜25 命令。
  - 符号付きは絶対値で割って符号を直す。d = 0 は Vulkan では未定義だが、trap せず決まった値（全 bit 1）を返す。
  - 試験: host で n・d の端（0、1、2^31、2^32 − 1、d > n、d = n、n = k × d ± 1）と乱数 10^6 組を lane の model で C と照合（HT2）。SFU の RECIP の精度は model が hardware と同じとは限らないので、実機の HW1 にも端の値の除算を入れる。
  - WS284・WS285 の shader が使う除算の形（画素の index → x・y、幅は push の値）は p004 の始めに WS284 の shader で確かめる。

### 1.6 分岐と loop（H4・M1）

IR の loop: `LOOP_BEGIN`〜`LOOP_END` を、`LOOP_END` の Boolean が真の channel が残る限り繰り返す。loop の変数は loop の前と終わりの `MOVE` で定義される（Z `ir.h` 冒頭）。if は前段が if 変換するので、後段の分岐は loop と SKIP と述語付きの store の前向きの branch だけ。

1. **active の初期値と msfign**: loop・SKIP・store の branch には全部 **msfign=P** を付ける（dispatch されていない lane と捨てられた lane を ANYA・ALLNA の数に入れない、M `nir_to_vir.c` 4241・4282・4569 行）。active の mask の初期値は全 lane 真（M と同じ）。dispatch されていない lane の store は hardware が MSF で落とす（**推測**: M が何もせずに CTS を通す事から）。HW2 の部分 batch の試験（1 workgroup の 24、8 × 3）で buffer の後ろの canary が変わらない事を確かめ、駄目なら active の初期値を MSF から作る（fallback、4.2・7.1 とも MSF は読める）。
2. **divergence の解析で一様な loop を見分ける**（§1.2 の性質）: 条件が一様な loop は active の mask を使わず、条件付きの書き込みも要らない（WS284 の ci の loop はこれ）。一様でない loop だけ次の 3〜5 を使う。
3. loop の中: loop の変数への `MOVE` と store は active を条件にする。他の値は SSA で loop の中だけで使うので、抜けた lane の値は捨ててよい。
4. `LOOP_END`: active = active AND cond、flag を push、**ANYA（msfign=P）で先頭へ branch**。branch の後の 3 語は遅延の slot（§1.8 の規則）。
5. 入れ子は mask を重ねる（loop ごとに 1 register）。
6. **uniform の流れの巻き戻し**: loop の本体が uniform を読むと繰り返すたびに流れが進むので、branch の `ub` で流れの address を相対に戻す（branch が 1 つ uniform を消費する）。巻き戻しの語の数は「loop の先頭から branch までに読む uniform の数」で、遅延の slot に uniform を読む命令を置くとずれる（§1.8 の規則 1）。
7. **生存区間**: loop の外で定義され中で読まれる値と loop の変数は loop の終わりまで生かす（back edge）。
8. **loop の変数の MOVE の合併**（M1）: loop の終わりの `MOVE v ← t` で、t の定義が v の最後の読みの後なら t を v と同じ register に置く（MOVE を消す）。これが無いと loop の終わりに古い値と新しい値が同時に生き、WS284 の conv1x1 の 16 個の和が 32 register を取る（§1.11）。
9. `SKIP_*` を本物の skip にする（ALLNA で飛ばす）のは graphics にも効くので、p007 で graphics の回帰（`shader71-host-test`）を流す。

### 1.7 block を持つ一般の scheduler（H2）

今の scheduler（fragment 専用、1 block）を、全 stage で block ごとに働く物に広げる（p008、7.1 から。4.2 は p013）。

- **block**: program を branch と branch の target で切る（LOOP_BEGIN が target、LOOP_END が branch、SKIP と述語付きの store の前向きの branch）。barrier の thrsw は block の境ではなく、block の中の順序の柵。
- **block の入口と出口の約束**: 受けていない TMU の lookup・開いた TMU の sequence・rtop・unifa の窓・flag・（4.2 の）accumulator は block をまたがない。block の間の値は全部 rf で受け渡す。だから block は互いに独立に schedule できる。
- **block の中**: 今の engine（`classify`→`build_edges`→`set_priorities`→`place`→`fits`）を使い、資源を足す:
  - TMU: sequence の不可分（開いている間は他の TMU の書き込み・ldtmu・thrsw を置かない）、入力の FIFO・出力の FIFO・lookup の数の計数（§1.4）、ldtmu の順序。
  - rtop: MULTOP → UMUL24 を 1 つの資源にし、間に thrsw を置かない。
  - unifa: unifa の書き込みから 3 語は ldunifa を置かない、ldunifa の順序を保つ、ldunifa を他の uniform を読む命令と同じ語にしない。
  - SYNCB: BARRIERID の前に TMU を全部受けて TMUWT、BARRIERID の後に thrsw、その間に TMU の操作を置かない。
  - uniform の流れ: uniform を読む命令の順序を保つ（今の規則）。
- **終わり（terminator）**: block の終わりは (a) 落ちる、(b) branch（cond の flag は branch の前に立てる。遅延の 3 語には §1.8 の規則 1 に合う、block の中の命令を詰める）、(c) program の終わり。(c) は stage ごと: fragment は今の tile の tail、compute は「最後の thrsw と遅延の 2 語、TMUWT を最後の語に置かない、受けていない TMU が無い」。
- **verify**: 今の「値の simulate」を block ごとにする。block の入口で各 register は「割り当て上その register にある値」と置き、block の語を simulate して全ての source が期待の値を読む事、出口で live-out の値が期待の register にある事を見る。block ごとに閉じるので組み合わせられる。
- **fallback**: scheduler が断った block だけを source 順の語にする（program 全体ではなく block ごと）。`binary->scheduled` は block の数と schedule できた数にする（HW10 で比べる）。
- **効率の見込み**（§3.3 の根拠）: WS284 の conv1x1 の内側の loop（1 回に 64 MAC）は、source 順で約 170〜200 語（fmul・fadd・ldunifa・ldtmu が 1 語ずつ、遅延の NOP）、schedule して約 75〜85 語（fmul と fadd が 1 語に組み、signal は ALU と同じ語に乗る）。ALU の上では **2〜2.5 倍** の差。TMU の速さが律速なら差は縮む。

### 1.8 hazard の規則と独立の rule checker（M3）

scheduler と lowering が守る規則（M `qpu_schedule.c` 2375〜2405・1416〜1510 行、`qpu_validate.c` 130〜445 行から事実として取り、自分の言葉で列挙する）:

1. branch の遅延の 3 語に、branch・thrsw・unifa の書き込み・uniform の流れを読む命令を置かない。
2. thrsw の遅延の 2 語に branch を置かない。branch の 3 語・thrsw の 2 語の中に次の branch を置かない。
3. unifa の書き込みの後 3 語の中に branch を置かず、ldunifa はその 3 語より後。ldunifa と他の uniform を読む命令を同じ語にしない。
4. MULTOP と UMUL24 の間に thrsw を置かない（rtop が消える）。thrsw の 2 語後の rtop の扱いも同じ。
5. program の終わり: 最後の thrsw（thrend）と遅延の 2 語。その中の rf の書き込みの制限（4.2 は rf2・rf3）、TMUWT を最後の語に置かない（GFXH-1625）。program の終わりの thrsw は必ずある。
6. SFU の結果を読む距離と、SFU の後の SFU の距離（今の規則）。
7. 1 語に TMU・SFU・TSY・TLB・VPM の magic の書き込みは 1 つ。
8. small immediate の規則（7.1 は 1 語に 1 つ、a/b・c/d の位置）。
9. TMU: sequence の不可分、入力・出力の FIFO と lookup の数の上限、block・program の終わりに受けていない lookup が無い、ldtmu の数が lookup の語の数と合う。
10. BARRIERID の前に TMU を全部受けて store の TMUWT、BARRIERID の後に TMU の操作より前に thrsw。
11. compute の loop・SKIP・store の branch は msfign=P。
12. loop の branch の `ub` の巻き戻しの数 = 先頭から branch までの uniform の読みの数（block を静的に数える）。
13. 4.2: accumulator と flag は thrsw と block の境をまたがない。
14. ldunifa: unifa の 1 回の書き込みの後の ldunifa の数 ≤ その窓の K（§1.10）。

**rule checker**: 上の規則を、M の `qpu_validate.c` を写さず、この一覧から独立に書いた host の検査器（`plan/tools/v3d-qpu/qpu-rules.c`、M の MIT の decoder で語を読む）。HT2〜HT4 の全 binary と、今の graphics の全 shader の binary（`shader-host-test`・`shader71-host-test` が作る物）に流す。lane の model（同じ作者の理解で書く）だけでは規則の誤解がすり抜けるので、規則は checker と model の 2 つで別々に持つ。checker の一覧と M の validate の規則の対応表を p003 の記録に残す（M の file の更新で規則が増えた時に見直す）。

### 1.9 共有 memory と barrier（M10）

- V3D に on-chip の共有 memory は無く、共有の変数は DRAM の arena に置いて TMU で読み書きする（M `nir_to_vir.c` 587 行）。速さは storage と同じ（L1T・L2T だけが効く）。WS284 は使わない。
- address = arena の base（uniform）+ umin(wg_in_mem × shared_bytes + offset, arena_bytes − bytes)。clamp は arena 全体に掛ける（同じ dispatch の他の workgroup の slot には届きうるが、それは app 自身の data で、隔離は保つ）。
- barrier（M 3697〜3712 行）: TMU を全部受け、store があれば TMUWT → `BARRIERID` で SYNCB に書く → **thrsw**（block は次の thread の切り替えで起き、その時に TMU の待ちが残っていてはいけない）。barrier のある shader は single segment にしない。
- **barrier を含む loop**（M10）: 回る回数が batch（thread）ごとに違うと、早く抜けた thread が barrier に来ず supergroup が永久に待ち、2 秒で全体の reset になる（compositor の仕事も失う）。だから **barrier を含む loop は、その pipeline の wgs_per_sg を 1 に固定し（supergroup を使わない）、loop の条件が workgroup の中で一様（§1.2 の divergence の解析、WorkgroupId も可）と証明できる時だけ通す。証明できなければ compile で断る**。前段の「全 invocation が入る loop」の検査（Z `ir.h` の BARRIER の注記）だけでは足りない。
- supergroup（M `common/v3d_util.c` 30〜90 行）: wg_size が 16 の倍数なら 1。それ以外は空きの lane が最少になる数（最大 16、dispatch の workgroup の数を越えない）。barrier がある時は batch の数を QPU の thread の総数の半分までに抑える（4.2: 8 × threads、7.1: 12 × threads）。barrier は supergroup の単位で待つ。
- 7.1 は 12 QPU が 3 つの slice に分かれる。slice をまたぐ supergroup の中の共有 memory の可視性は M が何もしない事しか分からない（**推測の risk RK1**）→ HW6 の reduction（wg 24・40、barrier あり）で確かめる。駄目なら barrier の前に TMU の cache の操作を足すか、supergroup を 1 slice に収まる大きさに抑える。

### 1.10 ldunifa（H6）と fp16

ldunifa（unifa に address を書き、以後 1 命令 1 語を全 lane に配る、ALU を使わない signal）は conv の重みの読みに効く。M の制約（`nir_to_vir.c` 3156〜3200 行）と、zedBSD の追加の条件:

1. **binding が NonWritable**（SPIR-V の NonWritable、GLSL の `readonly`）。ldunifa は L2T を読まない（推測: M の注記「possibly because ldunifa doesn't read from the L2T cache」）ので、同じ shader の TMU の書き込みが見えない。今の前段は NonWritable を捨てる（Z `spirv.c` 1813 行）→ **前段の `struct drv_gpu_shader_ir_uniform` に access の flag を 1 つ足し、NonWritable の decoration（変数か block の全 member）で立てる**。他の backend はこの field を読まないので振る舞いは変わらない。共有の前段の変更なので p011 の時に Q1 の了承を得る（§5）。
2. **その buffer を、同じ execution の前の dispatch が L2T の clean（DRAM への書き戻し）の後に書いていない**。実行器は execution ごとに「最後の waited clean の後に writable な storage として bind された buffer の VA の範囲」の小さな表（溢れたら全部を汚れと見る）を持ち、ldunifa を使う pipeline の dispatch の前に、ldunifa の binding がその表と重なれば waited clean を入れる（§2.5）。WS284 の重みの buffer は writable に bind されないので clean は入らない。L2T の clean を省く最適化（§2.5）と組んでも、この検査が ldunifa の正しさを守る。
3. **address が一様**（§1.2 の divergence の解析で一様と証明できる値だけから作る）**かつ、一様でない loop の変数から作らない**（unifa の address は lane 0 から取られ、lane 0 が無効だと値が嘘になる、M 3163〜3180 行）。
4. **H5 の clamp**: unifa の 1 回の書き込みの後に読む語の数 K を compile の時に決め（loop の各回で書き直す）、address = base + umin(offset, range − 4K)。K が静的に決まらない形では使わない。
5. 32 bit に揃った address だけ（M と同じ）。

WS284 への連絡（§5 に入れた）: 重みの buffer を `readonly` で宣言する。conv1x1 の出力の channel の束は `gl_WorkGroupID.y`（local size (64, 1, 1)）から取ると一様になる（`gl_GlobalInvocationID.x` から取ると divergent で ldunifa にならない）。

fp16（D3）: まず fp32 で正しく動かし、p011 で `packHalf2x16`・`unpackHalf2x16` の保存だけ（buffer の帯域を半分、計算は fp32）。encoder の `HALF_LOW`・`HALF_HIGH`・`PACK_HALVES` は既にある。

### 1.11 register の予算と WS284 の conv1x1（M1）

- 4 thread: 24 個（余白を引くと長く生かせるのは 20 前後）。2 thread: 56 個。足りない shader は 2 thread に落とし、それでも足りなければ断る（spill は作らない）。
- WS284 の conv1x1（出力 4 channel × 画素 P = 4）の見積もり: 和 16、入力 4 画素 × vec4 の 16 語、重みは ldunifa で 1 語ずつ（4 つの fmul に使い、組むために 2〜4 個生きる）、address と limit と counter で 4〜6。**合計 38〜42 で 2 thread に収まる**。§1.6 の MOVE の合併が無いと和の古い値と新しい値で +16 になり 56 を越えうる（推測）。だから p007 の完了の条件に「WS284 の実際の `conv1x1.comp`（P = 4）が 2 thread で compile できる」を入れる。P = 2（和 8、入力 8）は 4 thread の 20 前後に収まるか際どい。
- 2 thread だと出力の FIFO は 8 語で、vec4 の load は同時に 2 つまで。1 回の ci の束で入力の vec4 を 4 つ読むので、loop の 1 回に flush（thrsw）が 2 回要る。これは §3.3 の効率の幅に入れた。
- WS284 は spec constant を使わないので、P の違いは build の時の別の SPIR-V（WS284 の regenerate.py）で作る（Q1 の判断、H1）。

### 1.12 4.2 と 7.1（M9）

- **7.1 を先に作る**: Pi 5 が唯一の実機で、受け入れも Pi 5。版に依らない部分（IR の解析、block の構造、divergence の解析、loop の mask、除算の展開の形、uniform の一覧、CSD の構成の計算、rule checker の大部分）は共有の file（例 `shader-compute.c`）にし、版で違う部分（payload、small immediate、accumulator、scheduler、encoder）だけを `shader71-*.c` に書く。
- 4.2 は p013 の port: 7.1 の版で違う部分の 4.2 の写し（`shader-*.c`）と、4.2 の scheduler（accumulator と promote のある 2360 行）の block 化、4.2 の lane の model の拡張、HT の 4.2 の版。Pi 4 の実機は D1 で完了の条件から外した（板がある時）。
- Z の 4.2 の `run_compute`（`v3d-job.c` 504 行）は呼ぶ所が無く（`BCM2711_V3D_JOB_COMPUTE` を作るのは 7.1 の `v3d71-diagnostic.c` 449 行だけ）、**4.2 の CSD は一度も走っていない**（第 1 版の「4.2・7.1 とも既にあり」を直す）。7.1 は実機の self-test（V8 の store）で動いた。
- WS279 のユーザーの好み（「無理に共通化しないで rpi4のコピーでrpi5を作って、共通部分をまとめる」）は既存の 4.2 の code から 7.1 を作る時の物。compute は 4.2 にも code が無いので、試せる 7.1 から作り、違う部分だけを 4.2 に写す。向きが逆なので Q1 に確かめる（§5 の Q-a）。
- 4.2 と 7.1 の違い（compute に効く物）: payload の register、small immediate、accumulator の有無、CSD の register の offset と CFG7、CFG4 の「− 1」（< 7.1.6）、CFG5 の PROPAGATE_NANS（4.2 だけ）、QPU の数（8 と 12）。

### 1.13 specialization constant（H1）

前段に `OpSpecConstant*` の扱いは無く（Z `spirv.c` は 41〜44 の constant だけ、`spirv-compute.inc` 197〜198 行は LocalSizeId の spec constant を断る）、bcm2711 の `vulkan-pipeline-build.c` 219 行も `pSpecializationInfo` を断る。Q1 の判断: WS284 は build の時の variant と push constant を使うので、WS283 は今は足さない。後の項目として §5 に置く（Future Work への登録は Q1）。

## 2. 実行器（Vulkan → CSD）

### 2.1 今の形（Z `src/drivers/gpu/bcm2711/vk/`・`render-worker.c`）

kernel の中の Vulkan の実行器。wire の opcode を `vulkan-dispatch.c` の route の表で受け、command buffer を記録（`vulkan-record*.c`）、submit で descriptor を写して prepared の graph（`vulkan-prepared.c`）、worker が step ごとに native の pass を実行（1 step = 1 native pass、session が順に 1 step ずつ、`render-worker.c` 1281〜1292 行）。V3D の job は 1 つずつ完了まで待つ。

- executor は step を 1 つ終えるたびに `sched_yield()` する（`render-worker.c` 1905〜1908 行、mutex の待ち手に番を回すため）。`worker.contended`（他の session が待つか、1333 行）を step ごとに求めている。
- graphics の program は pass（step）ごとに `bcm2711_native_program_upload` で native storage に写して clean する（`vulkan-native-draw.c` 321 行 → `native-storage.c` 228〜280 行）。pipeline の作成の時ではない（第 1 版の記述は誤り）。
- CSD の前に L2T の flush（L2TCACTL = 1、FLM = 0、待たない）と slice の cache の無効化、後に TMU の write combiner の flush（0x100、待つ）と L2T の clean（FLM = 2、待つ）（`v3d71-hardware.c` 439〜505 行、`v3d71-job.c` の `run_compute`）。render の job には `clean_output` の選択がある（`v3d-job.h` 42 行）が compute には無い。

### 2.2 足す物

| 部分 | 中身 | file（予定） |
| --- | --- | --- |
| queue | 1 つの family に `VK_QUEUE_COMPUTE_BIT` を足す。**`CONFIG_DRIVER_BCM2711_COMPUTE` が y の時だけ**（§2.6） | `vulkan-query.c` 272 行 |
| 限界 | maxComputeWorkGroupCount 65535×3、Invocations 256、Size は前段の 128・128・64 と同じ値、SharedMemorySize 16384、minStorageBufferOffsetAlignment 32（M `v3dv_device.c` 979 行、WS284 §3.5 が使う）、maxPerStageDescriptorStorageBuffers 8・maxDescriptorSetStorageBuffers 24（M は 8 と stage 数 × 8）、maxStorageBufferRange 2^27（libglesv2 の最小と同じ、memory の budget の中） | `vulkan-query.c` |
| descriptor | `STORAGE_BUFFER` を pool・layout・update・prepared の写しに足す（`_DYNAMIC` は後）。中身は buffer の VA・offset・range。未設定の binding は dummy（§1.3） | `vulkan-descriptor-*.c`・`vulkan-layout.c`・`vulkan-prepared.c` |
| pipeline | `GPU_OP_CREATE_COMPUTE_PIPELINES`: 作る時に compile（`BCM2711_SHADER_COMPUTE`）。binary の情報: local size、threads、single seg、shared bytes、barrier の有無と barrier を含む loop の有無（wgs_per_sg の固定）、uniform の一覧、ldunifa の binding の一覧と各窓の K | `vulkan-pipeline-*.c` |
| bind point | compute の pipeline・set・push を graphics と別に持つ | `vulkan-record*.c` |
| dispatch | 記録 → prepared の event → step: uniform の流れ、arena、job の分割（§2.3）、CSD の構成（§2.4）、`BCM2711_V3D_JOB_COMPUTE` | 新しい `vulkan-native-compute.c` |
| code | **execution ごとに pipeline の identity で 1 回** native storage に写す（同じ execution の 2 回目以降の dispatch は写した物を使う。WS284 の 1 frame は 7 pipeline・122 dispatch で 7 回）。pipeline の寿命の割り当ては p011 の候補 | 同上 |
| barrier | 実行器は job を 1 つずつ完了まで待つので、`vkCmdPipelineBarrier` は hardware の命令が要らない。記録の検査で COMPUTE_SHADER の stage と SHADER_READ・WRITE、HOST の stage と HOST_READ（WS284 の最後の compute → host）、DRAW_INDIRECT と INDIRECT_COMMAND_READ を受け付ける | `vulkan-barrier-validate.c` |
| indirect | `GPU_OP_CMD_DISPATCH_INDIRECT`: step の時に CPU が indirect の buffer の 3 語を読む（前の job は完了済み。CPU が読む前に waited clean が要る、§2.5）。どれかが 0 なら何もしない、65535 を越える値は dispatch を断って execution を失敗にする（device lost の扱い）。arena・NumWorkgroups の uniform・分割はその値で step の時に作る | 同上 |
| UAPI の注記 | `include/uapi/gpu-op.h` の `GPU_OP_CREATE_COMPUTE_PIPELINES`（956 行）・`GPU_OP_CMD_DISPATCH`・`_INDIRECT` の「bcm2711 has no route」を、route を足した Phase で直す（注記だけ、layout は変えない、HAL ではない、L6） | `include/uapi/gpu-op.h` |

### 2.3 dispatch の step と分割（M4・L5）

1. group の数のどれかが 0 なら何もしない。各次元 ≤ 65535。
2. **job の分割**: 次のどれかを越える時、dispatch を workgroup の箱（z、y、x の順に切る）の複数の CSD job に分ける。
   - shared の arena: その job の shared_bytes × workgroup の数 ≤ session の arena の上限（例 16 MiB）。arena の大きさは M と同じ「shared_bytes × job の workgroup の数」（`v3dv_cmd_buffer.c` 4363〜4370 行）。wg_in_mem が (16 − b) bit で巡回するなら min(数, 2^(16−b)) で足りる可能性がある（推測）が、HW8（各 workgroup が自分の slot に自分の番号を書き、barrier の後に読み戻して重なりを見る）で確かめるまで M の数を取る。
   - CFG4 の batch の数が 32 bit に収まる事、および 1 job の batch の数 ≤ 2^20（1 job の時間を抑える目安、HW7・HW10 の測定で直す）。
   - 1 job は進んでいても **2 秒が上限**（500 ms の窓 × 4、Z `v3d-job.c` 20〜21・549〜660 行）。kernel は shader の時間を知らないので分割は batch の数の目安だけで、1 invocation が長い loop を回す shader は app の側で dispatch を小さくする（WS284 は 1 dispatch を数 ms に収める）。2 秒を越えると全体の reset で compositor の仕事も失う（RK4）。
3. 分けた job は **group の base を uniform で渡す**（§1.2、M の `QUNIFORM_WORK_GROUP_BASE` と同じ）。CFG0〜2 の下位 16 bit（WG_OFFSET）は 0。NumWorkgroups は dispatch の全体の数。job ごとに uniform の流れの写しを作る（base の 3 語だけ違う）。
4. uniform の流れ: compiler の uniform の一覧の順に、push の語・storage の base と limit・NumWorkgroups・group の base・shared の base と limit・TMU の config・loop の巻き戻しの値。execution ごとの ring に置く。
5. CSD の構成（§2.4）→ job。
6. 1 dispatch = 1 step が既定。同じ execution の次の event も compute の dispatch（か hardware の命令の要らない barrier）で、`worker.contended` が偽（他の session が待っていない）なら、同じ step の中で続けて走らせる（上限は 1 step あたり 4 ms か 32 dispatch、p011）。他の session が待てば今まで通り 1 dispatch ごとに番を回す（compositor を待たせない）。
7. hang の判定: CURRENT_CFG4（残りの batch）が 500 ms の窓で減らないと止まりと見なし、減っていても 2 秒で打ち切る。全 batch を配った後の最後の batch は CFG4 が動かないので窓 2 つ（1 秒）で止まりと見なされる（L5）。

### 2.4 CSD の構成の値（M `common/v3d_csd.h`、`v3dv_cmd_buffer.c` 4273〜4380 行、WS279 design §2.5）

| 語 | 中身 |
| --- | --- |
| CFG0〜2 | group の数 << 16 \| 0（WG_OFFSET は使わない） |
| CFG3 | (wgs_per_sg & 0xf) << 8（16 は 0）\| (batches_per_sg − 1) << 12 \| (wg_size & 0xff)（**256 は 0**）。bit 26 OVERLAP_WITH_PREV、[25:20] max SG ID は使わない |
| CFG4 | batch の数。batches_per_sg = ceil(wgs_per_sg × wg_size / 16)、全体 = batches_per_sg × 完全な sg の数 + ceil(余りの wg × wg_size / 16)。**4.2 と 7.1.6 より前は − 1** |
| CFG5 | code の address \| SINGLE_SEG（bit 1）\| THREADING（bit 0、4 thread）\| PROPAGATE_NANS（bit 2、4.2 だけ） |
| CFG6 | uniform の流れの address |
| CFG7 | 7.1 だけ、0 |

書く順: cache の無効化 → CFG1〜6 →（7.1）CFG7 → CFG0。supergroup の選び方は §1.9（M の関数の規則を自分で書き直す。barrier を含む loop の pipeline は 1）。2 thread の variant は THREADING を立てない（M は 4 thread の時だけ立てる。2 thread で動く事は HW3 で確かめる）。

例（HT3 と HW2・HW6 の場合、7.1、4 thread）:

| wg_size | group の数 | barrier | wgs_per_sg | batches_per_sg | CFG4（7.1.6 以後） | 通る経路 |
| --- | --- | --- | --- | --- | --- | --- |
| 24 | 1 | なし | 1（group が 1 つ） | 2 | 2 | 部分 batch（2 つ目は 8 lane） |
| 24 | 64 | あり・なし | 2 | 3 | 96 | supergroup |
| 40 | 64 | あり・なし | 2 | 5 | 160 | supergroup、barrier の上限の計算 |
| 8 | 3 | なし | 2 | 1 | 2 | 余りの workgroup |
| 256 | 4 | あり | 1 | 16 | 64 | CFG3 の wg_size が 0 |

### 2.5 cache の操作と dispatch の費用（M6、WS284 への答え）

- **WS284 の問い「同じ submit の中の CSD の job の間は L2T の clean を省けるか」への答え（設計の案、推測を含む）**: GPU → GPU の可視性に要るのは、(a) job の後の TMU の write combiner の flush（待つ）と、(b) 次の job の前の slice の cache（L1T など）の無効化（今もある）。L2T は全 slice で共有なので、write combiner を出た data は次の job の TMU の読みから見える（推測）。L2T の waited clean（FLM = 2、DRAM への書き戻し）が要るのは、CPU が GPU の書いた memory を読む前（execution の終わり、fence の signal、indirect の 3 語、UBO の中身を CPU が uniform の流れに写す step）と、ldunifa が GPU の書いた data を読む前（§1.10 の 2）だけ。だから **省ける見込み**。p011 で compute の job に `clean_output` を足し（write combiner の flush は常に、L2T の waited clean は選択）、実行器が上の場合だけ立てる。正しさは HW9 の「依存のある dispatch の鎖を clean 無しで回し、毎回 CPU の参照と比べる」で確かめてから既定にする。
- CFG3 の OVERLAP と CSD の queue への積み増しは、依存の無い dispatch の間だけ（依存のある間は cache の操作が入らないので不可）。WS284 の graph はほとんど依存の鎖なので効果は小さい見込み。
- 費用の内訳（1 dispatch の往復）: job の完了待ち（IRQ）、write combiner の flush、L2T の waited clean、step の切り替えと `sched_yield()`、code の upload（execution ごとに 1 回にする、§2.2）、uniform の流れの作成。§2.3 の 6 で yield を減らし、上で clean を減らす。
- **安い先の測定**: 今ある V8 の診断の dispatch（`v3d71-diagnostic.c`）を N 回回す往復の時間を、p006（最初の実機）で T1 に測ってもらう（HW7）。Keiland の desktop と CPU の負荷の下でも測る（yield の影響を見る）。

### 2.6 COMPUTE の flag の出し方（M5、Q1 の判断）

- `VK_QUEUE_COMPUTE_BIT` を出すと、noct の accel（`build/NoctLang/src/accel/accel_vulkan.c` 1131〜1139 行）が compute の family を選んで Vulkan の compute の道に入る。libglesv2 は display の family の COMPUTE に加え `vertexPipelineStoresAndAtomics` を要求する（Z `gles.c` 2433 行）。bcm2711 は features を全部 0 で返す（`vulkan-query.c` 122〜128 行）ので、**libglesv2 の GLES 3.1 の compute は bcm2711 では入らない**（WS283 は vertex の stores-and-atomics を出さない。確かめは p012）。
- だから途中の段階で他の component が compute を使い始めないよう、**新しい build の設定 `CONFIG_DRIVER_BCM2711_COMPUTE ?= n`**（top の `Makefile`、`CONFIG_DRIVER_BCM2711_VIDEO` と同じ形で `-DCONFIG_DRIVER_BCM2711_COMPUTE=0/1`）を置く。n の時は queue の flag を出さず、compute の route（66・110・111）は今と同じく断る。compiler と実行器の code は常に build する（warning と host の試験の対象にする）。WS283 の試験の image は `plan/ws283/tests/config.mk` で y にする（AGENTS の試験の image の規則）。
- p012（p010 の実機の確かめの後）で既定を y にし、Pi 5 で noct の accel、`vkreset`・`vkcoherency`、libglesv2 が 3.0 のままである事を確かめる。
- **制限（ユーザーに知らせる）**: Vulkan 1.0 は compute の stage の storage buffer の atomic と storage image を必須にしている。D4（atomic・storage image を最初の範囲から外す）の結果、flag を出した後も bcm2711 は atomic・image を使う compute pipeline の作成を断る（Vulkan としては不完全）。今も graphics の family に compute が無い時点で不完全なので、新しい判断は要らないと考える。noct の accel の shader が atomic・image を使うなら p012 で分かる（使うなら accel が CPU に戻るかを確かめる）。

## 3. 試験の計画と性能の見込み

ID: host の試験は **HT**、実機の試験は **HW**、risk は **RK**（第 1 版の R の重なりを直した、L3）。

### 3.1 host（担当が流す、短い物）

| 試験 | 中身 | 道具 |
| --- | --- | --- |
| HT1 encoder | branch（cond・msfign・ub・target）・MULTOP・UMUL24・BARRIERID・TMUWT・TIDX、UNIFA・TMUA・TMUAU・TMUD・SYNCB への書き込み、ldunifa の語を、4.2・7.1 とも M の packer・decoder と照合 | `qpu-format-host-test.c`・`qpu71-format-host-test.c` の形 |
| HT-R rule checker | §1.8 の規則の検査器を、規則ごとの正・誤の小さな語の列と、HT2〜HT4 の全 binary と今の graphics の全 shader の binary に流す | 新しい `plan/tools/v3d-qpu/qpu-rules.c` |
| HT2 compute の実行 | compute の SPIR-V を compile し、**16 lane・複数 thread の lane の model** で走らせて C の参照と bit 一致（fp は誤差の上限）。program: saxpy、SiLU、concat、2 倍の upsample、debayer の 2×2、ID の全部（local size 4×2×3、group の数 (3, 5, 2) で LocalInvocationId・WorkgroupId・NumWorkgroups・GlobalInvocationId を buffer に書く）、除算の端と乱数（§1.5）、範囲外の offset（負・巨大）の load・store で隣の canary の buffer が変わらない、全 lane で述語が偽の load・store、一様でない loop（`for (i = 0; i < gid % 7; i++)`）、3×3 conv、depthwise 3×3、**WS284 の実際の `conv1x1.comp`（P = 4、2 thread）**、reduction（共有 memory と barrier、wg 256・24・40）、barrier を含む一様な loop と、一様でない時に compile が断る事、2 thread を強制した variant | lane の model（D5 で `plan/tools/v3d-qpu/` に移す）を広げる: 16 lane の lockstep、batch と thread の交互の実行、dispatch されていない lane（MSF）、payload、uniform の流れと `ub` の巻き戻し、TMU の一般の memory（入力・出力の FIFO と lookup の数の検査、範囲外の access を失敗にする）、TMUWT、SYNCB の barrier（supergroup の全 thread が来るまで待つ、来なければ deadlock を失敗にする）、branch と遅延の slot、unifa と ldunifa |
| HT3 CSD の構成 | §2.4 の値（supergroup・batch・rev の規則・分割）を §2.4 の例の表の場合と端（65535³ の溢れ、arena の上限）で M の式と照合 | 新規 |
| HT4 実行器 | descriptor・pipeline・record の検査の境界（storage の range と dummy、bind point の分離、group の数 0・65536、indirect の値、barrier の stage・access）、job の分割と uniform の流れの写し | 既存の vk の host の試験の形 |
| HT5 graphics の回帰 | `shader-host-test`・`shader71-host-test`（block の scheduler・SKIP の本物化・encoder の変更の後） | 既存 |

試験の program（C・GLSL・SPIR-V）と HW 用の `plan/ws283/tests/config.mk` と個別の file の複写は、各 Phase の LW に入れた（M9）。

### 3.2 実機（Pi 5 D0、kei@10.0.30.5、T1 に依頼。Pi 4 は板がある時、D1）

`vkquad-pi` と同じく build した program を board に置いて走らせる（`plan/ws279/tests/vkquad-pi-build.sh` の形）。

| 試験 | 合格の条件 | Phase |
| --- | --- | --- |
| HW0 `vkcompute`（既存、fill、local size 32、128 group） | `PASS, 4096/4096 words` | p006 |
| HW1 saxpy（1 M 要素、local size 64・256）と除算の端の値 | 全要素が CPU の参照と bit 一致 | p006 |
| HW2 部分 batch と supergroup（1 workgroup の 24、wg 8 × 3、wg 24・40 × 64 の barrier なし）と ID の全部（4×2×3、(3, 5, 2)） | 全部の値が一致、buffer の前後の canary が変わらない（dispatch されていない lane が書かない） | p006 |
| HW3 2 thread を強制した variant（THREADING = 0） | HW1 と同じ | p006 |
| HW5 全 lane で述語が偽の条件付きの load・store（branch で囲まない版を試験用に作る）と範囲外の offset | 止まらない、canary が変わらない、範囲外の読みは buffer の中の値 | p006 |
| HW7 dispatch の往復の費用（V8 の診断と空の dispatch を N 回、単独と desktop・CPU の負荷の下） | 数字を記録（合格の線は無い） | p006 |
| HW4 一様でない loop と 3×3 conv（16→16 channel、52×52、loop） | CPU の参照と相対誤差 1e-5 の中 | p010 |
| HW6 reduction（共有 memory、barrier、wg 256・24・40、1 M float の和）と barrier を含む一様な loop | 誤差の上限の中。slice をまたぐ supergroup の可視性（RK1） | p010 |
| HW8 大きい dispatch の分割と arena（shared を使う、job を分ける大きさ、各 workgroup が slot に自分の番号を書いて読み戻す） | 重なり無し、全体の結果が一致 | p010 |
| HW9 依存のある dispatch の鎖（clean あり・無し）、compute と描画の混在（Keiland の desktop の上で HW4 を 1000 回）、`vkreset`・`vkcoherency` | 一致、lockup・MMU の違反なし、desktop が止まらない | p010・p011 |
| HW10 計測 | 1×1 conv の GFLOPS（schedule の有無、ldunifa・vector の有無）、TMU の lookup の語 / clock、TMU の access と miss の counter（Z `v3d-job.h` 97〜103 行）、V3D の clock | p010・p011 |
| HW11 公開の確かめ | flag を出した image で noct の accel、libglesv2 が 3.0 のまま、`vkreset`・`vkcoherency` | p012 |

### 3.3 YOLOX-Nano 416×416 の見込み（WS284 §5 と前提を揃えた、L1）

- 前提（WS284 と同じ）: MAC 0.523 G（416）、clock 800 MHz 前後（WS279 は 1.15 GHz を記録したが負荷の下で保つかは未確認、HW10 で読む）、peak 12 × 4 lane × 0.8 G = **38.4 G MAC/s**（FMA は無く 1 MAC = fmul + fadd、2 つの ALU が組めば 1 語）、活性化の読み書き約 130 MB/frame、122 dispatch。
- 1×1 conv（MAC の大部分）: §1.7 の通り、schedule した内側の loop は ALU の上で peak の 75〜85%。だが (1) 1 lane の vec4 の lookup が 1 回の ci の束に 4 つ（16 lane で 256 語）で、TMU の lookup の速さ（slice あたり 1 clock に何語か、**未知**）が律速しうる、(2) 2 thread で出力の FIFO が 8 語のため loop の 1 回に flush が 2 回、(3) 出力の channel の束ごとに入力の tensor を読み直す（C_out / 4 回、最大の tensor 2.8 MB は L2T より大きい、推測）。これらで **schedule 後 15〜45%、source 順 6〜15%** と置く。
- depthwise 3×3: 1 出力に 9 つの vec4 の lookup で memory が律速、5〜15%。
- 時間: 1×1 と stem（約 0.49 G MAC）は schedule 後 28〜85 ms、depthwise 5〜16 ms、memory の律速の小さい op 3〜8 ms、dispatch の固定の分は p011 の後 1〜4 ms（122 × 10〜30 µs、それまでは 122 × 20〜100 µs で 2〜12 ms）。
- **合計: p011 の後 40〜120 ms（8〜25 fps）。最初に動く形（p008 の block の scheduler あり、scalar の load、ldunifa なし）は 100〜250 ms（4〜10 fps）。scheduler が無い（source 順、p007 までの形）と 2〜4 倍遅い**。WS284 §5 の 60〜150 ms と重なる。320×320 は約 0.6 倍（D2 の fallback）。clock が 1.15 GHz なら全部 0.7 倍。
- 比べ: Pi 5 の CPU（A76 × 4、NEON の FMA）は最大 76.8 G MAC/s で、よく作った CPU の実装の方が速い見込み。GPU の利点は CPU を camera・表示・NMS に空ける事（D2: fps の数値の目標は置かず、測って報告する）。

## 4. Phase の案（M9）

1 LW ≈ 実時間 15〜25 分。host の試験は担当、実機は T1。7.1 を先に作り、4.2 は p013。

| Phase | 内容 | 依存 | 見込み |
| --- | --- | --- | --- |
| p001 | この設計（第 2 版） | — | （済み） |
| p002 | encoder（4.2・7.1）: branch（cond・msfign・ub・target）、MULTOP、UMUL24、BARRIERID、TMUWT、TIDX。HT1 | p001 | 3〜5 LW |
| p003 | 試験の道具: lane の model を `plan/tools/v3d-qpu/` に移した物（D5、移すのは Q1）の 7.1 の版を 16 lane・複数 thread・batch・MSF・payload・uniform の巻き戻し・TMU の memory と FIFO・SYNCB・branch・unifa に広げる。rule checker（§1.8、HT-R）と M の規則との対応表 | p002 | 6〜9 LW |
| p004 | compiler の直線の compute（7.1 と共有の部分）: `BCM2711_SHADER_COMPUTE`、preflight、**binary の interface を先に決める**（uniform の種類、local size、threads、shared bytes、barrier・barrier の loop、ldunifa の binding と K。p005 が並べて使う）、payload と `LOAD_SYSTEM`、group の base、NumWorkgroups、整数の乗算・比較・除算（§1.5）、storage の load・store（§1.3 の clamp、§1.4 の TMU の規則、store の ALLNA の branch）、push。source 順の語（schedule なし）。HT2 の直線の program | p003 | 8〜12 LW |
| p005 | 実行器（7.1。vk の code は版に依らない）: `CONFIG_DRIVER_BCM2711_COMPUTE`（既定 n）、queue の flag と限界、STORAGE_BUFFER の descriptor と dummy、compute pipeline、bind point、push、dispatch → CSD（uniform の流れ、arena、supergroup、分割、CFG）、code の execution ごとの upload、barrier の受け付け、indirect、UAPI の注記（L6）。HT3・HT4 | p004 の interface（p004 の前半の後に並べてよい） | 10〜14 LW |
| p006 | **最初の実機の確かめ**（T1）と直し: 試験の program と `plan/ws283/tests/config.mk`、HW0・HW1・HW2・HW3・HW5・HW7 | p004・p005 | 4〜8 LW |
| p007 | 分岐と loop（7.1）: §1.6 の全部（msfign=P、divergence の解析、一様な loop、execute の mask、`ub` の巻き戻し、back edge の生存区間、**MOVE の合併**）、本物の SKIP。HT2 の loop の program、**WS284 の `conv1x1.comp`（P = 4）が 2 thread で compile できる事**、HT5 | p006 | 9〜13 LW |
| p008 | block を持つ一般の scheduler（7.1）: §1.7 の全部（block ごと、compute の終わり、TMU・rtop・unifa・SYNCB の資源、遅延の slot、block ごとの verify と fallback）。HT-R を全 binary に、HT5 | p007 | 8〜12 LW |
| p009 | 共有 memory と barrier（7.1）: arena の base と clamp、`LOAD_SHARED`・`STORE_SHARED`、`BARRIER`・`FENCE`、multi segment、supergroup の barrier の上限、barrier を含む loop の規則（§1.9）。HT2 の reduction | p008 | 5〜7 LW |
| p010 | 2 回目の実機の確かめ（T1）と直し: HW4・HW6・HW8・HW9（混在と vkreset・vkcoherency）・HW10 | p009 | 4〜8 LW |
| p011 | 速さ: vector の TMU の load・store（FIFO の規則）、前段の NonWritable の印（Q1 の了承）と divergence と ldunifa（§1.10）、ldunifa の汚れの表、compute の job の `clean_output` と L2T の clean の省略（HW9 の確かめの後）、dispatch の続けての実行（§2.3 の 6）、fp16 の pack・unpack（D3）、HW10 の再計測 | p010 | 8〜12 LW |
| p012 | 公開: `CONFIG_DRIVER_BCM2711_COMPUTE` の既定を y、HW11 | p011 | 1〜3 LW |
| p013 | 4.2 の port: 版で違う部分の 4.2 の写し、4.2 の scheduler の block 化（accumulator・promote）、4.2 の lane の model の拡張、HT1〜HT5 の 4.2 の版、4.2 の `run_compute` の初めての使用（実機は板がある時、D1） | p011（p012 と並べてよい） | 8〜14 LW |
| p014 | 規約の全文の見直しと license の監査（M の file ごとの license、類似の検出） | p002〜p013 | 3〜4 LW |

**合計 77〜121 LW**（実時間 19〜50 時間）。第 1 版の 51〜77 LW との差は、block の scheduler（p008）、lane の model と rule checker（p003）、4.2 の port を現実の大きさにした事（p013）、2 回の実機の Phase、試験の program を数えた事。直しの量（p006・p010）で上下する。

並べ方と他の WS:

- p005 は p004 の interface が決まった後に p004 の残りと並べられる。p013 は p012 と並べられる。
- **WS284**: p002〜p005（重み・CPU の参照・lavapipe の backend・package）は host だけで WS283 を待たない（WS284 の design §6）。WS284 の Pi 5 の Phase（WS284 p006）は、loop の要る conv1x1 のために **WS283 の p007 の後**（p004〜p006 の直線の compute と最初の実機の確かめを含む）に始められる。第 1 版の番号の「p004（分岐と loop）」は第 2 版の p007。spec constant は H1 の判断で要らない。最初は source 順の語で遅く、p008 の scheduler で 2〜4 倍、p011 で更に速くなる。
- **WS285** の debayer は loop も共有 memory も要らない形にでき、p006 の後に書ける（除算は §1.5 の一般の形）。

## 5. 決まった事と残りの判断

ユーザーの回答（2026-10-23「decisionは推しでいいです」、Q1 経由）:

- **D1**: Pi 4 の実機は完了の条件から外す。4.2 の host の試験（p013 の HT）は完了の条件に残す。
- **D2**: fps の数値の目標は置かない。完了 = GPU で動き参照と一致、速さを測って報告。足りなければ 320×320。
- **D3**: fp32 で正しく動かし、fp16 は保存だけを p011 で。
- **D4**: atomic・storage image・subgroup は最初の範囲の外（§2.6 の制限が付く）。

Q1 の判断:

- **D5**: `plan/ws141/tests/qpu-machine.c`・`qpu-machine.h` と `plan/ws279/tests/qpu71-machine.c`・`qpu71-machine.h` を `plan/tools/v3d-qpu/` に移し、Master の Tools に登録する。使っている所（`plan/ws141/tests/shader-host-test.{c,sh}`、`plan/ws279/tests/shader71-host-test.{c,sh}`、`plan/ws211/tests/glass-shader-host-test.sh`）の path も直す（他の WS の file なので Q1 が行う）。
- **H1**: WS284 は spec constant を使わない（build の時の variant と push constant）。WS283 は今は前段に `OpSpecConstant` を足さない。
- **M5**: COMPUTE の flag は bcm2711 の compute が十分になってから（p010 の実機の後、p012）。`CONFIG_DRIVER_BCM2711_COMPUTE` で gate する（§2.6）。

残り（Q1 に確かめる事。ユーザーの判断が要る物は無い）:

- **Q-a 7.1 を先に作る順**（§1.12）: WS279 のユーザーの好みは 4.2 → 7.1 の写しの向きだった。compute は試せる 7.1 から作り、違う部分を 4.2 に写す案で良いか。逆にすると p006 の最初の実機の確かめの前に 4.2 の版も要り、合計はほぼ同じで実機の確かめが 1〜2 Phase 遅れる。
- **Q-b 共有の前段の変更**（§1.10）: p011 で `struct drv_gpu_shader_ir_uniform` に NonWritable の access の flag を 1 つ足す（他の backend は読まない）。i915・PowerVR・nvrtx・amdrdna と共有の file なので、その時に Q1 の了承を得る。
- **Q-c top の `Makefile` に `CONFIG_DRIVER_BCM2711_COMPUTE ?= n` を足す**（§2.6、p005）。VIDEO と同じ形の 2 行。

WS284 への連絡（Q1 が渡す）: 重みの buffer を `readonly` で宣言する。conv1x1 の出力の channel の束は `gl_WorkGroupID.y`（local size (64, 1, 1)）から取る（ldunifa に要る、§1.10）。P = 4 は 2 thread で動く見込み（§1.11）。L2T の clean は依存のある dispatch の間で省ける見込みで、p011 で HW9 の後に入れる（§2.5）。

後の項目（Future Work の候補、登録は Q1）:

- 前段の `OpSpecConstant*`（`VkSpecializationInfo` か既定値で OpConstant に畳む）と kernel の pipeline の decode。共有の前段の変更で、要る WS が出た時に（H1）。
- atomic（TMU の atomic の op）・storage image・subgroup（D4）。Vulkan 1.0 の compute の必須の機能。
- 1 invocation が長い shader の 2 秒の上限（RK4）: 進んでいる job の上限を延ばすか、shader を中断できるか。

## 6. risk

- RK1 7.1 の slice をまたぐ共有 memory の可視性（§1.9）。HW6 で確かめる。
- RK2 dispatch の 1 回の費用（HW7 で先に測る）。大きければ §2.3 の 6 と §2.5 の clean の省略（p011）。
- RK3 spill が無い: register が足りない kernel は断られる。§1.11 の見積もりと p007 の conv1x1 の compile で早く分かる。WS284 は P を小さくした variant で調整する。
- RK4 2 秒の job の上限: 長い shader は全体の reset になり compositor の仕事も失う（§2.3）。
- RK5 hardware の意味の推測（全 lane が無効の条件付きの TMU、dispatch されていない lane の store、WG_OFFSET を使わない分割、wg_in_mem の巡回、L2T の clean の省略、SFU の RECIP の精度）。各々を HW5・HW2・HW8・HW8・HW9・HW1 で確かめ、駄目なら設計に書いた fallback（branch で囲む、MSF から active、M の arena の数、clean を残す、除算の補正を 1 段増やす）。
- RK6 compiler の分岐と scheduler の変更は graphics の shader にも効く（SKIP の本物化、block の scheduler）。HT5 を p007・p008・p013 で流す。
- RK7 前段の制約: IR は scalar（vector の load は後段でまとめる）、fp16 の型・16 bit の storage は前段に無い（pack・unpack で代える）、spec constant は無い（H1）。
