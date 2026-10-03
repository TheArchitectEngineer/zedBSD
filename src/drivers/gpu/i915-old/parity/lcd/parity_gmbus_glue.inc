/*
 * WS031 E-123 -- zedBSD glue of intel_gmbus_port.c (included at its end).  zedBSD project code.
 * intel_gmbus_setup() for the DDC pins the connectors use (the reference makes all of the platform's valid pins at
 * display init; here a bus is made on first use), the i2c algorithm the adapter carries, and the bit-banging
 * algorithm, which is not ported: a transfer that falls back to it is a recorded step answering -EIO.
 */

static struct intel_gmbus hpd_gmbus_bus[GMBUS_NUM_PINS];
static int hpd_gmbus_mutex_inited;

/*
 * i2c_adapter_lock_bus / unlock_bus of the reference (gmbus_lock_bus / gmbus_unlock_bus): the single GMBUS
 * controller is shared by every pin, so a transfer holds display.gmbus.mutex.  Here the same mutex keeps the
 * hotplug worker's detection and a test thread's read apart.
 */
static int hpd_gmbus_xfer_locked(struct i2c_adapter *adapter, struct i2c_msg *msgs, int num)
{
	struct intel_gmbus *bus = to_intel_gmbus(adapter);
	int ret;

	mutex_lock(&bus->i915->display.gmbus.mutex);
	ret = gmbus_xfer(adapter, msgs, num);
	mutex_unlock(&bus->i915->display.gmbus.mutex);
	return ret;
}

static const struct i2c_algorithm hpd_gmbus_algorithm = { .master_xfer = hpd_gmbus_xfer_locked, .functionality = NULL };

static int hpd_bit_xfer_step(struct i2c_adapter *adap, struct i2c_msg *msgs, int num)
{
	kern_logf("i915: parity hpd step i2c_bit_algo.master_xfer (GPIO bit-banging not ported): %s -> -EIO\n", adap->name);
	return -EIO;
}
const struct i2c_algorithm i2c_bit_algo = { .master_xfer = hpd_bit_xfer_step, .functionality = NULL };

struct i2c_adapter *parity_hpd_gmbus_adapter(struct drm_i915_private *i915, unsigned int pin)
{
	static const char *const names[GMBUS_NUM_PINS] = { [1] = "dpa", [2] = "dpb", [3] = "dpc", [9] = "tc1", [10] = "tc2",
		[11] = "tc3", [12] = "tc4", [13] = "tc5", [14] = "tc6" };   /* gmbus_pins_icp */
	struct intel_gmbus *bus;
	const char *n;
	unsigned i;

	if (pin >= GMBUS_NUM_PINS || names[pin] == NULL)
		return NULL;                 /* intel_gmbus_is_valid_pin() */
	bus = &hpd_gmbus_bus[pin];
	if (bus->i915 == NULL) {
		if (!hpd_gmbus_mutex_inited) {
			(void)mutex_init(&i915->display.gmbus.mutex, LOCK_RANK_DEVICE, "i915-gmbus");
			hpd_gmbus_mutex_inited = 1;
		}
		i915->display.gmbus.mmio_base = PCH_DISPLAY_BASE;          /* HAS_PCH_SPLIT */
		memset(bus, 0, sizeof(*bus));
		memcpy(bus->adapter.name, "i915 gmbus ", 11);
		for (n = names[pin], i = 11u; *n != '\0' && i < sizeof(bus->adapter.name) - 1u; n++, i++)
			bus->adapter.name[i] = *n;
		bus->adapter.name[i] = '\0';
		bus->adapter.algo = &hpd_gmbus_algorithm;
		bus->adapter.retries = 1;
		bus->reg0 = pin | GMBUS_RATE_100KHZ;                       /* By default use a conservative clock rate */
		bus->i915 = i915;
		intel_gmbus_reset(i915);
	}
	return &bus->adapter;
}

/* the model tests start a new instance: the buses are made again on first use */
void parity_hpd_gmbus_forget(void)
{
	memset(hpd_gmbus_bus, 0, sizeof(hpd_gmbus_bus));
	hpd_gmbus_mutex_inited = 0;
}
