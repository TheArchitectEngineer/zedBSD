# ws073-p026: the zdesktop guest image (plan/ws035/tests/config-amd64-zdesktop.mk) without the clang and
# libcxx packages.  In an agent worktree whose build/llvm-source is a symlink to the shared tree, the clang
# package rebuilds the host LLVM tree and the whole target compiler (BUG-089), and the libcxx package's
# "cp -al build/llvm-source" copies the symlink and would patch the shared source through it.  A libc change
# needs neither; lldb and libc++ are therefore missing from this image.
include plan/ws035/tests/config-amd64-zdesktop.mk
ZEDBSD_USER_PROGRAMS := $(filter-out clang libcxx,$(ZEDBSD_USER_PROGRAMS))
