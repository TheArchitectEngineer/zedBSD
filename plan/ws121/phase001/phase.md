<!-- awesome-plan project=zedbsd record=ws121-p001 -->

# ws121-p001: browser の `<video>` の再生の要件・設計

Status: in-progress（2026-10-05 夜、P2 g16、q775。設計の初版と design-reviewer の 1 回目まで。review の指摘での改訂の前に、Q1 の指示で WS173 へ移った）
Disposition: normal
Parent: [WS121](../ws.md)
Queue: q775（Q1、2026-10-05）
依存: [WS122](../../ws122/ws.md) の p003（mediafile）・p004（libavcodec の dlopen の add-in）、[WS107](../../ws107/ws.md)（libbrowser の分離）

## 範囲と方針（2026-10-05 ユーザー、Q1 の伝達）

- ユーザー（2026-10-05）「WS107, WS121はあなたが作業します。」 WS122 と同じく、**まず dlopen の libavcodec の software decode**。GPU の decode（[WS083](../../ws083/ws.md) の Vulkan Video）は後で同じ境界の後ろに足す。
- Q1 の方向: WS122 の video player の add-in（`userland/desktop/videoplayer` の `codec.c`・`bitstream.c`・`codec-layout.h`、FFmpeg の major の確かめ）と自前の demux（`userland/desktop/mediafile`）を使い回す。mediafile の I/O は pread の helper 1 か所（`mf_read_at`）なので、network の data のための reader の callback の source を足し、HTTP の Range で読む。
- この Phase は設計だけ（code を書かない）。実装は p002 以降。

## 調べた今の形（2026-10-05、p2 の worktree、main 274c5304）

| 部分 | 今の形 | `<video>` に要る物 |
| --- | --- | --- |
| DOM | `DOM_TAG_VIDEO`・`AUDIO`・`SOURCE`・`TRACK` は tag の名前だけ（`dom/dom.h`、`dom/names.c`。source・track は void）。要素の固有の状態は `struct dom_element` の field か pointer（subclass は無い。GC の印は `element_trace`、malloc の物は `element_finalize`） | `dom_element` に media の状態への pointer |
| CSS・layout | UA の規則に video が無く `display: inline` の普通の箱、width・height の属性は無視。replaced は IMG・OBJECT・form control だけ（`layout/box.c`、`layout/replaced.c`）。`object-fit` は未対応 | replaced の箱、UA の規則、固有の寸法（300×150 の既定） |
| 画像 | `page/images.c` が取り、`image/decode.c` が `struct img_bitmap`（0xAARRGGBB、straight alpha、`serial`）に。GIF は最初の frame だけ（timer で描き直す物は今は無い）。`<img>` の load・error の event は無い | poster は同じ道 |
| 描画 | display list（`paint/paint.h`: RECT・TEXT・CLIP・UNCLIP・IMAGE）。CPU は `software.c`（最近傍）、GPU は `vulkan.c` の 2048² の atlas に `serial` を key にして 1 回だけ写す。atlas が溢れたら次の prepare で reset | 毎 frame 替わる絵の専用の texture の道 |
| 再描画 | `browser_view_process` は `page_needs_layout` の時だけ redraw。paint だけの道は `page_needs_paint`（`page/input.c`）。`page_paint` は display list を全部作り直す | paint だけの世代（frame ごと） |
| JS | interface の表（`bind/internal.h` の `bind_interface`、`window_interfaces[]`、`node_prototype_index`）。event は `bind_fire_event`、handler の名前は `bind/handler.c` の `handler_types[]`（media の event は無い）。Promise を返す native は `fetch` の形（root して後で settle、`bind_checkpoint`） | HTMLMediaElement・HTMLVideoElement・MediaError・TimeRanges、media の event |
| event loop | engine は単一の thread（loader の DNS の resolver だけ別、pipe で起こす）。埋め込み側が `browser_view_poll_fds`・`_timeout`・`_process` を回す。poll の fd は loader の物だけ。rAF は 16 ms の timer。GC は main の thread の stack を走査する | media の thread からの wake の fd、frame の期限の timeout |
| network | loader は GET だけ、追加の header は If-None-Match だけ、応答は全部を受けてから 1 回の callback、上限 256 MiB、cache は 200 だけ。Range・206 は無い。`file:` は同期で全部を読む | Range の要求・206・Content-Range |
| 音 | shell にも engine にも音は無い。audiod の client は videoplayer の `audio.c` だけ（protocol を直接、`AUDIOD_STREAM_VOLUME` あり） | 音の出口 |
| 公開の API | `<browser.h>`（`BROWSER_API_VERSION` 2）。platform の service の hook は無い。ABI の変更は [browser-component の規則](../../standards/browser-component.md) §7 で計画に記録 | 下の D5 のとおり変えない |
| player の部品 | mediafile（MP4・MOV・MKV・WebM、fragment の MP4 は読まない、size が要る）、`codec.c`（FFmpeg 9 と 7 の major、h264・hevc・vp9・vp8・mpeg4・aac・opus・mp3、AV1 は image の build では無い）、`bitstream.c`、`media.c`（media の thread、8 枚の AVFrame の ring、音の位置で時計、video の track が無いと開けない）。mediafile は library でなく player に直接 compile | 共有する形 |

