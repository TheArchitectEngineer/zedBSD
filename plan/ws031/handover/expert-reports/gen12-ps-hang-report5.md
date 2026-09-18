# Gen12 PS ハング 第5報 — RT write の形式は無関係。dispatch/実行の問題に収束

## 決定的な切り分け: RT write の形式を変えても不変

参照カーネル（Mesa 生成）の RT write を 3 通りに変えて実機投入:

| RT write の形式 | 結果 |
|---|---|
| `sendc.render` 実 RT write（Mesa そのまま） | ハング |
| `send.render` 実 RT write（scoreboard 依存待ちを除去） | ハング |
| `sendc.render` **null_rt**（メモリに書かず完了通知のみ） | ハング |

すべて `ps=0`、`markerB=0`、WMFE/PSS not-done、RCC idle。

→ **RT write の宛先 surface も、sendc の scoreboard 依存待ちも、原因ではない。**
PS スレッドは、どの終端形式でも**実行を完了（retire）しない**。

## 収束した2つの可能性

RT write の形式に依らずスレッドが完了しないことから:

1. **windower が PS スレッドを dispatch していない**（固定機能 windower ステートの問題）
2. **スレッドは dispatch されるが、命令フェッチ／実行の最初期段で停止**
   （EU が Instruction Base 相対の正しい場所からカーネルをフェッチできていない）

`ROW_INSTDONE`（MCR steered）が idle=0xffffffff → hang=0x8610e87f で **EU not-done** を示すこと
（＝スレッドが EU 上に存在）から、**(2) 命令フェッチ/実行段での停止**が有力です。
その場合、次が矛盾なく説明できます:
- ps=0（retire していない）
- RCC idle（send がまだ実行されていない）
- 全 RT write 形式で同一（send までたどり着いていない）
- EU not-done（スレッドは EU 上にいるが先頭で停止）

補足: カーネルバイトはメモリ上の正しい位置（`0x100400400` = Instruction Base + KSP0）に
CPU 読み戻しで確認済み。`wbinvd` でも不変。だが EU がそこを実行できていない可能性。

## これまでに Mesa 一致を確認済み（第1〜4報）
kernel（refps = brw_compile_fs、split send 込み）、3DSTATE_PS（dispatch_grf_start=2, KSP, dispatch）、
RENDER_SURFACE_STATE（isl_surf_fill_state）、URB（intel_get_urb_config: 3576 entries, deref SIZE_32）、
3DSTATE_WM（BLORP と同じ空）、SBA 全 DWORD、CTX_CONTEXT_CONTROL、RPCS、L3、MOCS、workaround 群。

## 伺いたいこと（核心）

1. **PPGTT batch から実行する 3D パイプラインで、EU の PS 命令フェッチ（STATE_BASE_ADDRESS の
   Instruction Base 相対の KSP）は、CS の MI_STORE と同じ PPGTT 変換を使いますか？**
   別経路（専用 translation、GGTT、あるいは instruction base に固有の要件）はありますか。
   Instruction Base に PPGTT 番地 `0x1_00400000`（bit32 セット）を与えていますが、EU フェッチが
   別空間や下位32bitのみを見て、別ページ（ゼロ/ガベージ）をフェッチしてハングする可能性は？

2. **Gen12 で「PS スレッドが dispatch されたか」「どの命令で停止しているか」を確定する方法**を
   ご教示ください（SIP / EU_ATT / TD_CTL / per-thread IP など、MMIO/MCR で読めるもの）。

3. windower が PS を dispatch する際に、**Instruction Base の適用や I-cache invalidate のタイミングで
   Gen12 特有の要件**（PIPE_CONTROL の instruction cache invalidate の位置、CS_STALL の要否など）は
   ありますか。現在は STATE_BASE_ADDRESS の直後に instruction cache invalidate の PIPE_CONTROL を
   入れています。

## 次の自前検証
コンパイラでメモリ store を行う PS を生成し、PS が実際に実行されるか（marker が書かれるか）を
確定させる予定です。これで dispatch/fetch の可否が切り分けられます。

（インフラ: refps/refsurf/refurb ハーネス構築済み。brw_compile_fs / isl / URB config を standalone で
叩けます。gentool で任意の EU カーネルを assemble/disassemble 可能。）
