<!-- awesome-plan project=zedbsd record=ws049p009-findings -->

# ws049-p009: 規約の全文のレビューの指摘（2026-10-04）

5 つのレビュー（P1 generation15 が起動した読みのみのサブエージェント、`plan/coding-style.md` の全文と §14 の checklist）の指摘の全件。
行番号はレビューの時点（commit a41776a ＋ 未 commit の acpi-event.c の style-check 3 件の修正）のもの。適用するときは内容で探し直す。
**[実害]** は実害の候補（2026-10-04 user「承認、全部適用」: 規約の全件と実害の候補の全部を P1 自身が適用。孫のサブエージェントは使わない）。

適用しないと決めたもの（tree の慣行、limitation として phase.md に記録する）: `ULL`・`unsigned long long`・`%llx`・`long long`・`%zu`、C99 の designated initializer（acpi-dev.c）、static 関数の複数行の header comment（P8）。
自動の検査（`plan/tools/style-check.py`）は WS049 の全 source で acpi-event.c の 3 件だけ → 修正済み（commit 済み）。

## 共通の型（file ごとの行は各節）

- **bare pass-through return**（§11）: `error = call(...); return error;` → `if (error != 0) return error;` の後に `/* Succeeded ... */ return 0;`（switch なら `break` して後で 1 回検査）。
- **成功の return が最後でない／失敗の return で終わる**（§11）: switch の case の中で成功して return し、最後が `return EINVAL;` → case は `error = ...; break;`、`default: error = EINVAL;`、後で検査、最後に Succeeded。
- **段落**（§5）: guard の直後の新しい処理に空行とコメントが無い、1 つのコメントが複数の操作を覆う、loop の本体の最初の段落にコメントが無い、`if` にコメントが無い。
- **allocation と初期化が同じ block**（§9）: allocation＋検査の後に空行とコメント。
- **連鎖の検査**（§9）: `error = f(); if (error == 0) error = g(); if (error != 0) ...` → 1 つずつ検査。
- **復唱のコメント**（§10）: "Reads it." "Frees it." "Reports the value." など → 意味を書く。
- **Succeeded の後に呼び手が得るものを書く**（§11）。
- **非標準の for を 1 行に**（§7）: 3 つの制御式を別の行に。
- **public 関数のコメントの最初の文が複数行**（§3、P7）: 1 行の動詞＋目的語にする。
- **protocol の flag・counter の意味のコメント**（§10）。
- **critical section の空行**（§5）。

## aml-operator.c（g1）

- 263・2446: 名前 `result` → `assembled`・`accumulated`（§4）。708・758: `bool value` → `truth`。
- 270-298・366-387・1785-1801: 失敗の return で終わる（§11）→ 単一の成功 return。
- pass-through return: 275-280、372-373、375-380、1389-1390、1791-1792、1794-1795、2073-2074、2076-2077、2090-2091、2113-2114、2116-2117。
- 287-289: loop のコメント（`/* Assembles the integer from its bytes, lowest first. */`）。330・410・530・1486・2041-2044・2489: guard の後の段落にコメント。
- 501-504: 剰余の格納を別の段落に。1240-1242・1358: allocation の段落から初期化を分ける。1344: `*result = NULL;` を独立の段落に。
- 221-227、1242、1248、1259、1475-1479、2033-2034、2163: 途中の成功 return（§11）。
- 1322: `logical(reference != NULL)` → Boolean を if で作る（§6）。
- 1495-1496: "Succeeded." の下に release → 分ける。1547・1600-1601: if のコメント。1543・1550・1558・1566: 名詞句のコメント → 動詞＋目的語。
- 1647-1653・1944-1950: 連鎖の検査（§9）。1833-1845: 段落を 3 つに。1884-1888・1959-1963: allocation が Succeeded の下（§9/11）。
- 2240-2242: **[実害]** `match_one()` が比較の error（ENOMEM を含む）を「一致しない」に変える → 挙動は保ち、コメントで明示（ACPICA との違いを検討）。
- 2322: loop のコメント。2419: **[実害]** 常に真の `if (form != STRING_DECIMAL)` → 消す。2457: 3 節を 1 行（§6）。2472: 非標準 for。
- 457・1922・2529: 曖昧・復唱のコメント。"Succeeded." に得るものを: 346、743、793、949、992、1031、1146、1272、1327、1360、1424、1456、1530、1767、1927、2046、2333、2382。
- 参考（適用しない）: 450（Mod）・546（ToBCD）・1719（ToHexString）の `default:` が最後の opcode を兼ねる。

## aml-object.c（g1）

- 66-70: 変数に入れて return（§11）。89-90・331-332: 途中の成功 return。122-125: 0 で「誰も持たない」counter のコメント（§10）。
- 143・158・256: 復唱。412-414: pass-through。415-418: default の呼び出しにコメント。488: unbraced loop の後の free に空行とコメント。
- 参考（適用しない）: 250 `drv_acpi_object_reference_node()` の const の cast。