## 設計

### D1. 部品の共有: 新しい library `libmedia`（推奨、要判断 U1）

`userland/desktop/libmedia/`（`/lib/libmedia.so`、header は `userland/desktop/libmedia/media.h`。base の desktop の中だけの内部の API で、外部の SDK にしない）に、次を移す（code は今の物を動かし、名前の接頭辞を揃える）:

- `mediafile`（demux）、`codec.c`・`codec-layout.h`・`bitstream.c`（libavcodec の dlopen の add-in）、`audio.c`（audiod の client）、`media.c`（再生の engine: thread・ring・時計）。
- videoplayer と libbrowser の両方がこれに link する（libbrowser の DT_NEEDED に `libmedia.so` が増える。`check-dynamic-elf.py --needed` の一覧を直す）。libmedia の NEEDED は libc だけ（FFmpeg は dlopen）。
- 理由: 同じ物を 2 つに compile すると、FFmpeg の版の表・bitstream の直しが二重になる。dlopen の状態は process に 1 つで足りる。
- 代案: libbrowser に source を直接 compile（library を増やさない。直しは二重）。

### D2. mediafile の source（reader の callback）

```c
struct mf_source {
	int (*read_at)(void *context, uint64_t offset, void *data, size_t size);	/* 0、または errno（ECANCELED: 止められた） */
	uint64_t size;									/* 全体の大きさ（必須） */
	void *context;
};
int mf_open_source(const struct mf_source *source, struct mf_file **file);
```

- `mf_open(path)` は pread の source を作る wrapper として残す。`struct mf_file` の `fd` を source に替え、`mf_read_at` は `read_at` を呼ぶ（format の reader は変えない）。
- `read_at` は待ってよい（media の thread の上で呼ばれる）。止める時（seek・close・page の破棄）は source の側が `ECANCELED` を返して待ちを解く。mediafile は ECANCELED をそのまま返し、media の engine はそれを「中断」として扱い、error にしない。
- 大きさの分からない応答（chunked で長さ無し、生放送）は扱わない（`MEDIA_ERR_SRC_NOT_SUPPORTED`）。fragment の MP4（MSE・DASH の前提）は mediafile が読まないので同じ扱い。

### D3. network の読み込み（libbrowser の `page/media-fetch.c`、main の thread）

- **Range は区切った GET**: 要素ごとの byte の cache（256 KiB の block の疎な表、予算 32 MiB、再生位置から遠い block から捨てる）を main の thread が持つ。`read_at` が無い block を要ると、要求を queue に積み、wake の fd（D4）を書いて condition で待つ。main の thread はそれを見て `Range: bytes=A-B`（最初の 1 本は 256 KiB、以後 2 MiB ずつ、先読みは同時に 2 本まで）を出す。
- **loader の変更（`net/loader.c`・`net/http.c`）**: `net_loader_fetch` に追加の header（Range）を渡せる形（例 `net_loader_fetch_range(loader, url, first, last, done, context, &request)`）、206 を成功として受け、`Content-Range` の全体の大きさを `net_response` に足す。206 は cache に入れない。応答は今のとおり全部を受けてから callback（2 MiB 以下なので streaming の受信を足さない）。keep-alive の pool で続きの要求は同じ接続になる。
- server が Range を無視して 200 を返したら: 全体を 1 回で受ける（256 MiB の上限まで）。それを超えたら `MEDIA_ERR_NETWORK`。最初の 256 KiB の要求で全体の大きさが決まる（206 の Content-Range、または 200 の本体の長さ）。
- `file:` は直接 pread の source（`mf_open`）。`data:` は解いた bytes の memory の source。`blob:` は無い（MSE も無い）。
- cookie・redirect・TLS は loader の今のとおり。cross-origin の video は no-cors で再生してよい（canvas が無いので taint の扱いは要らない）。混在 content（https の page の http の video）は今の loader の規則に従う。

