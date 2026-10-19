# The VisionFive 2 test kernel of ws269-p008: the CI board configuration
# plus the PowerVR driver's check of the core's compute path (a compute
# kernel's writes, the data master coherency rounds, the temporaries
# field).  Not for the ordinary image.
include config/ci/config-visionfive2.mk

CONFIG_POWERVR_COMPUTE_CHECK := y