## aml-sync.c（g1）

- 42-47: file-scope 変数の保護を書く。104-105・137・163・165・305・362-367・378・381・388・391・401・408: 段落・コメント。
- 281-283: **[実害]** `drv_acpi_thread_end()` が `drv_acpi_mutex_release()` の失敗を無視し、held の list が進まず無限 loop になりうる → 失敗を log し、強制で list から外す。
- 309-310: pass-through。441・456: `drv_acpi_ns_resolve_alias(node)->object`（§8）→ 中間を変数に。511: コメントの typo `\\_GL_`。527: 復唱。
- 541・569・594: Boolean を式から return（§6）。411: Succeeded に得るもの。
- 参考（適用しない）: 393（Reset）の default。

## aml-osi.c（g1）

- 80-81・112・129-130: 段落。132: Succeeded に得るもの。83: 曖昧なコメント。

## aml-thread.c（g1）

- 97・141: 復唱・曖昧。121-124: 最後の return の段落。68: Succeeded に得るもの。

## aml-define.c（g2）

- 1377-1427: public の `drv_acpi_package_resolve`・`drv_acpi_reference_resolve` が static の後（§2/3）→ `drv_acpi_create_buffer_field`（416）の後へ。
- 846-849: 失敗と成功（重複）を同じ stop point で返す → 分ける。388: 3 節を 1 行。1106-1110・1118-1124: 連鎖の検査。
- 368: "Ends the CreateField case." を消す。234: 重複のコメント。1440: for の初期化で関数呼び出し（§7）。885・947: `evaluated = 1` の flag の意味。
- 1386-1388: loop の if のコメント。1342-1344: Succeeded。486: return のコメント。1000・405: if の連鎖の段落。602-607: 段落 2 つ。1356-1362: allocation を分ける。
- allocation と初期化: 493、696、760、832、944、1176、1351。984・1043-1044: 準備の段落。
- 1243、1255、1282、1290（と aml-eval.c:1595）: `converted` は errno → `text_error`。1327-1328: pass-through。103: 曖昧なコメント。
- 1348-1350: **[実害]** package の要素の名前が 128 byte を越えると `drv_acpi_ns_name_text` が ENOSPC で Package 全体が失敗 → 他の呼び手と同じ "(long name)" に fallback。
- 1137-1141: **[実害]** named field の 4 byte の segment の名前の文字を検査しない → stream の読み手と同じ検査（EIO、field_list の短い segment と同じ）。

## aml-eval.c（g2）

- 507-508・1357-1358: `drv_acpi_store`・`store_reference` が `return EINVAL;` で終わる → switch を break にし最後に成功。
- 91-100: 段落 3 つ。828・831・478・481・835（と aml-skip.c:150）: control の flag・case の return のコメント。
- 581-585: **[実害]** `serialize(&eval, node->object, false)` の release の失敗を捨てる → log し、method が成功していれば error を返す（`eval.return_value` は release）。
- 560-564・1574: allocation と初期化。534・537・733: 段落。486-489・1346-1349: 準備の前にコメント。569: loop の if。
- in-block の成功: 795-796、1183-1184、1274-1276、1382-1384、1466-1468。421-423・431-433: guard の後の成功。
- 959-960・1286・1304・1168・1185-1186・1422-1429・1537（と 1534）・768: 段落・コメント。
- pass-through（27）: 217-218、238-239、241-242、244-245、247-248、255-256、483-484、490-491、495-496、498-499、501-502、545-546、629-630、632-633、691-692、808-809、815-816、819-820、822-823、825-826、1298-1299、1309-1310、1312-1313、1340-1341、1343-1344、1351-1352、1564-1565。
- 29・1502: `ULL`（適用しない）。

## aml-skip.c（g2）

- 286・314: 段落。144-165: case のコメントと pass-through → break にして最後に成功。362-363: lookup の NULL（弱い、参考）。
- pass-through（7）: 133-134、152-153、155-156、158-159、161-162、164-165、258-259。

## aml-stream.c（g2）

- 291・192・116: 段落。247-259: case のコメント、`read_segments` の段落、pass-through → `count` を決めて 1 回だけ呼ぶ。57-59: Succeeded。348・368: 曖昧なコメント。
- pass-through: 249-250、258-259。

## aml-field.c（g3）

- 共通の型の行: P1（成功のコメントと return の間の公開）423-425、716-722、964-966、1189-1191。P2（for）769、892、1001、境界 476、504、558。
  P6（allocation）303-306、1303-1306。P7 146-148、188-190、200-202、363-365、392-394。
