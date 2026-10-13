<!-- awesome-plan project=zedbsd record=ws171-p002 -->
# ws171-p002: hal.h の契約の comment の差分の案

Status: 案が全部そろった・ユーザーの review 待ち（2026-10-14 P3 q966: 6 領域の spec と diff。hal.h には未適用）
Parent: [WS171](../ws.md)
Queue: q966（P3、2026-10-14。ユーザー: WS171 は優先度低め、やることが無い時に）
依存: p001（[inventory.md](../phase001/inventory.md)）

**include/hal/hal.h には適用しない。** comment だけの変更でも HAL の API の変更なので、ユーザーの review と承認を得てから p003 で当てる（Guardrail の HAL）。

## 作り方（再現できる形）

| file | 役目 |
| --- | --- |
| `spec/NN-area.txt` | 領域ごとの新しい comment。`@@ name`（行の頭が `name(` の宣言）か `@@ ^regex`（宣言の最初の行）の後に comment の block。その宣言の直上の comment を置き換える（無ければ挿入する） |
| `apply.py HEADER SPEC` | spec を 1 つ当てる |
| `check.py ORIG NEW` | comment を除いた token の列が同じか（宣言・型・macro が不変か）、複数行の comment の `/*`・`*/` が単独の行か（coding-style §10） |
| `gen.sh WORKDIR` | `include/hal/hal.h` を WORKDIR に写し、spec を名前の順に重ねて当て、`diff/NN-area.diff`（前の spec の上の差分）と `diff/all.diff`（全部、今の hal.h に対して）を書く。hal.h は触らない |

```
mkdir -p build/tmp && w=$(mktemp -d build/tmp/ws171.XXXX) && plan/ws171/phase002/gen.sh "$w"
git apply --check plan/ws171/phase002/diff/all.diff
```

review は `diff/NN-area.diff` を領域ごとに見る。承認された領域の spec だけを残して gen.sh を流せば、当てる差分ができる。

## 方針

- 雛形は p001 の通り（HAL v2 の comment）: 何をするか、呼んでよい文脈、他の HAL の操作との関係、返り値と各 error、対応しない machine の扱い。
- 契約は支えている arch（amd64・aarch64、おまけの i386）の実装を読んで、全部が満たす事を書く。合わない所は下の「実装との差」に出し、契約の文は案として示す（決めるのはユーザー）。sparcv9・m68k はサポート外（master の「対象 platform」）で、照合はしたが契約の根拠にしない。
- 既にある HAL v2 の contract の comment（hal_irq_set_wake・suspend・resume、hal_cpu_notify ほか）は変えない。hal_irq_alloc_msi・detach_msi_sync・free_msi は文を保ち、引数と返り値の段を足した。

## 領域と状態

| # | 領域 | 関数 | spec / diff | 状態 |
| --- | --- | --- | --- | --- |
| 1 | 文字列と console・assert・fatal（と `HAL_ASSERT`・`HAL_FATAL`） | 11 | [spec](spec/01-runtime.txt) / [diff](diff/01-runtime.diff) | 案（2026-10-14） |
| 2 | CPU・mask・idle・panic・halt・reset・poweroff（SMP の節の説明、`hal_cpu_id_t`・`struct hal_cpu_mask`） | 20 | [spec](spec/02-cpu.txt) / [diff](diff/02-cpu.diff) | 案（2026-10-14） |
| 3 | IRQ（と `HAL_IRQ_*`・`hal_irq_ack_t`・`hal_irq_handler_t`） | 19 | [spec](spec/03-irq.txt) / [diff](diff/03-irq.diff) | 案（2026-10-14） |
| 4 | task と context（と `hal_task_t`、debug の register） | 28 | [spec](spec/04-task.txt) / [diff](diff/04-task.diff) | 案（2026-10-14） |
| 5 | 空間・pmem・mmio・io・barrier・cache（`HAL_SPACE_*` の属性と flag、`hal_space_t`） | 47 | [spec](spec/05-memory.txt) / [diff](diff/05-memory.diff) | 案（2026-10-14） |
| 6 | rtc・entropy・get（`struct hal_memstat`） | 5 | [spec](spec/06-misc.txt) / [diff](diff/06-misc.diff) | 案（2026-10-14） |

