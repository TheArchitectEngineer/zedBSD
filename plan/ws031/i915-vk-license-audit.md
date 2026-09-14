# WS031 Mesa reference audit

取得元: https://gitlab.freedesktop.org/mesa/mesa （src/intel は MIT）。
転記対象ファイルのみ監査。SPDX MIT または MIT permission notice を確認し SHA-256 を記録。

| file | MIT | sha256 |
| --- | --- | --- |
| src/intel/genxml/gen120.xml | MIT(repo,data) | 12fea5fdac709a3587cae64c3bd2b0d187a9f81ccd573160703c69327a521164 |
| src/intel/genxml/gen110.xml | MIT(repo,data) | b5159c099b41ef9afa10578f4215ee542634d2c422d59742b9c5c54af6ac93fa |
| src/intel/compiler/brw/brw_eu_defines.h | yes | 0c5f4e3785faba9742328ffd719d2552c7a1ba1d9722c7a75c915f6682e2d475 |
| src/intel/compiler/brw/brw_inst.h | yes | c6228a21fcb53481f3fa7c1b0fe3a3a88c00b78ffabd663698a79cc27fbd1953 |
| src/intel/compiler/brw/brw_eu.h | yes | eab53974753bc1d3b12bbad7ac9918414789c38309b3db877771265792806e52 |
| src/intel/compiler/brw/brw_eu.c | yes | 278e0e8ad79d6ecd1a604cf36ab968831966a788927cfb43f9a2e977648b5b47 |
| src/intel/isl/isl_format.c | yes | bd4db86433308c18f420b46d312062955bb53b82c43103671f8aaca3c803349d |
| src/intel/isl/isl_surface_state.c | yes | 91cfad233c67d7ea033bb351a5ca424b0c8df796d6c268b14f32f9c776def4e8 |
