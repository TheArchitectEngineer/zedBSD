# 案: libkeiland の interface の詳細と DNS（ws089-p001、未適用）

所有: libkeiland は WS035。ws089-p003 で足す。networkd の protocol は変えない（SHOW の出力は address を持たないので、socket の ioctl で読む）。

```c
/* One interface's details, read from the kernel (not from the daemon). */
struct keiland_network_link {
	char name[KEILAND_NETWORK_NAME_MAX];
	unsigned up;			/* IFF_UP and IFF_RUNNING */
	unsigned loopback;
	char address[16];		/* dotted IPv4, empty when none */
	char netmask[16];
	unsigned char hardware[6];	/* MAC */
	unsigned mtu;
	uint64_t received_bytes;	/* SIOCGIFSTATS */
	uint64_t sent_bytes;
};

/* Lists the interfaces (SIOCGIFCONF), up to capacity; returns how many there are. */
size_t keiland_network_get_links(struct keiland_network_link *links, size_t capacity);

/* Copies the DNS servers of /etc/resolv.conf (up to capacity, dotted IPv4); returns how many. */
size_t keiland_network_get_dns(char (*servers)[16], size_t capacity);
```

- `userland/desktop/libkeiland/network.c` に約 150 行（または新しい file `link.c`）、keiland.h の構造体と 2 つの宣言。exports.map は既存の
  `keiland_network_*` で足りる。
- 既存の `keiland_network_*` の振る舞いは変えない。
- link の速さと DHCP・static の別は kernel の `if_data` に無いので含めない。
- `KEILAND_VERSION` を 1 つ上げる。`keiland.h` は WS081・WS035 と衝突しうるので、適用の前に main と順序を合わせる。
