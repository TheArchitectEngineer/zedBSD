#### mkfifo makes FIFOs with the umask
umask 022; mkfifo p q; ls -l p q | cut -c1-10; umask 077; mkfifo r; ls -l r | cut -c1-10

#### mkfifo -m
umask 022; mkfifo -m 600 p; ls -l p | cut -c1-10; mkfifo -m g+w q; ls -l q | cut -c1-10; mkfifo -m a=r r; ls -l r | cut -c1-10

#### mkfifo -m ignores the umask
umask 077; mkfifo -m 644 p; ls -l p | cut -c1-10

#### mkfifo existing fails but continues
umask 022; touch p; mkfifo p q 2>/dev/null; echo "st=$?"; ls -l | grep -c '^p'

#### mkfifo -m only permission bits
umask 022; mkfifo -m +t p 2>/dev/null; echo "st=$?"; mkfifo -m 1644 q 2>/dev/null; echo "st=$?"; mkfifo -m +x r; ls -l r | cut -c1-10; mkfifo -m -w s; ls -l s | cut -c1-10; ls | wc -l

#### mkfifo -m invalid
umask 022; mkfifo -m 9 p 2>/dev/null; echo "st=$?"; ls

#### mkfifo no operand
umask 022; mkfifo 2>/dev/null; echo "st=$?"

#### mkfifo passes data
umask 022; mkfifo p; (printf 'through\n' > p &); cat p
