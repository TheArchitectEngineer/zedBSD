# ws131-p015: the guest image of the application API's test (kuidemo-p015.sh): the Files image
# (plan/tools/files/config-amd64-files.mk) with the widgets' sampler, kuidemo, which is written with kl_app.
#   FILES_CONFIG=plan/ws131/tests/config-amd64-p015.mk plan/tools/files/build-files-image.sh BUILD
include plan/tools/files/config-amd64-files.mk
ZEDBSD_USER_PROGRAMS += kuidemo
