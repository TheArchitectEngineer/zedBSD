# 提案: include/hal/hal.h の変更 diff（C3 の MSI 分離 + 実 WC mapping 用）

ご判断いただくための **hal.h インターフェース変更案**です。まだ適用していません。
既存の宣言・値は変更せず**追加のみ**。実装(.c)側の変更範囲も併記します。

---

## 1) MSI 分離（handler なし vector/message 確保 と handler 接続を分ける）

既存 `hal_irq_register_msi`/`hal_irq_unregister_msi` は**シグネチャ不変**のまま残し（互換 wrapper 化）、
下位に「handler なし確保」「handler 接続/切離」「解放」を追加します。

```diff
--- a/include/hal/hal.h
+++ b/include/hal/hal.h
@@ -348,6 +348,42 @@ hal_irq_register_msi(
 	paddr_t *mapped_addr,
 	uint32_t *mapped_event);

+/*
+ * Allocate the vector, architecture routing, and message for a message-signalled
+ * IRQ WITHOUT attaching a driver handler.  The allocated vector reaches a valid
+ * IRQ descriptor that safely absorbs an arrival while no handler is attached
+ * (performs the irqchip ack/EOI and records pending), i.e. it never dispatches
+ * through a NULL handler.  "source" is the canonical bus identity.
+ */
+int
+hal_irq_alloc_msi(
+	const char *source,
+	int *mapped_irq,
+	paddr_t *mapped_addr,
+	uint32_t *mapped_event);
+
+/*
+ * Attach a driver handler to a vector previously allocated by hal_irq_alloc_msi.
+ */
+int
+hal_irq_attach(
+	int mapped_irq,
+	hal_irq_handler_t handler,
+	void *handler_arg);
+
+/*
+ * Detach the driver handler.  Returns only after any in-flight invocation of the
+ * handler on any CPU has completed (so the caller may then free handler state).
+ */
+int
+hal_irq_detach_sync(
+	int mapped_irq,
+	hal_irq_handler_t handler,
+	void *handler_arg);
+
+/*
+ * Free a vector allocated by hal_irq_alloc_msi (its handler already detached).
+ */
+int
+hal_irq_free_msi(
+	int mapped_irq);
+
 /*
  * Unregister a handler for a message-signalled logical IRQ.
  */
```

**実装(.c)側の変更範囲**:
- `src/hal/amd64/irq.c`: 既存 `hal_irq_register_msi` の内部を「vector/routing/message 確保」と「handler 接続」に
  分割。`hal_irq_alloc_msi` は前者のみ、`hal_irq_attach`/`hal_irq_detach_sync`/`hal_irq_free_msi` は後者/解放。
  handler 未接続 vector への到着で NULL 参照しないよう、IRQ descriptor が ack/EOI + pending 記録を行う経路を保証。
  `hal_irq_register_msi` = `hal_irq_alloc_msi` + `hal_irq_attach`、`hal_irq_unregister_msi` = `detach_sync` + `free`
  の互換 wrapper に。
- 他アーキ(`src/hal/{arm64,sparcv9,i386,m68k}/irq.c`)は宣言に合わせ**同型の実装 or 未対応スタブ**が必要
  (少なくとも amd64 を実装、他は既存 register_msi のまま + 新規はビルドが要求すれば最小実装/‐ENOSYS)。
  ※ 他アーキへの波及があるため、ここはご判断ポイントです。

---

## 2) 実 WC mapping（write-combining ページ属性）

`HAL_SPACE_*` に WC を 1 つ追加（既存値は不変、次の空きビット 64）。

```diff
--- a/include/hal/hal.h
+++ b/include/hal/hal.h
@@ -486,6 +486,7 @@
 #define HAL_SPACE_NOCACHE		(8)
 #define HAL_SPACE_WRITETHRU		(16)
 #define HAL_SPACE_DEVICE		(32)
+#define HAL_SPACE_WC			(64)	/* write-combining via PAT; falls back to UC where PAT is unavailable */
```

**実装(.c)側の変更範囲**:
- `src/hal/amd64/space.c`: `HAL_SPACE_WC` を受けたら PTE に WC を選ぶ PAT インデックスを設定
  (PAT MSR に WC エントリを確保 or 既存の WC エントリを参照)。PAT 不可なら UC(NOCACHE)にフォールバック。
  `arch_phys_wc_add` 相当(MTRR)は「PAT 利用可なら必須ではない」参照挙動に合わせる。
  既存 attr マスク検査(space.c:256 等)に WC ビットを追加。
- x86 側 CPU PAT と、GPU の private PAT(i915 の 0x4800 系)は別物として扱う(混同しない)。

---

## 影響と留意
- **hal.h は追加のみ**(既存 API・値の変更なし)。ビルド影響は新規シンボルの未定義参照
  (amd64 実装で解決、他アーキは要スタブ)。
- MSI 分離は `src/hal/amd64/irq.c` の内部再構成 + IRQ core(msi-source / IRQ descriptor)の handler 未接続時挙動の
  実装が本体。WC は `space.c` の PTE/PAT 実装が本体。
- ご承認いただければ amd64 を実装し、他アーキは最小スタブ(未対応 or ‐ENOSYS)で通します。他アーキの扱いのご希望
  (実装 / スタブ / ビルド対象外)があればご指示ください。
