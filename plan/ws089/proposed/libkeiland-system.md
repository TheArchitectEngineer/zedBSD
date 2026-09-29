# 案: libkeiland の機械の情報（ws089-p001、未適用）

所有: libkeiland は WS035。About の memory の行のため（p002 では memory の行を出さず、許可の後に足す）。

```c
/* The machine as the kernel reports it (/dev/system), for About. */
struct keiland_system_info {
	uint64_t memory_total;		/* bytes, vm_statistics.physical_total */
	uint64_t memory_free;		/* bytes, physical_free */
	uint64_t swap_total;
	uint64_t swap_free;
};

int keiland_system_get_info(struct keiland_system_info *info);	/* 0 or an errno value */
```

- 新しい file `userland/desktop/libkeiland/system.c`（約 80 行、`/dev/system` の `KERN_SYSTEM_IOC` の vm_statistics）、keiland.h の節、
  exports.map の `keiland_system_*`。
- uptime は `clock_gettime(CLOCK_MONOTONIC)`（POSIX）で足りるので含めない（p002 で boot からの時間と一致するか確かめる）。
