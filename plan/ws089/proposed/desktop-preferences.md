# 案: desktop の設定の file と libkeiland の API、zdesktop の反映（ws089-p001、未適用）

所有: libkeiland と zdesktop（`userland/desktop/wayland`）は WS035。main の許可の後、ws089-p007（p004・p005 の前）で最小の追加として行う。
理由と代わりの案は [design.md](../design.md) §6.3・§9 の D3。衝突しうる相手: `include/libc/keiland.h`・`libkeiland/exports.map`・`Makefile`
（WS081・WS035）、zdesktop の `main.c`・`glass.c`・`seat.c`・`input.c`（WS035・WS075 のデモの変更）。適用の前に main と順序を合わせる。

## 保存先と形

- `$HOME/.config/keiland/desktop.conf`。HOME が無い・空なら `getpwuid(getuid())->pw_dir`、それも無ければ `ENOENT`（保存しない）。
  directory が無ければ 0700 で作る。
- 1 行に `key=value`、`#` で始まる行と空行は無視、未知の key と注釈は保つ（書き戻しで消さない）。値は印字できる UTF-8、1 行 255 byte まで。
- 書き込みは key 単位: file の隣の lock file を `flock(LOCK_EX)` → file を読み直す → その key だけを変える（または消す）→ `mkstemp` の
  一時 file に書いて `fsync` → `rename` → unlock。二つの writer や手の編集の変更を消さない。
- key（デモの範囲）:

| key | 値 | 既定（key が無いとき） | 当てる所 |
| --- | --- | --- | --- |
| `wallpaper` | PPM（P6）の絶対 path | `--wallpaper` の値、無ければ手続きで描く風景 | zdesktop（壁紙と blur を作り直す） |
| `window.opacity` | 85〜100 | `--window-opacity` の値（無ければ 100） | zdesktop |
| `pointer.speed` | 25〜300（百分率） | 100 | zdesktop（相対の pointer だけ） |
| `pointer.natural` | 0・1 | 0 | zdesktop（wheel） |
| `keyboard.repeat.rate` | 5〜60（毎秒） | 25 | zdesktop（`wl_keyboard.repeat_info`） |
| `keyboard.repeat.delay` | 150〜1000（ms） | 400 | 同上 |

## libkeiland（`include/libc/keiland.h` に節、新しい file `userland/desktop/libkeiland/preferences.c`、exports.map に `keiland_preferences_*`、`KEILAND_VERSION` を 1 つ上げる）

```c
/*
 * The desktop's preferences (ws089): the user's choices of the desktop's look
 * and input, kept in ~/.config/keiland/desktop.conf.  Settings writes them one
 * key at a time; zdesktop reads them and notices when the file changes.
 * Nothing here speaks to the compositor.
 */
struct keiland_preferences;

#define KEILAND_PREFERENCES_VALUE_MAX	256U

struct keiland_preferences *keiland_preferences_open(void);	/* reads the file (a missing file is empty); NULL with errno when there is no home */
void keiland_preferences_close(struct keiland_preferences *preferences);
int keiland_preferences_reload(struct keiland_preferences *preferences, int *changed);	/* reads again when the file's inode, size or st_mtim moved */
int keiland_preferences_get(const struct keiland_preferences *preferences, const char *key, char *value, size_t size);	/* ENOENT when unset */
int keiland_preferences_get_int(const struct keiland_preferences *preferences, const char *key, int fallback, int minimum, int maximum);
int keiland_preferences_set(struct keiland_preferences *preferences, const char *key, const char *value);	/* locks, rereads, changes one key, replaces the file */
int keiland_preferences_unset(struct keiland_preferences *preferences, const char *key);
```

Settings は slider の drag の間は file に書かず、drag の終わり（または最後の変更から 200 ms 後）に書く。

## zdesktop（`userland/desktop/wayland`）

- `main.c`: greeter でないとき（`--session` の無い試験の起動も含む）、**glass を作る前に** `keiland_preferences_open`（壁紙の PPM を二度読まない）。
  既存の `--wallpaper`・`--window-opacity` は既定として残し、file の値が勝つ。greeter は読まない（login で壁紙が替わるのは仕様）。
- 1 秒ごと（既存の loop の中の時刻の確認）に `keiland_preferences_reload`。変わったら変わった key だけ:
  - `wallpaper`: `glass.c` で新しい壁紙と blur と backdrop を先に作り、使用中の frame の完了（fence）を待ってから descriptor の参照を
    差し替えて古い物を release する。読めない path は log して今のまま。所要時間を host と guest で計る（数百 ms の見込み、未計測）。
  - `window.opacity`: `server->window_opacity` を替えて全体を描き直す。100% 未満では差分の描画が止まる（`shell.c` の `zwl_glass_still`）。
  - `pointer.speed`・`pointer.natural`: `input.c` の相対の動きと wheel に当てる（端数は持ち越す）。
  - `keyboard.repeat.*`: `seat.c` の `REPEAT_RATE`・`REPEAT_DELAY_MS` を変数にし、新しく bind した keyboard から効かせる。
- log の行: `ZWL PREFERENCES key=<key> applied` / `failed errno=N`（guest の試験が SSH で読む）。
- 変更の見込み: preferences.c 約 400 行（新しい）、zdesktop 約 200 行、keiland.h 約 40 行。

## 試験

- host: preferences の読み書き（未知の key を保つ、二つの writer の交互の set で両方残る、範囲の外の値、HOME が無い）。
- guest: Settings（App Home から起動、zdesktop と同じ HOME）で壁紙を選ぶ → 1 秒以内に zdesktop の log に `PREFERENCES key=wallpaper applied`、
  画面を撮って目で見る。frame の間隔の最大を reload の前後で計る。