### D4. 再生の engine と thread

- 要素ごとに libmedia の engine（media の thread 1 本）。media の thread は VM・DOM に触らない（GC は main の stack だけを走査し、cell を共有しない）。engine と要素の間は mutex で守る小さな状態と、**view ごとの wake の pipe**（`browser_view_poll_fds` に足す。fd の数が 1 増えるだけで公開の API は変わらない）。
- engine から main への知らせ（wake を書く）: metadata（寸法・長さ・track）、最初の絵、buffer の不足と回復（waiting・playing）、終わり、error、seek の完了。main は `browser_view_process` の中でそれを読み、event を task として送る。
- **絵**: media の thread は AVFrame の参照の ring（今の 8 枚を 4 枚に）と、BGRA の出力 buffer 2 枚（表示中と次）を持つ。次に期限の来る絵を media の thread が BGRA に変換しておき（sws、固有の寸法、上限 1920×1080 を超えたら縮める）、main は期限に buffer を取り替えるだけ（main の thread で変換しない）。
- **時計**: 音があれば audiod の read の位置、無ければ monotonic。headless（`browser_view_settle` の仮想の時計）では page の仮想の時計に従い、音を出さず、絵を待ってから進める（試験の決定性のため）。
- `browser_view_timeout` に「次の絵の期限」と timeupdate（再生中 250 ms ごと）を足す。期限で絵を替えたら paint だけの世代（`page->media_generation`）を進め、`page_needs_paint` がそれを見て、`browser_view_process` が redraw の callback を呼ぶ。shell はその callback で描く（今の FIFO の present のまま）。
- 上限: 同時に再生する要素は view ごとに 4。5 つ目の `play()` は `NotAllowedError` で断る（制限として記録）。decoder の thread の数は libavcodec の `threads` を 2。

### D5. 音（engine が直接 audiod へ、`<browser.h>` は変えない）

- libbrowser は process の中の library なので、libmedia の audiod の client で直接 stream を作る（shell を通さない）。要素ごとに 1 本の playback stream（16-bit stereo 48 kHz、今の player と同じ）。`volume`・`muted` は `AUDIOD_STREAM_VOLUME`（muted は 0）。pause で stop、再生で start、seek で flush、要素が文書から外れる・page を離れる・view の破棄で destroy。
- audiod が無い（接続できない）時は音無しで絵だけ（時計は monotonic）。
- **公開の API は変えない**（v2 のまま、docs の変更も無し）。埋め込み側が音を止めたい（Settings の窓など）・tab の「音が出ている」表示が要る時は、後で関数を足す（例 `browser_view_set_media(view, flags)`、export を足すだけで struct は変えない）。今の利用者（browser・browser-probe）には要らない。→ 要判断 U4。
- 「shell を通して audiod へ」の形（host の callback）は採らない: callback の struct の配置が変わり v3 になり、shell が音の thread を持つ必要が出る。

### D6. DOM・layout・描画

- `dom_element` に `struct media_element *media`（malloc、`element_finalize` で engine を止めて解放）。要素が文書から外れたら pause（仕様どおり）。
- UA の規則: `video { object-fit: contain; }` は object-fit が無いので、描画の側で contain（縦横比を保って箱の中央、余白は描かない）を固定で行う。`audio:not([controls]) { display: none; }`、`audio[controls]` は D9 の決定まで 300×54 の空の箱。
- layout: VIDEO を replaced に。固有の寸法は videoWidth×videoHeight（metadata の後）、無ければ poster の画像、無ければ 300×150。metadata で寸法が決まったら layout の世代を進める（1 回だけ）。
- poster: `page/images.c` の walk に `<video poster>` を足す。絵が出るまで poster（無ければ透明）。最初の絵は readyState が HAVE_CURRENT_DATA になった時点で出す（poster がある時は再生か seek が始まるまで poster のまま）。
- display list に `PAINT_VIDEO`（要素の id、表示中の buffer の世代、箱、contain の矩形）を足す。
  - CPU（`software.c`）: buffer から双線形で写す（画像の最近傍と別。参照の描画と GPU の一致の試験は ±2 の許容）。
  - GPU（`vulkan.c`）: atlas を使わず、要素ごとの専用の `VkImage`（B8G8R8A8、optimal、staging の buffer から copy）を持ち、世代が変わった時だけ写す。sampler は linear（今の pipeline の texture の座標の型を共有し、video 用の descriptor を足す）。要素が消えたら次の prepare で破棄。
