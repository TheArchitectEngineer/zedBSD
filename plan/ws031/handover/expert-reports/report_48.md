# WS031 第48報 — timer HAL 境界の確認(第一提出物)。one-shot 化のうち境界不変で進める範囲と、レビューが必要な範囲を分離

ご指示どおり、LAPIC のモード変更に先立ち、**現行 timer インタフェースの宣言・呼出し関係・変更案の対応表**を提出します。実コードから抜き出した事実に基づき、「境界の契約が変わるか」で判定しました。今回は**コード適用なし(分析)**。並行して進められる作業(fuse 遅延試験・純粋な期限計算・probe 接続 diff)は別途着手します。

---

## 1. 現行 timer 境界(実コードからの抜粋)

### (a) HAL → kernel の唯一の timer 契約 — `kernel_timer_handler`(`include/hal/hal.h`)
```c
/* Scheduling timer callback. */
void kernel_timer_handler(hal_cpu_id_t cpu, hal_irq_ack_t acknowledge);
```
- 契約(現行):**「スケジューリング tick(論理 10ms)が発生した」**という通知。各アーキの IRQ ハンドラが呼ぶ。
- 呼出し元(amd64):`src/hal/amd64/irq.c`(IRQ_TIMER 専用パス)
  ```c
  if (irq == IRQ_TIMER) {
      clock_handler();                       /* amd64 bsp 診断 */
      cpu = hal_cpu_current();
      kernel_timer_handler(cpu, acknowledge);/* ← HAL→kernel 通知 */
      return;
  }
  ```
- kernel 側受け口(`src/kern/clock.c`):`kernel_timer_handler` が CPU0 で `kernel_ticks++`(**IRQ 一回=1 tick**)、`process_timer_tick(now)`、全 CPU で `sched_clock_cpu(cpu, now)` を呼ぶ。`now` は tick 単位。

### (b) HAL 内部の timer 制御 — `amd64_lapic_timer_*`(`src/hal/amd64/bsp-pcat/lapic.h`)
```c
int  amd64_lapic_timer_start(void);   /* 較正済み periodic LAPIC timer 開始 */
void amd64_lapic_timer_stop(void);
```
- **amd64 HAL 内部専用**(lapic.h は HAL 内ヘッダ、hal.h ではない)。呼出し元は amd64 bsp `clock.c` の `prekern_bsp_timer_init` のみ。**kernel からは呼ばれない**。
- 現状は **periodic モード**(自動リロード、LAPIC_TIMER_INITIAL 再設定不要)。

### (c) kernel → HAL の timer 設定 API — **存在しない**
- `include/hal/hal.h` に timer の周期設定・one-shot・arm 等の kernel→HAL API は**ありません**。**kernel は timer 発火時刻を HAL に指示できない**。HAL が自律的に periodic tick を出すだけ。
- kernel 側の tick 読出しは `clock_ticks()`(`include/kern/clock.h`, KERN_CLOCK_HZ=100)/ `sched_ticks()`(`include/kern/sched.h`)。単位=論理 tick。

### (d) `amd64_lapic_timer_arm` — **現状存在しない**(第47報で新設予定として言及したもの)

## 2. 呼出し関係(要約)

```
LAPIC periodic IRQ (amd64)
   └─ amd64/irq.c: IRQ_TIMER パス
        └─ kernel_timer_handler(cpu, ack)         [HAL→kernel 境界: hal.h]
             ├─ CPU0: kernel_ticks++ (IRQ=1 tick)
             ├─ CPU0: process_timer_tick(now)
             └─ sched_clock_cpu(cpu, now)          [tick 単位 now, sleep 期限も tick 単位]

amd64_lapic_timer_start/stop  [HAL 内部, lapic.h] ← amd64 bsp clock.c のみ
kernel → HAL の timer 設定     : 無し
```

## 3. 現行 → 変更案 → 利用者への影響(対応表)

| 要素 | 現行 | 変更案 | 利用者への影響 | 判定 |
|---|---|---|---|---|
| `amd64_lapic_timer_start/stop`(lapic.h) | periodic 開始/停止 | one-shot モードを内部に追加(レジスタ操作・較正値の再利用) | amd64 HAL 内のみ。外側契約同じ | **内部変更(レビュー不要)** |
| `amd64_lapic_timer_arm(counts)`(新, lapic.h) | — | one-shot 期限を LAPIC counts で設定。**呼出しは amd64/irq.c(HAL 内)から** | amd64 HAL 内のみ。kernel から呼ばない | **内部変更(レビュー不要)。ただし kernel から呼ぶ設計にした時点で境界追加=レビュー** |
| amd64 IRQ timer パス(irq.c) | periodic IRQ → kernel_timer_handler | one-shot IRQ → **次の 10ms 境界を amd64 timecounter から算出し再武装** → 10ms 境界のときだけ kernel_timer_handler を1回呼ぶ | kernel_timer_handler は**従来どおり 10ms ごとに1回**。kernel から見て不変 | **内部変更(レビュー不要)** |
| `kernel_timer_handler`(hal.h) | 「10ms tick 発生」通知 | 段階1では**不変**(10ms ごと1回)。段階2で sub-tick 通知に意味を広げる案は**契約変更** | 段階2の意味変更は kernel_ticks++ 前提を壊す | 段階1=不変 / 段階2の意味変更=**レビュー** |
| kernel→HAL「期限 T で発火」API(新) | 無し | sub-tick sleep が特定時刻の起床を要求するには必須 | HAL 境界の**新規追加** | **レビュー必須** |
| `sched_ticks()`/`kernel_ticks` の単位 | 論理 tick(10ms) | 内部で単調時刻から算出しても**単位・意味は維持** | 生 counter/ns へ黙って置換しない | 維持なら不変 / 置換は**レビュー** |
| `waitq_sleep()` の deadline 引数 | tick 単位 | sub-tick を渡すには counter 単位の新表現が必要 | 型/意味の変更は境界に出る | 変更なら**レビュー** |

