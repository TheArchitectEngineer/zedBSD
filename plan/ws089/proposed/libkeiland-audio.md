# 案: libkeiland の音量の API（ws089-p001、未適用）

所有: libkeiland は WS035（ws035-p026「system bar の音量、音量も libkeiland 経由」、planning）。ws089-p005 で足し、p026 が同じ API を使う。
audiod の protocol（`userland/base/audiod/protocol.h`）は変えない。

```c
/*
 * The sound output's volume (ws089, for Settings and the system bar): the
 * device volume audiod applies to everything it plays, 0 to 100 per
 * channel, and whether it is muted.  Nothing here waits: keiland_audio_update
 * reads what audiod reported, a set is sent at once.
 */
struct keiland_audio;

struct keiland_audio_state {
	unsigned reachable;	/* 0 while audiod cannot be reached */
	unsigned device;	/* 0 when audiod has no sound device */
	unsigned rate;
	unsigned channels;
	unsigned left;		/* 0..100 */
	unsigned right;
	unsigned muted;
};

struct keiland_audio *keiland_audio_open(void);	/* HELLO, SUBSCRIBE */
void keiland_audio_close(struct keiland_audio *audio);
int keiland_audio_fd(const struct keiland_audio *audio);	/* to poll, -1 while not connected */
int keiland_audio_update(struct keiland_audio *audio, unsigned *changed);	/* VOLUME_CHANGED, reconnects at most once a second */
void keiland_audio_get_state(const struct keiland_audio *audio, struct keiland_audio_state *state);
int keiland_audio_set_volume(struct keiland_audio *audio, unsigned left, unsigned right, unsigned muted);	/* DEVICE_VOLUME */
```

- 新しい file `userland/desktop/libkeiland/audio.c`（約 300 行）、keiland.h の節、exports.map の `keiland_audio_*`、Makefile の source。
- 試しの音（stream）は含めない。要るなら別の追加。
