/*
 * WS031 Linux-parity — read-only firmware provider (request_firmware /
 * release_firmware equivalent).  Serves fixed reference blobs embedded in the
 * boot image by name; it never allocates or frees the blob (static, read-only),
 * so release only drops the handle.  No HAL API, no VFS.
 */
#ifndef PARITY_OSDEP_FIRMWARE_H
#define PARITY_OSDEP_FIRMWARE_H

#include <stdint.h>

struct osdep_firmware {
	const uint8_t *data;   /* NULL until a successful request */
	unsigned size;         /* uncompressed byte length */
};

/* request_firmware(): 0 with data/size set, or -ENOENT (data=NULL) if absent. */
int osdep_request_firmware(struct osdep_firmware *fw, const char *name);

/* release_firmware(): drop the handle; the embedded blob is NOT freed. */
void osdep_release_firmware(struct osdep_firmware *fw);

/*
 * Test-only override: when set, requests are routed to `request` (which may
 * hand back a mutable test copy or report absence).  Production leaves it NULL.
 */
struct osdep_firmware_test_ops {
	int (*request)(void *ctx, struct osdep_firmware *fw, const char *name);
	void *ctx;
};
void osdep_firmware_test_set(const struct osdep_firmware_test_ops *ops);

#endif /* PARITY_OSDEP_FIRMWARE_H */
