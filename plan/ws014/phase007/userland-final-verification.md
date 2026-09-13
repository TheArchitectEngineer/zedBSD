# q310 userland final verification

Result: **PASS**. This run follows the optional native OPAQUE negotiation and allocation-profile changes. All 13 jobs completed within their 120-second limits with at most two jobs running concurrently. No aggregate `make check`, production source edit or generated-file replacement was performed.

Noct regenerated eight maintained files byte for byte: public core/external headers, dispatch table, API manifest, opcodes, codec C/header and command-recording include. The actual manifest has 170 commands and the actual opcode enum has 145 entries.

| Check | Result | Scope |
| --- | --- | --- |
| Noct generation | PASS | 137 core + 20 WSI + 13 external/query commands; 145 wire IDs; 100 encoders / 47 decoders; 44 recording functions |
| Independent ABI | PASS | ILP32 and LP64; 148 structures/unions, 942 fields, 2394 enum constants |
| Full DSO dispatch | PASS, ordinary + ASan/UBSan | All 170 real exports linked; independent scopes and extension gating |
| Context / wire | PASS, ordinary + ASan/UBSan | Mapped transport, reply lifetime, deadlines, exact vendor suffix, public OPAQUE/private DMA selection |
| Commands | PASS, ordinary + ASan/UBSan | 44 numeric wire records, >64 KiB copied batches, bounded flush, reset/error/free ownership |
| External memory | PASS, ordinary + ASan/UBSan | Stock DMA and negotiated OPAQUE flags, private WSI DMA, immutable metadata, fd rollback/lifetime, native/public byte bounds and lazy shared mmap |
| External fence | PASS, ordinary + ASan/UBSan | Reference aliases, temporary restoration, native completion, reset generations, reservation rollback and device loss |
| Notifications | PASS, ordinary + ASan/UBSan | Exact/all/any wake, timed-out record retention, saturation fallback, unlocked wait and native failure |
| Core sync | PASS, ordinary + ASan/UBSan | Payload transitions, enqueue rollback, unlocked waits and query preservation |

The source/header/fixture fingerprint map was unchanged throughout the run. Full command lines, timestamps, file fingerprints and log hashes are in [the compact JSON record](userland-final-verification.json); retained raw outputs and generated comparison files are under [the temporary evidence directory](../temp/q310-userland-final-20260913/). The complete input map is in that directory's `results.json`.

These checks establish the recorded library and ABI behavior. Actual GPU output and remote lifecycle acceptance remain separately evidenced by the root-owned QEMU runs; this fixture suite is not a Vulkan conformance claim.
