# Placement fixture supplement

Both bounded suites passed their ordinary and ASan/UBSan variants:

- `timeout 120 sh plan/ws030/tests/run-libvulkan-context.sh`
- `timeout 120 sh plan/ws030/tests/run-libvulkan-external-memory.sh`

The context fixture additionally requires the unchanged numeric G4 ioctl with its 40-byte argument and zero output identities. The external-memory fixture independently checks G33/64-byte framing and exact fields at offsets 0/4/8/16/24/32/36/40/44/48/56, including max DMA address 0x123456780 and alignment 65536. NULL stays legacy; a non-NULL all-zero requirement reaches the new ioctl for K's legacy allocator fallback.

ENOTSUP and ENOTTY return FEATURE_NOT_PRESENT; ENOMEM returns OUT_OF_DEVICE_MEMORY. Each refusal retires the real fake-native allocation and all callback-owned local/command storage, acquire no K alias/backing fd, and do not silently retry through the unconstrained allocator. Native allocation OOM never reaches the placed ioctl. EIO is DEVICE_LOST: local storage is freed but native free is suppressed in the uncertain namespace; one independently counted native record remains until simulated renderer-session destruction.

Existing stock-DMA/negotiated-OPAQUE cases, immutable schema/UUID/type rejection, consume-fd-only-on-success, native import rollback, rounded native extent with original public bounds, source-retirement persistence and real lazy shared mmap assertions remain in place. Context production was unchanged. During review a memory.c reference to context.c's static error helper was identified; the production owner replaced it with local ENOTSUP/ENOTTY/ENOMEM/terminal classification before these successful runs.

The earlier immutable 170-API suite record is retained as historical evidence; this is the additional coverage for the later placement change. Raw source SHA256 values captured immediately after these tests are in the companion JSON.

The final ENOTTY addition was rerun in ordinary and ASan/UBSan modes after the production owner added old-kernel rejection handling. Both passed, with all captured inputs unchanged during this run. The fixture requires exactly one placed ioctl, one native rollback free, no K alias, balanced callbacks, no device loss and no implicit legacy retry. Raw output: `plan/ws014/temp/q310-placement-enotty.log`.
