# ws029 host fixture の整理（i915 再構築 S5）

`sh plan/ws029/tests/run-i915-host-tests.sh` の既定一覧は `ppgtt stream`。各 fixture は試験する driver file を
kernel と同じく別の翻訳単位として link し、残りの kernel は `i915-host-stubs.inc` が与える
（kernel allocator、4 GiB 超の偽 page pool と direct map、到達しない依存の stand-in は呼ばれたら abort）。

## 新ファイルへ移した fixture

| fixture | 旧 | 新 |
| --- | --- | --- |
| `i915-stream-test.c` | legacy `i915.c` の `drv_i915_stream_parse`（全 legacy file を include） | `src/drivers/gpu/i915/command.c` の `drv_i915_stream_parse`。検査内容は変更なし（正常 3 形、不正 13 形） |
| `i915-ppgtt-test.c`（新） | `i915-gtt-test.c` の PPGTT 3 試験（legacy `ppgtt.c`） | `src/drivers/gpu/i915/ppgtt.c` の session address space（`drv_i915_ppgtt_*`）。scratch 連鎖、4 段 walk と table 確保、clear、VA allocator に、uncached insert（PAT 3）と不正引数 2 形を追加。旧 `drv_i915_ppgtt_lookup` は本番から削除されたため、fixture 側で GPU と同じ walk をする |

## 廃止した fixture（本番から到達しない legacy code の試験）

| ファイル | 試験していたもの | 理由 |
| --- | --- | --- |
| `i915-uncore-test.c` | legacy `uncore.c`（forcewake 参照計数、GDRST、範囲外 MMIO 拒否） | legacy `uncore.c` は廃止。forcewake/MMIO は `mmio.c` が持ち、その試験は contract（T2）の担当 |
| `i915-gtt-test.c` | legacy `ggtt.c`（GMCH からの table 寸法、scratch fill、first-fit allocator、insert/clear、probe 失敗）と PPGTT | legacy `ggtt.c` は廃止（GGTT は `ggtt.c` の GT 側に統合され、legacy の GGTT 確保は本番に無い）。PPGTT 部分は `i915-ppgtt-test.c` へ移した |
| `i915-irq-test.c` | legacy `irq.c`（enable/mask 値、bank/identity decode、CS error） | legacy `irq.c` は廃止。割込みは新 `irq.c`（GT）と `display/interrupts.c` |
| `i915-lrc-test.c` | legacy `lrc.c`（context image、ELSQ、CSB、ring） | legacy `lrc.c`・`engine.c` は廃止。execlists は `context.c`・`submit.c` |
| `i915-backend-test.c` | legacy `i915.c` の backend ops（session/resource/stream/job、selftest、hang→reset） | legacy backend と attach 時 selftest は廃止。ops は `session.c`・`resource.c`・`command.c`・`job.c`、実機試験は T4a |
| `i915-fixture.inc` | 上記と旧 vk fixture の共通土台（偽 BAR0/GTT、execlists emulator、legacy device attach） | 旧ツリーの `internal.h` と legacy file に依存し、使う fixture が無くなった |

関連する変更:

- `run-i915-analyzer.sh`: 対象を新ツリーの `src/drivers/gpu/i915/*.c`（`render/`・`compiler/` は
  `plan/ws031/tests/run-vk-analyzer.sh`）へ。廃止した `CONFIG_DRIVER_PCI_I915_SELFTEST` の定義を外した。
  compile できない source があれば失敗し、警告数は報告する。
- `run-i915-build-selection-test.py`: i915 object を旧 9 file の名前ではなく、`AMD64_I915_SOURCES` の全 source と
  1 対 1 で照合し、必須 object の存在と、試験 build（`I915_TESTS`）以外で `tests/` が link されないことを確かめる。
- `config-i915-selftest-amd64.mk` と実機 loop（`run-i915-remote.py`、`i915-qemu.py`、`campaign.sh`）は変更していない。
  新 driver には attach 時 selftest が無いため `CONFIG_DRIVER_PCI_I915_SELFTEST` は何も選ばない。
