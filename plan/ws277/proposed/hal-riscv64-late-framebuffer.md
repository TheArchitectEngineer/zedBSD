<!-- awesome-plan project=zedbsd record=ws277-proposed-hal-late-framebuffer -->

# WS277 の HAL の API の差分の案: kernel が後から設定した framebuffer を早期の console に渡す（承認の前は適用しない）

- 2026-10-20 P1（q1353-i01、[design.md](../design.md) 版 3.2 §15.1 の 3）。guardrail「HAL の API の変更は差分ごとの事前承認」に従い、ここに置くだけで `include/hal/`・`src/hal/` は変えていない。
- 要る時: VF2 で U-Boot が GOP を渡さず（または bootfb が mode を収めず）、DC8200 の driver（p005）が kernel で framebuffer を確保して HDMI に出す時。今の HAL の fbcons（WS256 p011）は board の初期化の時に loader の handoff の画面だけを `riscv64_fbcons_attach()`（`src/hal/riscv64/fbcons.h`、hal.h の外）で受けるので、kernel から渡す口が無い。
- 要らない時: GOP があり bootfb をそのまま scanout する時（配布の U-Boot の普通の場合、§15.1 の 2）。**p005 の実装はこの口なしで完結する**（確保した framebuffer の時は画面に文字が出ず serial だけ）。

## 差分（案）

### `include/hal/hal.h`（追加だけ）

```c
/*
 * A screen the kernel set up after boot, for the early console to draw on.
 *
 * physical is the first pixel's address, pitch the bytes from one row to
 * the next, format a KERN_BOOT_FRAMEBUFFER_* code.  The memory must stay
 * the screen for the rest of the system's life.
 */
struct hal_console_screen {
	hal_physaddr_t physical;
	uint32_t width;
	uint32_t height;
	uint32_t pitch;
	uint32_t format;
};

/*
 * Moves the early console onto a screen the kernel set up, redrawing its
 * text there.  ENOTSUP on a port whose console cannot draw on a screen;
 * EINVAL for a screen it cannot use.
 */
int hal_console_set_screen(const struct hal_console_screen *screen);
```

### `src/hal/riscv64/`（実装、参考）

- `hal_console_set_screen` は、画面を device の窓（uncached）に写し、`riscv64_fbcons_attach()` に flush 無し（uncached なので要らない）で渡し、grid を描き直す。名前の handoff（`riscv64.framebuffer.*`）は変えない（loader の画面の記録のまま）。
- 他の port（amd64・i386・arm64・ppc64）は ENOTSUP を返す stub。

## 理由と代わり

- 代わり (1): kernel の側に framebuffer の text console を足す（HAL は変えない）。kernel の console の多重化になり、WS256 レビュー 2 MJ3 で避けた形。
- 代わり (2): 確保した framebuffer の時は画面に文字を出さない（今の実装）。panic・早期の message が serial だけになる。
- 推奨: 配布の U-Boot が GOP を出すと分かった（2026-10-20）ので、**今は承認を求めない**（代わり (2) のまま）。GOP の無い firmware を支える必要が出た時に、この案を承認の問いにする。
