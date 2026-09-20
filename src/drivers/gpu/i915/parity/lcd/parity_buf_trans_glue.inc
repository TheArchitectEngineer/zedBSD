/*
 * WS031 Linux-parity — zedBSD glue at the end of intel_ddi_buf_trans_port.c: what intel_ddi_buf_trans_init()
 * selects for an ADL-P combo PHY (`encoder->get_buf_trans = adlp_get_combo_buf_trans`).
 */
/*
 * intel_ddi_buf_trans_init() picks the table set by platform.  Display version 13 (Alder Lake-P) and
 * version 12 (Tiger Lake) have different combo-PHY translations, so the device decides (E-126).
 */
void parity_lcd_ms_bind_buf_trans(struct intel_encoder *encoder)
{
	encoder->get_buf_trans = parity_lcd_display_ver() >= 13 ?
		adlp_get_combo_buf_trans : tgl_get_combo_buf_trans;
}
