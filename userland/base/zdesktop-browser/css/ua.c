/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * The user agent's style sheet: the default look of HTML elements, written
 * for zdesktop-browser after the rendering section of the HTML Standard
 * (the first pass: display, margins, fonts, lists and links).
 */

#include "css/internal.h"

/*
 * The sheet's text.  Elements that are not rendered are display: none;
 * the block elements, the headings and the lists get their usual margins.
 */
const char css_user_agent_sheet[] =
	"html, address, blockquote, body, center, dialog, div, figure, figcaption, footer, form, header, hr,"
	" legend, listing, main, p, plaintext, pre, search, xmp, article, aside, h1, h2, h3, h4, h5, h6, hgroup,"
	" nav, section, dir, dd, dl, dt, menu, ol, ul, details, summary, fieldset, optgroup { display: block; }\n"
	"area, base, basefont, datalist, head, link, meta, noembed, noframes, param, rp, script, style,"
	" template, title { display: none; }\n"
	"[hidden] { display: none; }\n"
	"li { display: list-item; }\n"
	"table { display: table; }\n"
	"tr { display: table-row; }\n"
	"td, th { display: table-cell; }\n"
	"thead, tbody, tfoot { display: table-row-group; }\n"
	"caption { display: table-caption; text-align: center; }\n"
	"html { color: black; font-family: serif; font-size: medium; line-height: normal; }\n"
	"body { margin: 8px; }\n"
	"p, blockquote, figure, listing, plaintext, pre, xmp, dl { margin-top: 1em; margin-bottom: 1em; }\n"
	"blockquote, figure { margin-left: 40px; margin-right: 40px; }\n"
	"dd { margin-left: 40px; }\n"
	"h1 { font-size: 2em; margin-top: 0.67em; margin-bottom: 0.67em; font-weight: bold; }\n"
	"h2 { font-size: 1.5em; margin-top: 0.83em; margin-bottom: 0.83em; font-weight: bold; }\n"
	"h3 { font-size: 1.17em; margin-top: 1em; margin-bottom: 1em; font-weight: bold; }\n"
	"h4 { font-size: 1em; margin-top: 1.33em; margin-bottom: 1.33em; font-weight: bold; }\n"
	"h5 { font-size: 0.83em; margin-top: 1.67em; margin-bottom: 1.67em; font-weight: bold; }\n"
	"h6 { font-size: 0.67em; margin-top: 2.33em; margin-bottom: 2.33em; font-weight: bold; }\n"
	"ul, ol, menu, dir { margin-top: 1em; margin-bottom: 1em; padding-left: 40px; }\n"
	"ol { list-style-type: decimal; }\n"
	"ul ul, ol ul { list-style-type: circle; }\n"
	"ul ul ul, ol ul ul, ul ol ul, ol ol ul { list-style-type: square; }\n"
	"ul ul, ul ol, ol ol, ol ul { margin-top: 0; margin-bottom: 0; }\n"
	"b, strong, th { font-weight: bold; }\n"
	"i, em, cite, dfn, var, address { font-style: italic; }\n"
	"pre, code, kbd, samp, tt, listing, plaintext, xmp { font-family: monospace; }\n"
	"pre, listing, plaintext, xmp { white-space: pre; }\n"
	"small { font-size: smaller; }\n"
	"big { font-size: larger; }\n"
	"sub, sup { font-size: smaller; }\n"
	"a:link { color: #0000ee; text-decoration: underline; }\n"
	"u, ins { text-decoration: underline; }\n"
	"center { text-align: center; }\n"
	"hr { border: 1px inset gray; margin-top: 0.5em; margin-bottom: 0.5em; }\n"
	"td, th { padding: 1px; }\n"
	"th { text-align: center; }\n"
	"mark { background-color: yellow; color: black; }\n"
	"fieldset { margin-left: 2px; margin-right: 2px; border: 2px groove gray;"
	" padding: 0.35em 0.75em 0.625em; }\n";