- `page_paint` は frame ごとに display list を全部作り直す（今の形）。重い page で 30 fps が保てない時の最適化（video の item だけ差し替える）は後（制限）。

### D7. JS の API（部分集合）

- **HTMLMediaElement**: `src`・`currentSrc`・`crossOrigin`（反映だけ）・`networkState`・`preload`（`none` なら play か load まで取らない、他は metadata まで）・`buffered`・`load()`・`canPlayType()`・`readyState`・`seeking`・`currentTime`（set で seek、key frame まで戻って目標の時刻まで捨てる）・`duration`（metadata まで NaN）・`paused`・`defaultPlaybackRate`・`playbackRate`（1.0 以外は値を持つだけで速さは 1.0、制限）・`played`・`seekable`・`ended`・`autoplay`・`loop`・`play()`（Promise）・`pause()`・`controls`・`volume`・`muted`・`defaultMuted`、定数 `NETWORK_*`・`HAVE_*`。
- **HTMLVideoElement**: `width`・`height`・`videoWidth`・`videoHeight`・`poster`・`playsInline`（反映だけ）。
- **HTMLAudioElement** と `new Audio()`: D9 の決定による。
- **MediaError**（`code`・`message`、定数）、**TimeRanges**（`length`・`start()`・`end()`）。
- **event**: loadstart・progress・suspend・abort・error・emptied・stalled・loadedmetadata・loadeddata・canplay・canplaythrough・playing・waiting・seeking・seeked・ended・durationchange・timeupdate・play・pause・ratechange・volumechange・resize。`handler_types[]` に `on*` を足す。
- **source の選択**: `src` があればそれ、無ければ子の `<source>` を順に、`type` を `canPlayType` で見て空でない最初の物（`media` の属性は見ない）。
- `canPlayType`: container（video/mp4・video/webm・audio/mp4・audio/webm・audio/mpeg、Matroska）と `codecs=` の codec を、**libavcodec が読み込めて decoder がある時だけ** `"maybe"`（codec まで分かれば `"probably"`）。libavcodec が無ければ常に `""`。
- **play() の Promise**: 再生が始まった（HAVE_FUTURE_DATA 以上）時に resolve、`pause()`・`load()`・error で `AbortError`・`NotSupportedError` で reject。自動再生の規則（D8）で断る時は `NotAllowedError`。
- 範囲の外（後）: MSE（MediaSource、YouTube などの多くの動画 site が要る）、EME、WebVTT（`<track>`・textTracks）、`requestVideoFrameCallback`、Picture-in-Picture、Fullscreen API、`captureStream`、canvas への `drawImage(video)`、HLS・DASH、`playbackRate` の速さの変更。

### D8. 自動再生（要判断 U2）

- 推奨: `autoplay` と user の操作の無い `play()` は **muted の時だけ許す**（音を出す自動再生は `NotAllowedError`）。user の操作（click・key・touch の event の中、またはその後 5 秒）の中の `play()` は音つきで許す。Chrome・Safari と同じ考え方。
- 代案: 全部許す（簡単だが、page を開いただけで音が鳴る）。全部断る。

### D9. 範囲の判断（要判断 U3・U5）

- U3 `<audio>`: engine は同じ（video の track が無いだけ）。今の player の engine は video の track が要るので、audio だけの再生を足す。推奨: 同じ WS で `<audio>` と `new Audio()` も入れる（追加の作業は小さい: layout は表示しない、JS の interface 1 つ）。
- U5 `controls` の属性（標準の操作の部品）: 推奨: engine が描く最小の操作の帯（再生・一時停止、seek の bar、時刻、mute）を別の Phase（p006）で。それまでは `controls` を無視し、page が自分で作る操作だけが効く。

