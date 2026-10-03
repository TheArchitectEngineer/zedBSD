# Gen12 PS/compute ハング 第21報 — golden-context 実装・検証完了。walker ハングは LRC 非依存と確定（golden 仮説の反証）

ご指定の context ライフサイクル（bootstrap A → save via B → inherit/restore to C）を **状態採取込みで実装し、
実機で検証**しました。結論として、**保存機構は本物どおり動作したにもかかわらず walker は同一署名で停止**し、
**停止原因が golden-context state の欠如ではない**ことが確定しました。以下、証跡と次の方向付けのご相談です。

## 1. 実装（selftest 内に自己完結、累積修正は全保持）
- `i915_golden_run()` ヘルパ（request->context を差し替えて1件投入→breadcrumb 完了まで spin）。
- 標準の `drv_i915_lrc_create()` で **ctxA / ctxB / ctxC** を生成、**同一 kernel_vm（同一 PML4/PPGTT）** を共有
  （batch VA が解決し、PDP は 3者同一）。breadcrumb は engine 共通 HWSP なので context 跨ぎの完了追跡は成立。
- **A**: 初回 restore-inhibit（init_regs のまま ctrl=0x00090009）。walkerless で ctx WA（L3ALLOC+FF_MODE2 LRI
  ＋ PIPE_CONTROL flush）を適用。
- **B**: A から切替させ **HW に A を save させる**ためだけの MI_NOOP 要求。
- **C**: A の saved image `[0x1000..0xe000)` を継承コピー → 自 ppHWSP `[0,0x1000)` をクリア →
  RING_START/HEAD/TAIL/CTL・PDP0 を自分に再所有 → **ctrl=0x00090008（restore, inhibit 解除）**。
- prologue は前報の Gen12 EMIT_INVALIDATE（AUX 込み）を保持。default ビルド warning 0。

## 2. 保存機構が本物であることの実測検証
```
golden A.ctrl(初期)      = 0x00090009            (inhibit)
golden step1 A(ctxWA)    rc=0 完了               (A 実行 OK)
golden step2 B(save)     rc=0 完了               (A→B 切替=A save 実行)
golden saved A ctrl      = 0xffff0008            ★HW が restore-inhibit を初回ロード後に自動クリア(=Linux 挙動)
golden saved A layout    s1=0x11081019 s2=0x00002244 s3=0xffff0008  (LRI header / offset 0x2244 / CONTEXT_CONTROL)
golden saved A ringctl   = 0x0000f001  head=0xc0 tail=0xc4          (HW が ring 位置も保存)
golden C inherited ctrl  = 0x00090008  rpcs=0x80041000  ringstart=0x0017c000  (C は自分の ring を指す)
```
→ A の saved image は **HW 由来の本物の context-save**（初期 0x00090009 が save 後 0xffff0008 に変化、
restore-inhibit ビットが HW により消えている＝実際に一度ロード・保存されたことの動かぬ証拠）。
→ C はそれを継承し **restore して実行**した（下記 gC0 が完走）。

## 3. 決定的結果：golden C 上でも walker は停止
golden C（本物の saved image を継承・restore した context）で3ケース実行：
```
gC0    (marker, walker 無)          rc=0  ready=0xc0ffee10 done=0xc0ffee20 eu=0xdead0000  → 完走
gC2    (marker + walker, @shared)   rc=-1 ready=0xc0ffee10 done=0xdead0000 eu=0xdead0000  → HANG
gC3-low(empty  + walker, @low)      rc=-1 ready=0xc0ffee10 done=0xdead0000 eu=0xdead0000  → HANG
   HANG 署名: ipehr=0x70040000 acthd=0x100600150(=batch+0x150) instdone=0xffdeffff fault=0 row=0x8610e87f
```
**gC2 は、第一報群で「同一 HW/VFIO 上の Linux i915 で完走し EU が 0xc0ffee02 を書いた」実証済みの
L-C1（marker+walker, shared VA）と同一ケース**です。それが golden C 上で停止しました。
- `ready=0xc0ffee10`：walker **手前**の CS 側 store は発火（CS は walker まで到達）。
- `eu=0xdead0000`：EU thread が marker kernel を実行して書くはずの 0xc0ffee02 が**書かれない**（EU 未実行/未完了）。
- `done=0xdead0000`：walker 直後の post-sync PIPE_CONTROL が**発火しない**（CS は batch+0x150 で EU drain を無限待機）。

