import os
os.chdir('/tmp/claude-1000/-home-awe-zedBSD-rpi4/3cdd62d7-28a8-5eca-949e-9a1decb1273f/scratchpad/halprop/tree')
p = 'src/drivers/platform/rpi4/rpi4-pcie.c'
s = open(p).read()
a = """/*
 * Starts the PCI core and the BCM2711 root complex.
 */"""
b = """/* Scratch-only probe of the uncached DMA view (not part of the proposal). */
volatile uint64_t ws048_probe[6] __attribute__((used));

static void
ws048_uncached_probe(void)
{
	struct drv_dma_constraints constraints;
	struct drv_dma_device *device;
	struct drv_dma_buffer kept;
	struct drv_dma_buffer freed;
	volatile uint32_t *words;

	kern_memset(&constraints, 0, sizeof(constraints));
	constraints.address_bits = 31U;
	constraints.max_segment_size = 16U * 1024U * 1024U;
	constraints.coherent = 0;
	ws048_probe[0] = 0x5753303438000000ULL;
	if (drv_dma_device_create(&constraints, &device) != 0)
		return;
	ws048_probe[0] |= 1U;
	if (drv_dma_alloc_coherent(device, 8192U, 64U, &kept) != 0)
		return;
	ws048_probe[0] |= 2U;
	words = kept.address;
	words[0] = 0x57533034U;
	words[1] = 0x38000001U;
	words[1024] = 0x38000002U;
	ws048_probe[1] = (uint64_t)(uintptr_t)kept.address;
	ws048_probe[2] = kept.device_address;
	if (drv_dma_alloc_coherent(device, 4096U, 64U, &freed) != 0)
		return;
	ws048_probe[3] = (uint64_t)(uintptr_t)freed.address;
	ws048_probe[4] = freed.device_address;
	drv_dma_free_coherent(device, &freed);
	if (freed.address == NULL)
		ws048_probe[0] |= 4U;
	ws048_probe[5] = 0x444f4e45ULL;
}

/*
 * Starts the PCI core and the BCM2711 root complex.
 */"""
assert a in s
s = s.replace(a, b, 1)
a = """	/* Finds the device tree in the direct map. */"""
b = """	ws048_uncached_probe();

	/* Finds the device tree in the direct map. */"""
assert a in s
s = s.replace(a, b, 1)
s = s.replace('#include <drivers/generic/fdt.h>\n', '#include <drivers/generic/fdt.h>\n#include <drivers/generic/dma.h>\n', 1)
open(p, 'w').write(s)
