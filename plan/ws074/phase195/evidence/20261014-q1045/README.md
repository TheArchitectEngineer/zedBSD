# q1045: XHTML timeout root cause

Fixed input: WPT `2d66b9b7998bb58c336138c178323ddee857b586`, `css/css-flexbox/flexbox-justify-content-horiz-003.xhtml` and `004.xhtml`, 800×600, fixed Mahora/JetBrainsMono/DroidSansFallbackFull fonts.

Direct `browser --render --async` under `timeout 6s` returned 124 for both test files without an image. The matching `003-ref.xhtml` produced a 1,440,015-byte 800×600 PPM immediately. This isolates the symptom from the Python runner and HTTP server.

`gdb` interruption of 003 reached `inline_cut_text` (`layout/inline.c:257`), called from `layout_inline`, `layout_block`, `flex_lay_sized`, `layout_flex`. `browser --dump=dom` completed and showed the XML declaration as an HTML comment and a deeply nested div structure. The source has 53 self-closing div tags. `page_load_file` and `page_load_location` both call `page_load_html` unconditionally (`page/page.c`); the latter discards HTTP MIME. The child frame path already selects strict XML parsing and DOM projection by Content-Type (`page/frame-load.c:frame_load_parse`).

Conclusion: the timeout is downstream of treating top-level XHTML as HTML. Fix requires MIME-sensitive top-level document loading and XML graph lifecycle. No code was changed in this attempt, so the ten original 5s outcomes remain 10 timeout. The whole p195 goal remains uncleared. Continue in p196 with a bounded implementation and regression scope.
