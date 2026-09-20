# E-119 freeze (2026-09-19)

E-119 final source = base commit 2bf790a4 + e97-e119-changes.patch (applied with git apply; verified: the result equals the E-119 final src/drivers/gpu/i915 tree, before any E-120 change).

| item | sha256 |
|---|---|
| e97-e119-changes.patch | 7fbe991ccea6e06f7c944fc1b930ba1c3e2747ea036f54e1dc13a6eb582e5b07 |
| e119-review-changes.patch | e89d56397730ad75d727eff686843df32a9b03ab08cf02a4ab81c1176a757f09 |

Final sweep (sweep_e119f.sh, 13/13 PASS) build outputs:

| file | sha256 |
|---|---|
| build/eu-e119f/vmunix | d5165b2574be63e99b4caead8d73ea69b948d4130755bd96da0a6327236e1207 |
| build/eu-e119f/hdd-image | 995c39c9e786aaee827d56048a4e7ae0c2d555ba2030cd692a237660cf8e5091 |
| build/draw-e119f/vmunix | 1c4fdcc6e685cb5b9607f4d2ef04a6cac763422f600a50522b9205ff8bab8a2f |
| build/draw-e119f/hdd-ima | 94c2df0f6e38d7831e18462d6a70cb8b1fc6a40a7ed5252e42272ca13159a926 |
| build/r1-e119f/vmunix | e72510ddc5bb01031ee992326cd401605e18ff3b413fd92cc2529db7e0105c59 |
| build/r1-e119f/hdd-image | 009cd63fd0a8dce0fc8ff81c053d33595406d511b7f9fcdfb8a6bf260cfa80ee |
| build/tex-e119f/vmunix | 50f1cf87ee28da41b36bbbca176764ffffd47bd77ab50d6dfccd7f70f5b79e56 |
| build/tex-e119f/hdd-imag | eca8cc8a3c744f3865235d0b16835a66610394cb4e99f06450ced1320fc4202f |
| build/t3-e119f/vmunix | 16bc0a7666226b88f90d85ac1c0798b8d4b3ff42598ff8ae54cfdcbc19ca1a86 |
| build/t3-e119f/hdd-image | cf0ab08dab9838123482a6dbfda2f6e3318965f15060ea8f146f0d3afcfa563f |
| build/bl-e119f/vmunix | 9a4bf204aa9b977ab3393469a8104ef34b8a4a2cf1ce32f8e63a405b8566f2c6 |
| build/bl-e119f/hdd-image | 2db5e284bd370ed88f3e526ed9091c2bda631c9f45e615a34e62cc120da65789 |
| build/texvbt-e119f/vmuni | 20d1856d8dde3bc87775434187d1f93d27e4b74cfb945189071f3a0383030a1f |
| build/texvbt-e119f/hdd-i | b1d6aa590aff4b61fd658bb3427652df43958f76eaa630738348da01da193e4a |
| build/aux-e119f/vmunix | a00d071f3419defd337dce966b99e455e5c97840a263d4d2c9de35b4b0d43b96 |
| build/aux-e119f/hdd-imag | 4e14e76b39ae9def5e078a6c52d514364213ff3d881383cb96a77b93b0d33add |
| build/lcdb-e119f/vmunix | 61af78b10e4d15a0bb184a6dbc86df79a20af1d65a160d7602ac8a78aede46e8 |
| build/lcdb-e119f/hdd-ima | 04270b2de840fa52df160f62af660e38b0e8a2c1119339e7458e11345d687c84 |
| build/lcdr-e119f/vmunix | cfab48eeca74f8f5b9ecfe2fc0d690ce149b991b1cebd4f9d49c1c83bbba3b4f |
| build/lcdr-e119f/hdd-ima | 6b49dd932685480e5f507385260bc74ff7c69c23042e61478831d6228f0845cc |
| build/lcdg-e119f/vmunix | b57208064a5f3d38937aaded101a37202e96f2329cd67ff5586db43b6dee5553 |
| build/lcdg-e119f/hdd-ima | 8c9c05fc92fe1c3f073fbc10039c64e533b895255fdec502b7cb2aeca9b8a3d9 |
| build/lcdc-e119f/vmunix | fc96d1b9d8935dcaebe2bf56b823d18860ba5bee716e276954cbbbf1bf019147 |
| build/lcdc-e119f/hdd-ima | 8d48939d6f6f369d57d8dd271a81d6218fb1a60d8f341a5f3f11aebf9674bbfe |
| build/lcdd-e119f/vmunix | 05cf0cfe12d569450f10577161f327e8e278525810e944fe83d957b209164643 |
| build/lcdd-e119f/hdd-ima | 50a47abec61c6489c7a68cd26ac18d4f224511e5fd768b3b026a6ea83e5751c2 |

Run conditions: RUNSCRIPT run-parity-ref.sh (8 GPU modes, AUX) / run-parity-ref-240.sh (LCD-R/G/C/D); OVMF + VFIO, explicit VBT, 10 ms tick, execlists; per-mode flags as in sweep_e119f.sh. Logs: e119f-run-parity-hw-*.log.
