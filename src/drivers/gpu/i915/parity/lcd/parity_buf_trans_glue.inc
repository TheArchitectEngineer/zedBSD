/*
 * WS031 Linux-parity — zedBSD glue at the end of intel_ddi_buf_trans_port.c: what intel_ddi_buf_trans_init()
 * selects for an ADL-P combo PHY (`encoder->get_buf_trans = adlp_get_combo_buf_trans`).
 */
void parity_lcd_ms_bind_buf_trans(struct intel_encoder *encoder)
{
	encoder->get_buf_trans = adlp_get_combo_buf_trans;
}