### D10. GPU の decode（WS083）を後で足す境界

- libmedia の decoder は ops の表（open・send・receive・flush・close）にし、software（libavcodec）と vulkan-video を同じ形で選ぶ。絵の型は `struct media_picture { kind; ... }`: 今は `MEDIA_PICTURE_BGRA`（CPU の buffer）だけ。後で `MEDIA_PICTURE_VK_IMAGE`（NV12 の `VkImage`、YCbCr の変換を shader で）を足す。
- Vulkan Video は decode の queue family を持つ device が要る。browser の device は shell が貸す（`struct browser_gpu`）ので、その時に decode の queue family を渡す形（`browser_gpu` の拡張 = API の版を上げる）が要る。今は決めず、WS083 の後の Phase で設計する（ここでは境界だけ）。

### D11. 安全と資源

- demux と decode は page の data を同じ process の中で読む（WS074 の D1: 敵意のある site に安全ではない、sandbox は後）。mediafile の上限（packet 64 MiB、moov 64 MiB）、byte の cache 32 MiB、BGRA の buffer 2 枚（1080p で 16 MiB）、AVFrame の ring 4 枚。
- 固有の寸法の上限 4096×2304（超えたら `MEDIA_ERR_SRC_NOT_SUPPORTED`）。
- page を離れる・view の破棄で、全部の engine を止め（source の待ちを ECANCELED で解く）、thread を join してから DOM を解放する。

## Phase の分割（案、ws.md に投影）

| Phase | 内容 | 依存 | 確かめ |
| --- | --- | --- | --- |
| p002 | libmedia（D1・D2・D4 の engine の一般化: source、audio だけ、buffer の不足、BGRA の 2 枚、wake の知らせ、decoder の ops）、videoplayer をそれに移す | p001 | host（mediafile・codec の今の試験を libmedia で、reader の source の試験）。T1 は videoplayer の回帰（WS122 の p004 の試験） |
| p003 | loader の Range（206・Content-Range、cache しない）と `page/media-fetch.c`（block の cache、file・data の source） | p001 | host（python の Range の server、Range を無視する server、redirect、途中の中断） |
| p004 | DOM・layout・描画（replaced の箱、poster、`PAINT_VIDEO`、CPU の双線形、GPU の専用 texture、paint だけの世代） | p002 | host の CPU の描画（headless の仮想の時計で決まった時刻の絵）、browser-probe の offscreen の GPU と CPU の一致 |
| p005 | JS の API（D7）、event loop（wake の fd、timeout、timeupdate）、自動再生（D8）、（U3 なら `<audio>`） | p003・p004 | host の JS の試験（event の順、play の Promise、seek） |
| p006 | 音（D5、volume・muted、同期）と（U5 なら）操作の帯 | p005 | T1（QEMU）: browser で試験の page の video を再生、時刻ごとの PNG、audiod の stream の log |
| 最後 | 全文規約と回帰 | 全部 | — |

試験の素材: host の ffmpeg で作る（H.264+AAC の MP4、VP9+Opus の WebM、moov が後ろの MP4）。tree に入れない（WS122 の試験と同じ）。image は `plan/ws121/tests/` の config.mk と個別の複写だけで作る（2026-10-04 ユーザーの規則）。

## 要判断（ユーザーへ）

- U1: 部品の共有を新しい library `libmedia.so` にするか（推奨）、libbrowser に直接 compile するか。
- U2: 自動再生の規則（推奨: muted だけ自動、音つきは user の操作の後）。
- U3: `<audio>` を同じ WS に入れるか（推奨: 入れる）。
- U4: 公開の `<browser.h>` は変えない（音は engine が直接 audiod へ）でよいか。埋め込み側の mute・「音が出ている」の表示は後で関数を足す。
- U5: `controls` の標準の操作の帯を作るか（推奨: 最小の帯を p006 で）。
- 参考（判断ではない）: MSE が無いので、YouTube など MSE で配る site は再生できない。普通の `<video src=".mp4">`・`.webm` の page が対象。

## 確かめ

