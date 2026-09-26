# Cases that need a user other than root (root may write a read-only
# file); plan/ws001/tests/guest-run.sh does not run them on the guest.

#### cp -f replaces an unwritable destination
printf 'new\n' > a; printf 'old\n' > b; chmod 444 b; cp -f a b; echo "st=$?"; cat b

#### cp without -f fails on an unwritable destination
printf 'new\n' > a; printf 'old\n' > b; chmod 444 b; cp a b 2>/dev/null; echo "st=$?"; cat b
