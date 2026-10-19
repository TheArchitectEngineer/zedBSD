# The VisionFive 2 test kernel of ws256-p023: the CI board configuration
# plus /dev/vf2-dmatest, the DMA coherency check on the AXI DMA controller.
# Not for the ordinary image.
include config/ci/config-visionfive2.mk

CONFIG_VF2_DMA_TEST := y
ZEDBSD_TEST_CPPFLAGS += -DCONFIG_VF2_DMA_TEST
