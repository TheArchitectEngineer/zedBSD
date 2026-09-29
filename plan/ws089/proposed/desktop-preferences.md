# 案: desktop の設定の file と libkeiland の API、zdesktop の反映（ws089-p001、未適用）

所有: libkeiland と zdesktop（`userland/desktop/wayland`）は WS035。main の許可の後、ws089-p007（新設、p004・p005 の前）で最小の追加として行う。
理由と代わりの案は [design.md](../design.md) §6.3・§9 の D3。

## 保存先と形

- `$HOME/.config/keiland/desktop.conf`。1 行に `key=value`、`#` で始まる行と空行は無視、未知の key は保つ（書き戻しで消さない）。
- 値は印字できる UTF-8、1 行 255 byte まで。書くのは Settings だけ。一時 file（同じ directory）に書いて `fsync`・`rename`。
- key（デモの範囲）:

| key | 値 | 既定 | 当てる所 |
| --- | --- | --- | --- |
| `wallpaper` | PPM（P6）の絶対 path | 無し（`--wallpaper` の値） | zdesktop（壁紙と blur を作り直す） |
| `window.opacity` | 70〜100 | 100（`--window-opacity` の値） | zdesktop |
| `pointer.speed` | 25〜300（百分率） | 100 | zdesktop（相対の pointer） |
| `pointer.natural` | 0・1 | 0 | zdesktop（wheel） |
| `touchpad.natural` | 0・1 | 0 | zdesktop（touchpad を区別できるまでは `pointer.natural` と同じに当てる） |
| `scroll.inertia` | 0・1 | 1 | libkeiland の scroller |
| `keyboard.repeat.rate` | 5〜60（毎秒） | 25 | zdesktop（`wl_keyboard.repeat_info`） |
| `keyboard.repeat.delay` | 150〜1000（ms） | 400 | 同上 |

## libkeiland（`include/libc/keiland.h` に節を足す、新しい file `userland/desktop/libkeiland/preferences.c`、exports.map に `keiland_preferences_*`）

```c
/*
 * The desktop's preferences (ws089): the user's choices of the desktop's look
 * and input, kept in ~/.config/keiland/desktop.conf.  Settings writes them;
 * zdesktop and the scroller read them.  Nothing here speaks to the
 * compositor: zdesktop notices the file changed.
 */
struct keiland_preferences;

#define KEILAND_PREFERENCES_VALUE_MAX	256U

struct keiland_preferences *keiland_preferences_open(void);          /* reads the file (a missing file is empty) */
void keiland_preferences_close(struct keiland_preferences *preferences);
int keiland_preferences_reload(struct keiland_preferences *preferences, int *changed); /* reads again when the file's mtime moved */
int keiland_preferences_get(const struct keiland_preferences *preferences, const char *key, char *value, size_t size); /* ENOENT when unset */
int keiland_preferences_get_int(const struct keiland_preferences *preferences, const char *key, int fallback, int minimum, int maximum);
int keiland_preferences_set(struct keiland_preferences *preferences, const char *key, const char *value); /* writes the file at once */
```

## zdesktop（`userland/desktop/wayland`）

- `main.c`: session のとき `keiland_preferences_open`。既存の `--wallpaper`・`--window-opacity` は既定として残し、file の値が勝つ。
- 1 秒ごとの timer（既存の loop の timeout の中）で `keiland_preferences_reload`。変わったら:
  - `wallpaper`: `glass.c` の `wallpaper_create` を新しい path で作り直す（古い image を release、backdrop・blur も）。読めない path は log して今のまま。
  - `window.opacity`: `server->window_opacity` を替えて全体を描き直す。
  - `pointer.speed`・`pointer.natural`: `input.c` の相対の動きと wheel に当てる。
  - `keyboard.repeat.*`: `seat.c` の `REPEAT_RATE`・`REPEAT_DELAY_MS` を変数にし、次の keyboard の bind と、既存の keyboard へ repeat_info を送り直す。
- greeter（`_greeter`）では読まない。
- 変更の見込み: preferences.c 約 350 行（新しい）、zdesktop 約 150 行（main・glass・input・seat）、keiland.h 約 40 行。

## 試験

- host: libkeiland の preferences の読み書き（未知の key を保つ、原子的な置き換え、範囲の外の値）。
- guest: Settings で壁紙を選ぶ → 1 秒以内に zdesktop の log に `PREFERENCES wallpaper` の行、画面を撮って壁紙が替わったのを目で見る。