## 4. 判定と段階分け

- **段階1(one-shot で論理 100Hz tick を再現)= 境界不変・内部変更**。LAPIC を one-shot 化し、amd64 IRQ パスが amd64 timecounter から次の 10ms 境界を算出して再武装、10ms 境界のときだけ `kernel_timer_handler` を1回呼ぶ。`kernel_timer_handler` の契約(10ms ごと1回)・`kernel_ticks`/`sched_ticks()` の単位は不変。**この範囲は amd64 HAL 内に収まり、レビュー前でも進められます**が、ご方針に従い**適用前に段階1の diff(lapic.c / amd64 irq.c、hal.h は無変更)をレビューへ出します**(境界不変であることの確認を含めて)。
- **段階2(sub-tick sleep の期限を同じイベント管理へ追加)= 境界追加が不可避 → レビュー必須**。理由:**現状 kernel は HAL に「時刻 T で起こせ」と言う手段が無く、HAL は sub-tick イベントを scheduling tick と区別して通知する手段が無い**。したがって次のいずれかの**新しい境界インタフェース**が要ります(未適用 diff としてレビューへ提出、確定はレビュー後):
  - **案A(kernel→HAL arm)**:kernel の sleep 登録が最早 sleep 期限を HAL に渡し、HAL が `min(次 tick, 最早 sleep 期限)` で one-shot 再武装。返却契約=成功/未対応/設定失敗を区別。CPU 条件=現 CPU、IRQ 内可否を明記。
  - **案B(HAL→kernel query)**:HAL の再武装時に kernel へ「最早 sleep 期限」を問い合わせる。sub-tick 起床は `kernel_timer_handler` とは別の通知にする(scheduling tick の意味を変えない)。
  - どちらも「期限の単位・基準時計、CPU・IRQ 文脈、再設定/過去期限/取消後割込み/実行中同期、返却契約」を明記して提出します。ここで新 API 名/シグネチャは私が確定せず、既存 HAL 設計に合わせた最小追加のみをレビュー対象にします。

**KERN_CLOCK_HZ は上げません。idle 時 tick 停止・全面 tickless は今回の完成条件に含めません**(段階1は論理 100Hz tick の維持、段階2は sub-tick sleep 期限の追加まで)。

## 5. レビューを待たず実施した作業(本報で完了・GPU-free 検証、ktest 159→169/0、image da3a74c9)

- **A. 純粋な次イベント選択・tick 更新の計算 + 試験(完了)**:HW 非変更の純関数 `timer_calc.{c,h}`(`parity_timer_next_event` / `parity_timer_on_fire` / `set_sleep` / `clear_sleep`)を新設し、fake 時刻で検証。合格した観測:①次 tick=10ms・sleep=2ms → **次イベント=2ms**、②2ms の sleep 満了で **10ms tick を加算しない(0 tick)**、③最早 sleep 取消で **tick を選び直す**、④tick 満了は **ちょうど 1 tick**、⑤複数 set_sleep は**最早を保持**、⑥**遅延発火は跨いだ tick を全て配信し期限を now の先へ**(過去期限を待たない)、⑦**sub-tick 発火は tick として数えない**。単位・意味は既存 tick を維持(生 counter/ns へ置換せず)。LAPIC 再接続は境界確認後。
- **B. fuse 遅延試験(完了)**:PG1 が遅れて成立する fake 入力で、fuse 待機(fast=2µs→slow=1ms)が**遅延した PG1 を観測して継続**。PG1 が永久に来ない入力では **HW timeout を警告して enable 継続(-EIO でない)**=時間源異常と区別。scripted 成功を実 sub-tick sleep の検証済とは記録しません。
- **C. probe 接続 diff の準備(次報で提出)**:probe.c の UNIMPL を試験済み `intel_power_domains_init_hw(false)` 相当へ置換する diff を用意(同一 device 状態・INIT 参照保持・fault 確認・DMC 入口停止)。**timer 受入まで実 GPU では有効化しません**。GPU-free では引き続き probe=NOT_RUN が正しい結果です。

## 6. 保持と次の提出

第47報で完了した**実 preemption 試験・PCODE 追加区間 2 試験(159/0)**、および D0・CDCLK・D3 親・実 sleep(hybrid)は成果として保持します。timer 変更後はこれらを回帰試験として再実行するだけで足り、設計・受入をやり直しません。boot が壊れた場合の復旧版は **3098b4ff**(hybrid)を使用し、新変更の diff と失敗ログは保存、無関係な GPU 側の累積修正までは戻しません。

次報の第一提出物:**段階1の未適用 diff(lapic.c one-shot / amd64 irq.c 再武装、hal.h 無変更)+ 境界不変の確認**、および案A/案B の**未適用インタフェース diff**(期限表現・CPU/IRQ 文脈・設定/取消/過去期限/同期・返却契約・影響範囲)。あわせて fuse 遅延試験の結果、純粋な次イベント計算の試験結果、probe 接続 diff。実 GPU は timer 受入後。
