# ws216-p007: the test image of Notes' drawing tools (tests/scenarios/apps/notes/draw-tools.md and fill-region.md):
# the AAT image (plan/tools/aat/config-amd64-aat.mk: the UAT configuration, the input injector, aat-input and the
# screen's capture) with the test pen, peninject, so that the strokes carry a pressure.  A test image only.  No file is
# copied in: the helper (plan/tools/aat/scenarios/helpers_notes_edit.py) makes edit-basic.pdf on the host
# (plan/ws175/tests/make-edit-samples.py) and puts it on the target at run time.
#   AAT_CONFIG=plan/ws216/tests/config.mk plan/tools/aat/build-image.sh BUILD
#   plan/tools/aat/run-aat.sh qemu OUTDIR apps.notes.draw-tools apps.notes.fill-region
include plan/tools/aat/config-amd64-aat.mk
ZEDBSD_USER_PROGRAMS += peninject