- 設計だけ。code の変更・build・試験は無い。
- design-reviewer の review（2026-10-05 夜、1 回目）: **このままでは実装に進めない**（高 6・中 14・低 8）。設計の改訂は未（WS173 の優先で中断、下の「再開点」）。

## 未実施

- 実装（p002 以降）、QEMU・実機。

## design-reviewer の 1 回目の指摘（2026-10-05 夜、改訂は未）

高（改訂で必ず直す）:
- H1: 今の `vp_media_open` は呼び出し側の thread で `mf_open` する（`videoplayer/media.c` の open の後に thread を作る）。D3 の `read_at` は main の thread が Range を出すまで待つので、main で open すると deadlock。→ open は要求を積んで返し、mf_open と decoder の open は media の thread で、結果は wake で。
- H2: mediafile と media.c が error を終端にまとめる（`mkv.c` の element_at の失敗を ENODATA に、cues の読みの失敗で黙って cues 無し、`media.c` は mf_read の 0 以外を全部 EOF、`mf_seek` の結果を捨てる）。network の失敗・ECANCELED で `ended` が出る。→ 終端・中断・network・decode を分けて伝える（mediafile は WS122 の部品なので調整）。
- H3: loader の cache 引き・redirect・keep-alive の再試行・304 の経路で Range が落ちる（`loader_begin`・`loader_prepare`）。→ request に range と「cache を使わない」を持たせ毎回 Range を付ける、Content-Range の検証、短い 206、416、If-Range（ETag）で途中の差し替えを検出。
- H4: Range を無視する server の 200 は raw・body・cache の複写で数百 MiB、全部を受けるまで始まらない。→ header が揃った時の callback を足し、大きい 200 は打ち切る（または streaming の受信）、上限と見積もりを D11 に。
- H5: 再生中の要素が GC で回収されて止まる（`new Audio(url).play()`）。→ 再生中・取得中・event の残る間は page の media の一覧から root、finalize は解放だけ、join は page の破棄の順で。
- H6: headless の既定（`BROWSER_FETCH_AT_ONCE`）は loader を作らず、settle は media の要求を回さない（`view/view.c`）。→ fetch の mode ごとの扱い、wake の fd を loader と無関係に出す、settle に media の段。

中（M1〜M14）: thread の間の block の pin と中断の世代・wake の pipe の non-blocking（M1）、headless の決定性の外部時計・試験の素材の許容（M2）、`browser_view_process` に paint だけの redraw の判定と期限の切り上げ・描かれない video は世代を進めない（M3）、GPU の専用 texture の descriptor・draw の分割・copy の位置・破棄は record の中の prepare・key は process で一意・shader の作り直し（M4）、BGRA の buffer の状態と一時停止中の seek の絵（M5）、cues の無い MKV と interleave の悪い MP4 の network の費用・LRU（M6）、再生中の network の失敗・stalled・waiting（M7）、資源の選択の起動の時機・source の fallback・文書から外れた時の stable state・状態と event の表・終端の手順・寸法の変化（M8）、`<audio>` は mp3・ogg・wav を demux できず Vorbis も無い、canPlayType を mediafile の表と突き合わせる（M9）、audiod の client の MSG_NOSIGNAL・timeout・遅延の stream の作成・音が先に尽きた時の時計（M10）、`<browser.h>` の ABI は変えなくても header の説明（poll・timer・音・thread）を更新する・wake の fd を先頭に（M11）、Phase の依存（p002 ← WS122 p003・p004、p003 ← p002）・WS121 の目標「Vulkan Video の hardware decode」との差の判断・試験の素材の置き場所（M12）、libmedia の `vp_log` の未定義・threads の引数・package と host の build（M13）、http(s) の page からの file: の media の禁止（M14）。

低（L1〜L8）: 回転・pasp・BT.709・10 bit の制限の記録、5 つ目の play の扱い、表示の寸法での変換、`dom_element` の pointer、buffered・seekable の換算と Duration の無い MKV、iframe の media、p006 の試験の判定（serial の log を使わない、guest から host の server、小さい素材）、GPU と CPU の一致の許容。

## 再開点

上の指摘で D1〜D11・Phase の表・U を改訂し、design-reviewer の 2 回目を通してから、ユーザーへの質問（U1〜U5 と M12 の WS121 の目標の判断、M9 の `<audio>` の範囲、H4 の fallback の上限）を Q1 に送る。
