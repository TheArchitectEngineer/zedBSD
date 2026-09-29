# 案: libkeiland の network の追加（ws089-p003、main の許可済み D4、2026-09-29 に改訂）

所有: libkeiland は WS035。ws089-p003 で足す。**networkd の protocol は変えない**（調べた結果、既存の流れで足りる）。

## 新しい network への鍵の入力（networkd の protocol の変更なし）

`net wifi key SSID PASSPHRASE [auto]`（`userland/base/net/main.c`）と同じ流れを libkeiland から行う:

1. 鍵を自分の credential store に書く: `wifi_store_set_key_for_effective_user(ssid, key, 1)`（`userland/base/net/wifi-store.c`。
   root は `/etc/wifi.conf`、一般の user は passwd の home の `.wifi.conf`。HOME は見ない）。secret は使った後に消す（`explicit_bzero` 相当）。
2. networkd に `NETWORKD_OP_WIFI_PROFILES_CHANGED`（payload 無し）を送る。networkd は Wi-Fi の owner（Wi-Fi を入れた user）の profile を読み直す。
3. `NETWORKD_OP_WIFI_CONNECT`（SSID）を送る（既存の `KEILAND_NETWORK_REQUEST_JOIN`）。networkd は owner の profile の鍵で入る。

制約（networkd の既存の振る舞い）: 鍵は WPA-PSK の 8〜63 文字だけ（`wifi_conf_validate_profile`）。鍵の無い（open の）network は store に
入らないので、今は入れない。Wi-Fi の owner が別の user のとき、JOIN は EPERM（Settings は「Wi-Fi を入れ直す」を促す）。

## keiland.h（`KEILAND_VERSION` を main の最新の次へ: 10 → 11）

```c
#define KEILAND_NETWORK_REQUEST_PROFILES	6U	/* the user's saved networks changed (after keiland_network_save_key) */
#define KEILAND_NETWORK_LINKS_MAX	16U
#define KEILAND_NETWORK_DNS_MAX		4U
#define KEILAND_NETWORK_ADDRESS_MAX	16U	/* dotted IPv4 with its NUL */
#define KEILAND_NETWORK_KEY_MIN		8U
#define KEILAND_NETWORK_KEY_MAX		63U

struct keiland_network_link {
	char name[KEILAND_NETWORK_NAME_MAX];
	unsigned up;		/* IFF_UP */
	unsigned running;	/* IFF_RUNNING: the link is there */
	unsigned loopback;
	char address[KEILAND_NETWORK_ADDRESS_MAX];	/* empty when none */
	char netmask[KEILAND_NETWORK_ADDRESS_MAX];
	unsigned char hardware[6];
	unsigned mtu;
	uint64_t received_bytes;
	uint64_t sent_bytes;
};

size_t keiland_network_get_links(struct keiland_network_link *links, size_t capacity);	/* SIOCGIFCONF and the SIOCGIF* of each */
size_t keiland_network_get_dns(char (*servers)[KEILAND_NETWORK_ADDRESS_MAX], size_t capacity);	/* /etc/resolv.conf */
int keiland_network_save_key(const char *ssid, const char *key);	/* the user's credential store; 0 or an errno value */
size_t keiland_network_get_saved(char (*ssids)[KEILAND_NETWORK_SSID_MAX], size_t capacity);	/* the SSIDs the user has keys for */
```

## 変更の場所

- 新しい file `userland/desktop/libkeiland/network-link.c`（links・DNS・鍵・保存済みの一覧）。
- `userland/desktop/libkeiland/network.c`: `keiland_network_request` に `KEILAND_NETWORK_REQUEST_PROFILES`（1 行の case）。
- `userland/desktop/libkeiland/Makefile`: `network-link.c` と、networkd・net と同じく `userland/base/net/wifi-conf.c`・`wifi-store.c` を source に。
- exports.map: 既存の `keiland_network_*` で足りる（変更なし）。
- 既存の `keiland_network_*` の振る舞いは変えない。link の速さと DHCP・static の別は kernel の `if_data` に無いので含めない。