## 4. 帰結（仮説の反証）
**本物の HW-saved + inherited + restored context でも walker は同一ハング**。
→ 停止原因は **golden-context state（LRC per-context image）の欠如ではない**。LRC は差別化要因でない。
第一報群の「同一 batch/kernel/IDD/VFE/walker/state が Linux i915（同一 HW/VFIO/execlists）で完走」と併せると、
**差は『zedBSD が Linux と違えて設定している、LRC にも比較済み GLOBAL レジスタにも含まれない global/GT 状態
または setup 手順』に局在**します。これまでに **除外済み**：batch 内容・kernel・IDD・VFE・walker・physical/VFIO/GPU・
GuC・forcewake(ALL 常時保持)・ring prologue・全比較 GLOBAL レジスタ（MOCS/CMD_*/L3ALLOC/ROW_CHICKEN/DFR/
MISCCPCTL/SAMPLER_MODE 差も無関係）・PAT・そして今回 **LRC/golden-context**。

## 5. 実機の追加状態（今回採取）
```
baseline power: eu_dis(0x9134)=0  slice_ack(0x804c)=0x3  eu_ack(0x805c/0x8060)=0x3/0x3  dss(0x913c)=0x1f(5 DSS, 559 thr)
baseline regdump: 0x2084=0x100 0x20c4=0x306 MOCS 0x4008/0x400c=0x37/0x05 0xb024=0x00100030 MI_MODE 0x209c=0x200
  CONTEXT_CONTROL(global)0x2244=0x08 CS_CHICKEN1 0x2580=0x01 GFX_MODE 0x229c=0x08 MISCCPCTL 0x9424=0xfffffffe
  DFR 0x9550=0x3ff L3ALLOC 0xb134=0xd0000020 L3SQCREG1 0xb100=0xb3400000 L3SQCREG4 0xb118=0x40
  ROW_CHICKEN2 0xe4f4=0xffff4100 ROW_CHICKEN4 0xe48c=0xffff0200 SAMPLER_MODE 0xe18c=0xb021  0x7008=0 0x14800=0
```
→ **EU は電源生存**（eu_dis=0, ack=0x3）。停止は「dispatch 済み EU thread が実行/drain されない」状態。
row_instdone(0xe164)=0x8610e87f は EU-array 側ユニットが busy のまま。

## 6. 伺いたいこと（未比較 global/GT 状態の方向付け）
LRC を反証できたので、次は **「Linux i915 が GT/render 初期化で書くが zedBSD が書いていない、非 context の状態」**を
特定したいです。dispatch は起きている（row 側 busy）が EOT に至らない＝「thread が命令 fetch できない／EOT SEND が
thread-spawner/TDL に届かない／dispatch 中に clock/power gate される」のいずれかと見ています。具体的に：
1. **render/EU の clock・power well**：forcewake-ALL は register 面を起こしますが、**dispatch 中の EU 実行 well／
   DOP clock gating／RC6・render power gate** は別制御でしょうか。ADL-P で「EU は fuse 上 enable だが thread を
   実行させるために init 時 disable すべき clock gate / 立てるべき power well」があればご教示ください（レジスタ名希望）。
2. **L3 の SLM/URB 割当・GPGPU 用 L3 config**：L3ALLOC(0xb134)=0xd0000020 以外に、GPGPU thread dispatch に必要な
   L3 bank/SLM enable や URB 割当（3D/GPGPU 別）で zedBSD が欠く設定はありますか。empty kernel でも停止するので
   SLM 未使用でも要る類なら重要です。
3. **thread dispatch / TDL 関連レジスタ**：EU thread の生成・回収に関与する register（例: dispatch enable、
   thread limit、scoreboard、preempt/thread-group barrier 制御）で、比較リストに入れるべきものは何でしょうか。
   次サイクルで regdump に追加し Linux(intel_reg) と diff します。**優先して読むべきアドレス一覧**を頂ければ即実施します。
4. 反証の妥当性確認：この「golden C でも walker 停止」から **LRC/context を原因候補から外して良い**でしょうか、
   それとも見落とし（例: C の継承で **URB/scratch base や別 context フィールド**を上書きし損ねた等）を疑うべき点が
   ありますか。継承は `[0x1000..0xe000)` 全コピー＋ppHWSP クリア＋ring/PDP/ctrl 再所有のみです。

（インフラ: Linux dev VM(intel_reg)・c2replay・zedBSD selftest(golden 実装+regdump probe)維持。GPU vfio-pci。
 累積修正[PAT/prologue AUX/全 GT・engine init/golden 経路]保持。build warning 0。ご指定あれば regdump 追加 diff を
 次サイクルで即実行します。）