- 50-54: `PCI_HEADER_*` の macro のコメント（→ g3 が適用済み、commit 済み）。
- 124・163: `drv_acpi_enter` を独立の段落に。133-135: `regions_connected` の判断の段落。233-235・278-280: 2 つの if。242: 空行 2 つ。
- 352-357: `object_to_bits` の結果をすぐ検査。379・381: 復唱。415-418: 2 つの評価を一緒に検査。477-478・503-504・510: 段落・復唱。
- 572-576（と 322、1229、1307、1426）: `bits_to_object` の検査が free の後（弱い）。621-625: data table の分岐の loop と成功 return → helper へ。
- 642-651: memset の段落。692・742-744・751・813-814・833・841・902-903・909・917-918・958・1039・1099・1149・1178・1213-1215・1225-1227・1248・1274-1275・1354: 段落・コメント。
- 1361-1362: **[実害]** `connect_visitor` が `run_reg()` の失敗（ENOMEM）を捨てる → log して walk を続ける。
- 1385-1387: 2 つの allocation を一緒に検査 → 1 つずつ。

## aml-table.c（g3）

- P1: 371-373、637-639、681-684、715-717、1006-1009。P2: 435、500、950、974。P6: 315-318、672-675。P7: 159-162、182-185、224-226。
- 57: `tables_last` に自分のコメント。68: `struct load_table_request` が変数の後（§2）→ 前へ。65-67: 型のコメントを役割に。
- 220-221・234-235・256・402・501・571・629・691・909-916・925・988: 段落・コメント。665・698: 長さの decode を 1 行 1 byte か helper に。
- 681-684: 成功の exit が 2 つ → helper に分ける。739-746・841-845: 連鎖の検査。810-815・933-937: return のコメントと成功の位置。915: 入れ子の呼び出し（§8）。
- 818: **[実害]** `table_loaded()` が `table_check` の前に `data + 10`・`data + 16` を読む → 長さを先に確かめる。

## aml-namespace.c（g3）

- P1: 130-132、220-222、388-390、445-447、488-490、703-705。P2: 156、188、362、513、760、788。P6: 255-259、470-476。P7: 71-73、96-98。
- 202・236・313・359・512-514・574: 段落・コメント。352-355・358-373: 成功の exit が 3 つ → `ns_search_upward()` に。358: 4 節を 1 行（§6）。
- 433-436: **[実害]** `drv_acpi_ns_create()` が ENOMEM のとき未初期化の `node` を `*result` に写す → NULL で初期化するか成功・EEXIST のときだけ。
- 262-264・271-273: **[実害]** root の object の確保か予約名の作成に失敗すると `namespace_root` が半端なまま残り、次の初期化が 0 を返す → 失敗で元に戻す。

## aml-internal.h（g3）

- 26-28: `UNUSED_PARAMETER` の fallback にコメント。199-213・501: macro・enum の順（header への §2 の適用、機械的なら移す）。
- 347-382: `value` の union の無名の struct（string・buffer・package・event・processor・power・ddb・alias）に役割のコメント。149: `ULL`（適用しない）。

## aml-os.h（g3）

- 指摘なし。

## acpi-kern.c（g4）

- 96: `firmware` の保護。170-176: 3 つの install の連鎖の検査。185-193: 段落 4 つ。195-203: コメントと内容の食い違い（Global Lock）、2 つ目の `if (error == 0)`。
- 229・247・267・354・366・659・277・334: 復唱。230-233: 変数に入れて return。292: 入れ子の呼び出し。403-404・435-436: 成功 return の位置。
- 578: 5 節を 1 行。611-615・635-643: critical section の空行と `event_work++` の意味。683・763・843・686・774・694-699: 段落。
- 725-728: pass-through。793・801・809・817・893・901・909・917: case の中の if のコメント。1002-1003: 分割した呼び出しの 1 行 1 引数。696・864: `hal_space_unmap_device` の結果。
- **[実害]** io_handler: 64 bit の access が port 0xfffc-0xffff のとき 2 つ目の dword が `(uint16_t)(port + 4)` で低い port へ wrap → 最後の byte が 0xffff を越える access を EFAULT。

## acpi-event.c（g4）

- 257・285・332-333・378・406・410・442・455-461・476・482-484・643-645・516-524・532・594-604・612-616・620-621・755-758・773・787-789・1049-1053・1117-1121・661・828: 段落・flag・critical section・復唱。
- 377-381・405-412・503-508・550-554: critical section の空行。418-423・487-490: public 関数のコメントを動詞＋目的語に。475・1017: Boolean を式から（§6）。642-645: 失敗で終わる理由のコメント。1076-1077: 分類の関数（弱い）。
- **[実害]** GPE1 があり GPE1_BASE が GPE0 の最後より上のとき、その隙間の番号が GPE0 の block の外の port（`gpe0.port + gpe/8`）になる → 隙間を無い GPE として初期化・SCI・enable/disable・method の visitor で飛ばす。
- **[実害]** `enable_acpi_mode()` が PM1a の control block の無い FADT で port 0 を読む → `drv_acpi_events_init` で `pm1a_control.port == 0` を ENODEV。