- 全部を当てた hal.h を p001 の `inventory.py` で数え直すと contract 81・short 33・none 16（none は全て、群の頭に 1 つの comment を置いた群の 2 つ目以降: `hal_rmb`・`hal_wmb`、`hal_io_rmb`・`hal_io_wmb`、`hal_io_inp16`〜`hal_io_outp32`、`hal_mmio_read16`〜`hal_mmio_write64`）・placeholder 0。short の 33 は 1〜2 文で足りる物（`hal_task_get_tls` など）。
- 「XXX: Add explanation.」と「XXX: Add a single-line explanation here.」は無くした。名前を変える案の XXX（`hal_halt`・`hal_space_prot_query`・`hal_space_query`・`hal_space_clear_flags`）はユーザーの問いなので残した（`hal_halt` の「hal_cpu_idle() と同じか」には comment の中で答えた）。
- 節の説明（「HAL C runtime」の「HAL と early init だけで使う」）は変えていない（H18）。

## 実装との差（ユーザーの判断か bug。契約の案は amd64 の動作に合わせた所が多い）

| ID | 関数 | 差 | 案 |
| --- | --- | --- | --- |
| H1 | hal_irq_mask・unmask | 無い IRQ: amd64・i386 は黙って無視、aarch64 は HAL_FATAL。amd64 は IRQ 0（tick）を無視、i386 は IRQ 0（PIT の tick）も mask する | 契約は「IRQ は存在すること」だけ（どちらの扱いも書かない） |
| H2 | hal_irq_register | 既に handler がある IRQ: amd64・i386 は HAL_ERR_BUSY、aarch64 は黙って置き換える | 契約は BUSY。aarch64 は bug 候補（src/hal の直しは承認不要） |
| H3 | hal_irq_unregister | amd64 は mask し、他の CPU で走る handler の終わりを待つ。i386 は mask し、走っていれば BUSY。aarch64 は mask せず待たない（Pi 4 の SMP（WS208）で効く） | 契約は「mask し、HAL_OK なら走っている呼び出しは無い」。aarch64 は bug 候補 |
| H4 | hal_irq_unmask・handler の無い IRQ | handler の無い IRQ が来た時、amd64・i386 は mask して EOI、aarch64 は EOI と表示だけ（level の IRQ なら続けて来る） | 契約は「acknowledge して mask し直す」。aarch64 は bug 候補 |
| H5 | hal_irq_set_affinity | 届けられる CPU が mask に無い: amd64 は INVALID、i386 は UNSUPPORTED。aarch64 は CPU 0 を含む mask だけ受け、いつも CPU 0 | 契約は INVALID。i386 は bug 候補（おまけの arch） |
| H6 | hal_irq_get_affinity | effective: amd64 は route した 1 つの CPU、aarch64 は CPU 0、i386 は requested の mask そのまま（route は最初の 1 つなので、複数の CPU の mask では違う）。NULL の結果: amd64 は INVALID、i386・aarch64 は飛ばす | 契約は「NULL を渡さない」。i386 の effective は bug 候補 |
| H7 | hal_irq_set_mode | amd64 は自分で mask して戻す、aarch64 は unmask の IRQ を BUSY で断る | 契約は元の文（呼び手が mask する）に「BUSY で断ってよい」を足した |
| H8 | hal_irq_send_eoi | 順の検査: amd64 は厳密な stack、aarch64 は 1 つだけ、i386 は IRQ ごと | 契約は「この CPU が持つ ack でなければ HAL_FATAL」と「取った逆の順に終える」 |
| H9 | hal_task_create | 新しい task の CPU: amd64・i386 は作った CPU、aarch64 は CPU 0 | 契約は「HAL が最初に決めた CPU、hal_task_transfer() まで」。差は kernel が transfer してから switch するので無害 |
| H10 | hal_task_get_tls | 走っている task: amd64 は MSR の今の値、aarch64・i386 は保存した値（aarch64 は利用者が TPIDR_EL0 を直に書ける） | 契約は値の出所を書かない。aarch64 の走っている task の値は bug 候補 |
| H11 | hal_task_transfer | i386 は target の CPU が online かを見ない（STATE を返さない） | おまけの arch、記録だけ |
| H12 | hal_task_signal_enter の restorer | aarch64 は x30 に入れる、amd64・i386 は使わない（kernel が user の stack に置く） | 契約に両方を書いた |
| H13 | hal_task_get/set_user_*regs・single_step・debug_points | i386 は定義が無く、`include/hal/arch/i386.h` に HAL_DEBUG_POINT_MAX も無い（kern/ptrace.c が i386 で build されるかは p003 で確かめる） | 記録だけ |
| H14 | sparcv9・m68k の hal_irq_get_affinity | 宣言と違う形（`struct hal_irq_affinity *`）で、hal_irq_register・unregister が無い（`hal_irq_set_handler` だけ） | サポート外、記録だけ |
| H15 | exec・fork・signal・get_user_context・register の関数 | 返り値は HAL_OK などではなく 0 と -1 | 契約にそのまま書いた（型を変えるのは API の変更） |
| H16 | hal_printf | 変換: amd64・i386 は c s d u x X %、aarch64 は c s u x p %（d と X は「?」を出す）、amd64・i386 に p は無い | 契約は共通の c s u x % と幅・0・l/ll だけ。aarch64 の d・X は bug 候補 |
| H17 | hal_assert・hal_fatal | amd64 は他の CPU も止める（hal_cpu_panic_all）、aarch64・i386 は呼んだ CPU だけ（aarch64 は画面にも描く） | 契約は「呼んだ CPU が止まる」だけ。揃えるかはユーザーの判断 |
| H18 | 「HAL C runtime」の節 | 「HAL と kernel の early init だけで使う」とあるが、kernel は hal_fatal・HAL_FATAL を各所で使う（kern/entry.c・sched.c ほか） | 節の文を直すか使い方を直すかはユーザーの判断（案では変えていない） |
| H19 | hal_cpu_notify_mask | online でない CPU: aarch64 は STATE、amd64 は INVALID（hal_cpu_notify は STATE）。空の mask: amd64 は HAL_OK、aarch64 と UP は INVALID。UP は CPU 0 だけの mask に UNSUPPORTED | 契約は hal_cpu_notify と同じ INVALID・STATE・UNSUPPORTED。amd64 は bug 候補、空の mask は決めていない |
| H20 | hal_reset・hal_poweroff・hal_panic | aarch64（と m68k・sparcv9 の reset）だけが定義し、amd64・i386 は定義しない（kernel の platform の code が ACPI などで行う）。hal_panic は kernel から呼ばれていない | 契約に「定義しない HAL がある」と書いた。hal_panic を残すかはユーザーの判断（消すのは API の変更） |
| H21 | hal_dcache_* | aarch64・m68k だけが定義（kernel の dcache.c はそれらだけで build） | 契約に書いた |
| H22 | 空間 | aarch64 の hal_space_map は書けて実行できる page と RAM の外の device の page を INVALID で断る、clear_flags は ACCESSED を消さない（控えめ）、flush_tlb_range は space 全体、get_page_size(3) は aarch64 で 1 GiB・amd64 で 0 | 契約に「断ってよい」「控えめでよい」「多く flush してよい」と書いた |
| H23 | hal_space_map_device | amd64 は WRITETHRU を、aarch64 は WC を UNSUPPORTED | 契約に書いた |
| H24 | SMP の節 | 「secondary は HAL の中で初期化され kernel_main() の時に IPI を待つ」は今の動作（hal_cpu_start_others が kernel から起こす）と違う | 節の文を直した（案） |
| H25 | hal_rtc_read_epoch_time | rpi4 はいつも false（RTC が無い） | 契約の false の場合に入る |
| H26 | hal_cpu_panic_all | aarch64 は mask できる SGI で止めるので、割り込みを切って走る CPU は止まらない（amd64 は NMI） | 契約に書いた |

## 確認

- 2026-10-14 `gen.sh`（6 つの spec）: 各 spec の後と全部で `declarations unchanged: yes`、`comment delimiter problems: none`。`git apply --check diff/all.diff` 成功（今の main の hal.h に当たる）。hal.h は不変（worktree の include/hal/hal.h は変えていない）。
- build は不要（comment だけ。p003 で当てた後に amd64・aarch64・i386 の vmunix を build する）。

## 残り

- ユーザーの review（Q1 経由）: 領域ごとの diff と、上の H1〜H26 の判断。承認された領域の spec だけで gen.sh を流し、p003 で当てる。
- bug 候補（H2・H3・H4・H5・H6・H10・H16・H19）は p003 で bug にするか、src/hal の直し（承認不要）として別の Phase に。
