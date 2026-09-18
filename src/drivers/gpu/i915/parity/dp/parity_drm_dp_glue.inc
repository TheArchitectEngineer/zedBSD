/*
 * WS031 Linux-parity — zedBSD glue at the end of drm_dp_helper_port.c.
 *
 * drm_dp_aux_init(): the hardware mutex and the DDC adapter bound to the
 * I2C-over-AUX algorithm.  (The i2c core's own -EAGAIN retry loop is not
 * reproduced: drm_dp_i2c_xfer never returns -EAGAIN.)
 */
void parity_drm_dp_aux_init(struct drm_dp_aux *aux)
{
	mutex_init(&aux->hw_mutex);
	aux->ddc.algo = &drm_dp_i2c_algo;
	aux->ddc.algo_data = aux;
	aux->ddc.retries = 3;
}
