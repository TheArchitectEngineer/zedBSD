# ws211-p003: the AAT image (UAT config + test injection + screen capture) for the draw-batching comparison
# (plan/ws211/tests/batch-qemu.sh): the same compositor with and without KWL_BATCH=0, counted by `KWL DRAWS`.
#   plan/tools/aat/build-image.sh BUILD --file /etc/keiland/session=plan/ws211/tests/session-batch.sh
include plan/tools/aat/config-amd64-aat.mk
