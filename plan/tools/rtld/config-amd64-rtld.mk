# ws140: the SSH guest image for the dynamic loader's tests (rtld-many.sh, and run-tls-check.sh of ws073-p038), the
# guest harness's image with nothing added: the tests copy their programs and libraries in over SSH.  Build (the
# result is BUILD/hdd-image.img):
#   plan/tools/guest/test-image.sh plan/tools/rtld/config-amd64-rtld.mk BUILD
include plan/tools/guest/config-amd64-ssh.mk