## acpi-ec.c（g4）

- 136・144: 4 節を 1 行。175・281: **[実害]（死んだ flag）** `ec.attached` は誰も読まない（tree を grep 済み）→ 消す。237・258・310-312・346・428・674・383・497・543-548・582-583・653-659: 段落・return・Boolean の混在・結果の無視。

## acpi-tables.c（g4）

- 189-219: 成功が loop の中。271・284-287・426-437（`wide` を 2 つの意味に使う）・440-441・532・557-559・106・494・499・575・590・150・190: 段落・名前・コメント。

## acpi-text.c（g4）

- 231・253・263・297-298・302-303・307-308: 分割した呼び出しの 1 行 1 引数。336: `switch (drv_acpi_object_type(object))`（呼び出しを条件に）。338・341・359・380: 入れ子の呼び出し。347・447・452・145: loop・if・コメント。

## acpi-dev.c（g4）

- 61-66: designated initializer（適用しない）。97・99・146-168・161-165・192・198-204: 段落・critical section・コメントの食い違い・混在の条件。
- 147-152: **[実害]** namespace の text の作成が途中で失敗すると部分の text が残り、次の read が後ろに足す → 失敗で text を捨てて作り直す（`drv_acpi_text_release` → `drv_acpi_text_init`）。

## acpi-tables.h・acpi-text.h

- 指摘なし。

## include/drivers/acpi/acpi.h（g4）

- 85-92: `struct drv_acpi_region_access` のコメントが "An address space handler." → access の説明に。102: `drv_acpi_region_handler_t` にコメント。

## plan/ws049/tests/aml-host-hardware.c（g5）

- 75-77・83-89・102-104: file-scope 変数のコメントに不変条件。134・140・242・325・342・411・413: if のコメント。137・167・192・216: 復唱。139: `access_byte` の結果の無視。
- 144・362・398・441・298-299: Succeeded の形。181: `ec_queries[ec_query_count++]` を 2 行に。273-280: 段落。317・334: Boolean を式から。322・340: 3 節・混在。
- 346・348: 呼び出しの結果を直接 return。373-377・409-416: 成功 return が最後でない。381-392・420-436・428-431: case のコメント。
- 127-128: **[実害]** `hardware_port()` が modelled かを `access_byte()` の実際の読みで調べ、EC の data port の probe が OBF を消す → 副作用の無い判定（`port_modelled()`）。

## plan/ws049/tests/aml-host.c（g5）

- 192・227-230・248-254: file-scope 変数のコメント。329・364・332-333・368・395・409-415・417-418・1119-1120: 段落・結果の無視・return。
- 387・1085・1113・1669・434・448・466・543・574・617・633・734・1100・1287: 復唱・flag。437-438・518-519・646: return。515・1199・1201: `clock_gettime`・`fseek` の結果。
- 547・578・904・1024・1034-1035・1037-1038・1296-1301・1348・1381・1535・1595-1598・1694-1698・1738-1743・1760-1765・1785-1787: 段落・if・入れ子の呼び出し。
- 690-697・1351-1357: 成功 return が loop の中。1394-1395: pass-through。1585: 非標準の for。1600・1642・1720: Succeeded の形。1639: 結果の無視。1646-1654: `struct method_list` が関数の間（§2）。
- 788-790・830-833・953・1020: **[実害]** `action_count` に上限が無く、65 個以上で配列の外を読む → `OPTION_LIST_MAX` で拒む。
- 1665・1708-1710: **[実害]** `evaluate_methods()` が walk の失敗（realloc の ENOMEM）を無視して部分の一覧を評価し 0 を返す → 失敗を返す。
- `long long`・`%llx`・`%zu`（適用しない）。

## plan/ws049/tests/aml-host-hardware.h

- 指摘なし。

## WS049 の kernel の他の file の部分（g5）

- `src/hal/amd64/bsp-pcat/acpi.c` 570・573-576: EBDA の後の段落と fallback の if のコメント。578-579: NULL を含む `return result;` → 分ける。48-55・125-126・263-279: 指摘なし。
- `src/hal/amd64/bsp-pcat/boot.c` 417-419: `rsdp = amd64_acpi_rsdp_handoff(); return rsdp;` → NULL と成功を分ける。
- `src/kern/platform/pcat.c` 231-232: `(void)drv_acpi_attach();` → error を kern_logf（ENODEV は drv_acpi_attach が log 済みなので除く）。229-230: `#if` の中の空行（軽微）。
