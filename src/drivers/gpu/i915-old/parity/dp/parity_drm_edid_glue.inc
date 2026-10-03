/*
 * WS031 Linux-parity — zedBSD glue at the end of drm_edid_port.c.
 *
 * Reads the base EDID block and its extension blocks over a DDC adapter with
 * the reference's drm_do_probe_ddc_edid(), following the contract of the
 * reference's edid_block_read(): up to four tries per block; a read failure or
 * an all-zero first answer stops at once; a block counts only with a perfect
 * header (base block) and a correct checksum.  Unlike the reference no header
 * repair is attempted: a damaged block is reported, not patched.
 *
 * Returns the number of valid blocks stored (>= 1), or a negative errno:
 * -EIO read failure, -EPROTO invalid block, -ENXIO all-zero answer.
 * *extensions receives the base block's extension count even when `max_blocks`
 * is too small to hold them all.
 */
int parity_drm_edid_read(struct i2c_adapter *ddc, u8 *buf, unsigned max_blocks, unsigned *extensions)
{
	unsigned block, total = 1, try, i;
	int valid;

	if (extensions != 0)
		*extensions = 0;
	if (ddc == 0 || buf == 0 || max_blocks == 0)
		return -EINVAL;

	for (block = 0; block < total && block < max_blocks; block++) {
		u8 *b = buf + block * EDID_LENGTH;

		valid = 0;
		for (try = 0; try < 4 && !valid; try++) {
			int zero = 1;

			memset(b, 0, EDID_LENGTH);
			if (drm_do_probe_ddc_edid(ddc, b, block, EDID_LENGTH))
				return block == 0 ? -EIO : (int)block;
			for (i = 0; i < EDID_LENGTH; i++)
				if (b[i] != 0)
					zero = 0;
			if (try == 0 && zero)
				return block == 0 ? -ENXIO : (int)block;
			valid = edid_block_compute_checksum(b) == edid_block_get_checksum(b) &&
				(block != 0 || drm_edid_header_is_valid(b) == 8);
		}
		if (!valid)
			return block == 0 ? -EPROTO : (int)block;
		if (block == 0) {
			total = 1u + ((const struct edid *)b)->extensions;
			if (extensions != 0)
				*extensions = total - 1u;
		}
	}
	return (int)block;
}
