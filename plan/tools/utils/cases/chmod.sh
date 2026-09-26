#### chmod octal
touch f; chmod 640 f; ls -l f | cut -c1-10; chmod 7777 f; ls -l f | cut -c1-10; chmod 0 f; ls -l f | cut -c1-10

#### chmod octal clears set-ID on a file
umask 022; touch f; chmod 2755 f; chmod 755 f; ls -l f | cut -c1-10

#### chmod octal keeps set-ID on a directory
umask 022; mkdir d; chmod 2755 d; chmod 755 d; ls -ld d | cut -c1-10; chmod 0755 d; ls -ld d | cut -c1-10; chmod 00755 d; ls -ld d | cut -c1-10

#### chmod = keeps set-ID on a directory
umask 022; mkdir d; chmod 2755 d; chmod a=rx d; ls -ld d | cut -c1-10; chmod 2755 d; chmod g-s d; ls -ld d | cut -c1-10

#### chmod +x respects the umask
touch f; umask 022; chmod 644 f; chmod +x f; ls -l f | cut -c1-10; umask 077; chmod 644 f; chmod +x f; ls -l f | cut -c1-10

#### chmod = without who respects the umask
touch f; umask 077; chmod 644 f; chmod =rx f; ls -l f | cut -c1-10

#### chmod permission copy
touch f; umask 022; chmod 640 f; chmod u=g f; ls -l f | cut -c1-10; chmod 640 f; chmod o=u-w f; ls -l f | cut -c1-10; chmod 640 f; chmod g+u f; ls -l f | cut -c1-10; chmod 640 f; chmod +u f; ls -l f | cut -c1-10

#### chmod several actions and clauses
umask 022; touch f; chmod 600 f; chmod u+r-w+x,g=u f; ls -l f | cut -c1-10; chmod 755 f; chmod a-rwx,u+rw f; ls -l f | cut -c1-10; chmod 755 f; chmod g=o f; ls -l f | cut -c1-10

#### chmod X
umask 022; touch f; chmod 644 f; chmod a+X f; ls -l f | cut -c1-10; chmod 744 f; chmod a+X f; ls -l f | cut -c1-10; mkdir d; chmod 600 d; chmod a+X d; ls -ld d | cut -c1-10

#### chmod s and t
umask 022; touch f; chmod 755 f; chmod +t f; ls -l f | cut -c1-10; chmod 755 f; chmod u+t f; ls -l f | cut -c1-10; chmod 755 f; chmod o+s f; ls -l f | cut -c1-10; chmod 755 f; chmod ug+s f; ls -l f | cut -c1-10; chmod 755 f; chmod +s f; ls -l f | cut -c1-10

#### chmod empty permission lists
umask 022; touch f; chmod 755 f; chmod = f; ls -l f | cut -c1-10; chmod 755 f; chmod u= f; ls -l f | cut -c1-10; chmod 755 f; chmod +rw- f; ls -l f | cut -c1-10

#### chmod -w is a mode
touch f; umask 022; chmod 755 f; chmod -w f; echo "st=$?"; ls -l f | cut -c1-10; chmod 755 f; chmod -- -w f; ls -l f | cut -c1-10

#### chmod invalid modes
umask 022; touch f; chmod 755 f; for m in 'u+x,,' 'ug' '8' '17777' 'u+x,' 'z+x' 'u+q' ''; do chmod "$m" f 2>/dev/null; echo "$m st=$?"; done; ls -l f | cut -c1-10

#### chmod several files and a missing one
umask 022; touch f g; chmod 600 f nosuch g 2>/dev/null; echo "st=$?"; ls -l f g | cut -c1-10

#### chmod -R
umask 022; mkdir -p d/e; touch d/f d/e/g; chmod -R go-rwx d; find d | sort | while read p; do echo "$(stat -c %a "$p") $p"; done

#### chmod -R X on a tree
umask 022; mkdir -p d/e; touch d/f d/e/g; chmod d/f 755; chmod -R go-rwx d; chmod -R g+rX d; find d | sort | while read p; do echo "$(stat -c %a "$p") $p"; done

#### chmod -R skips symlinks inside
umask 022; mkdir d; touch t; chmod 644 t; ln -s ../t d/l; chmod -R 600 d; ls -l t | cut -c1-10

#### chmod follows an operand symlink
umask 022; touch t; chmod 644 t; ln -s t l; chmod 600 l; ls -l t | cut -c1-10

#### chmod missing operand
umask 022; chmod 755 2>/dev/null; echo "st=$?"
