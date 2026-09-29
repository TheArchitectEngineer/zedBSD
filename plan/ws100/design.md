# WS100 の設計: system bar の音量（icon・popup・確かめの音）と 5330 の HDA

2026-09-30、ws100-p001（サブエージェント）。対象の基準は [ws.md](ws.md) の A1〜A7。source は変えていない（調べと設計だけ）。

## 1. 今あるもの（調べた事実）

| 部品 | 今 | 出典 |
| --- | --- | --- |
| kernel の HDA | `src/drivers/pci/pci-hda.c`: PCI の class `0x0403xx`（mask `0xffff00`）で bind。Intel の codec（vendor 0x8086 = display の HDMI codec）は飛ばし、最初の analog codec の出力 pin（複数）と入力 1 つを選ぶ。出力の amplifier（`volume_nid`・`volume_steps`・mute）を探し、起動時に 100% にする。`/dev/dspN` と `/dev/mixerN`（`KERN_AUDIO_GET_VOLUME`・`KERN_AUDIO_SET_VOLUME`、0〜100 と mute）。amplifier の無い codec では set が無い（`ops.set_volume` が NULL） | `pci-hda.c` 328 行・1279〜1360 行・620 行、`src/drivers/audio/audio.c` 2303 行〜 |
| audiod | `/run/audiod.sock`（mode 0666）。`AUDIOD_DEVICE_VOLUME`（0〜100、mute、`/dev/mixer0` に ioctl）、`AUDIOD_SUBSCRIBE` と `AUDIOD_VOLUME_CHANGED`（購読した client へ配る）が既にある。stream ごとの音量（`STREAM_VOLUME`、65536 が等倍）もある。device が無ければ `welcome.device = 0` で、get は 100・非 mute を返す | `userland/base/audiod/protocol.h`・`main.c` 417〜432 行・`device.c` 207〜245 行 |
| libkeiland の音 | まだ無い。WS089 の案 `plan/ws089/proposed/libkeiland-audio.md`（`keiland_audio_open/close/fd/update/get_state/set_volume`、試しの音は含めない、`KEILAND_VERSION` を上げる）が未適用で残る | 同 |
| zdesktop の system bar | 右から時計・電池・network の icon（`bar->signal_x = battery_x - 36`）。network は libkeiland（`keiland_network_*`）を通し、icon（`zwl_network_draw_icon`）と click で開く menu（`zwl_network_draw_menu`・`_button`・`_key`・`_motion`・`_is_open`・`_tick`）を持つ。この作りをそのまま手本にできる | `userland/desktop/wayland/network.c`・`shell.c` 1651〜1656 行・2593 行 |
| wheel | `seat.c` の `zwl_seat_axis` が App Home（`zwl_home_axis`）と title bar（`zwl_titlebar_axis`）に先に渡し、取られなければ client へ | `seat.c` 695〜720 行 |
| 利用者の設定 | `~/.config/keiland/desktop.conf`（`keiland_preferences_get/set/reload`、WS089 p007）。zdesktop は起動時と 1 秒ごとに読み、壁紙・窓の不透明度・pointer・key の repeat を当てる | `userland/desktop/libkeiland/preferences.c`・`userland/desktop/wayland/preferences.c` |
| Settings の Sound の頁 | 「Sound service Running / Not running」と「Volume … are coming in a later version of Kei.」だけ | `userland/desktop/settings/page-input.c` 160 行 |
| QEMU の音の試験 | ws035-p007: `-device ich9-intel-hda`（か `intel-hda`）`-device hda-output`（か `hda-duplex`）`,audiodev=w` と `-audiodev wav,id=w,path=…`、`hda-wav-check.py`（pattern・peak・windows）。`mixer=off` を外すと QEMU が codec の amplifier の値を wav に当てる（ws035-p007 の volume の evidence） | `plan/ws035/tests/run-hda-qemu.sh`・`hda-wav-check.py` |
| image | full の config（`config/ci/config-amd64.mk`）は `CONFIG_DRIVER_PCI_HDA := y` と audiod を持つ。基準の image（WS099 の criteria、login・graphical の系）は audiod を入れていない（`config-amd64-audiod.mk` は ws035 の HDA の試験用） | 各 config |

## 2. 5330 の HDA（legacy の経路）の調べ

### 2.1 試験機 `solaris10-man`（5330、Linux 6.19、読むだけ）

| 見たもの | 結果 |
| --- | --- |
| `lspci -nn`・`-vv` | `00:1f.3 Audio device [0403]: Intel Alder Lake PCH-P HDA Controller [8086:51c8] (rev 01) (prog-if 80 [HDA compatible with vendor specific extensions])`、Subsystem Dell `1028:0b02`、BAR0 16 KiB・BAR4 1 MiB。driver は無し（bind していない） |
| class | `0x040380`（kernel の log）。Kei の pci-hda の match（`0x040300` / mask `0xffff00`）に**合う** |
| Linux の選択 | `snd_intel_dspcfg` の `dsp_driver=0`（自動）で、**SOF でなく legacy の `snd_hda_intel` が probe した**（`snd_hda_intel 0000:00:1f.3: enabling device`）。SOF・AVS の module は読み込まれたが使われていない |
| probe の結果 | `deferred probe pending: snd_hda_intel: couldn't bind with audio component`。この host は WS031 の VFIO の試験のために i915 と xe を blacklist しており（`/etc/modprobe.d/vfio-igd.conf`）、display の audio の部品（i915）と結べないので保留。**codec の列挙まで進んでいない**ので `/sys/bus/hdaudio`・`/proc/asound` は空 |
| ACPI の NHLT（DSP 向けの endpoint の表） | endpoint は 2 つ、どちらも SSP（I2S）の render、device_type 0（Bluetooth の経路）。**DMIC は無い**。Linux の自動の選択が legacy になったのと合う（DMIC が無ければ legacy の HDA が選ばれる） |

→ 5330 は legacy の HDA の経路（firmware 不要）で扱う機械と Linux も判断している。内蔵の speaker・headphone は HDA link の analog codec に付いている見込みが高い（DMIC も SoundWire も NHLT に無い）。ただし **analog codec の widget（speaker・headphone の pin）はまだ誰も見ていない**。Linux は i915 の保留で codec を読まず、Kei は 5330 で HDA を動かした記録が無い（これまでの実機の試験は IGD だけの passthrough）。試験機の設定を変えない約束なので、Linux で codec を読むこと（i915 の blacklist を外す）はしていない。

### 2.2 Kei の pci-hda が 5330 で動くための危険

1. **display の電源の部品**: Linux が待つ「audio component」は、HDA link の上の Intel の HDMI codec に i915 の display の電源が要るため。Kei の pci-hda は Intel の codec を飛ばすので、analog codec だけなら要らない見込み。ただし controller の reset の後の codec の列挙（STATESTS）に display の電源の影響が出ないかは実機でしか分からない。
2. **prog-if 0x80（vendor の拡張）**: firmware（BIOS）が Audio DSP を有効にしていても、legacy の経路は同じ BAR0 の HDA の register で動く（datasheet の「baseline の Intel HD Audio の動作」、ws.md）。Linux の `snd_hda_intel` は Skylake 以降の Intel で、`PCI_CFG` の TCSEL・snoop（`AZX_DCAPS_INTEL_SKYLAKE`）、`EM2`・`GCTL` の設定をする。Kei の pci-hda がそれらの Intel 固有の設定を持つかは p006 で比べる。
3. **codec の amplifier の無い pin**: speaker が amplifier の無い pin だと `volume_steps == 0` で `/dev/mixer0` の set が無い（§3.2 の software の音量で補う）。
4. **EAPD（外付けの amplifier の電源）**: 内蔵の speaker は codec の EAPD や GPIO で amplifier を入れる機種が多い（Dell の Realtek の codec によくある）。pci-hda は `HDA_PIN_CAP_EAPD` を読むので EAPD は入れる見込みだが、GPIO での amplifier は Linux の codec 別の quirk の範囲で、Kei には無い。鳴らない場合の第一の容疑。

### 2.3 5330 での確かめ方（p006、実機はユーザー）

1. Kei を 5330 で直に起動し（passthrough でなく。HDA の passthrough は試験機の設定の変更が要る）、kernel の log の `hda: codec N, M output pin(s), input …, volume yes/no, …` と `hda: codec N not used` を SSH で読む（Kei の guest の harness）。codec の vendor・device と、pin の設定（speaker・headphone）を log に出す小さな diagnostics を pci-hda に足すのが要る（p006 の中）。
2. `audiod-client`（ws035-p009）か `audiotest play` で音を出し、ユーザーが speaker と headphone で聞く。
3. 鳴らなければ、codec の dump（全 widget の caps・pin の設定・EAPD）を log に出し、Linux の Realtek の codec の quirk（codec の vendor・device と Dell の subsystem `1028:0b02` で引く）と比べる。GPIO や COEF の quirk が要るなら A7 は Future Work に回す（ws.md のユーザーの判断: デモに必須ではない）。

## 3. 設計

### 3.1 全体の流れ

```
 system bar の icon・popup（zdesktop）          Settings の Sound の頁（後、p005）
        │ keiland_audio_*（libkeiland、新）            │ keiland_audio_* と keiland_preferences_set
        ▼                                             ▼
 audiod（/run/audiod.sock）: DEVICE_VOLUME・SUBSCRIBE/VOLUME_CHANGED（既存）・FEEDBACK（新）
        │ /dev/mixer0（codec の amplifier）、無ければ audiod の software の音量（新）
        ▼
 pci-hda（kernel）── codec の amplifier・EAPD ── speaker・headphone
 保存: ~/.config/keiland/desktop.conf の sound.volume・sound.muted（zdesktop が書き、session の始めに audiod へ当てる）
```

### 3.2 audiod（p002）

protocol は version 1 のまま、request を 1 つ足す（知らない request に古い audiod は `ERROR EINVAL` を返すので、client はそれを「確かめの音なし」として扱える）。

- **`AUDIOD_FEEDBACK`（11）**: payload なし（header だけ）。audiod が内蔵の短い音（約 100 ms）を、device の音量で 1 回鳴らす。
  - 音: 880 Hz の正弦波に 5 ms の立ち上がりと指数の減衰（約 100 ms で -40 dB）、振幅は full scale の約 -12 dB。audiod の起動時に device の rate・channels で 1 回だけ作り、memory に持つ。
  - 重ならない: 鳴っている間に次の `FEEDBACK` が来たら、今の音を止めて頭から鳴らし直す（A4「連続の操作では重ならない」）。
  - 他の stream と混ぜる（mix.c の既存の mix に、audiod 自身の内部の stream として入れる）。device が無ければ `DONE` を返して何もしない。
- **software の音量**: codec に amplifier が無い（`/dev/mixer0` の set が `EOPNOTSUPP`）ときは、audiod が mix の最後に device の音量（0〜100 → 振幅、mute は 0）を掛ける。今は amplifier が無いと `DEVICE_VOLUME` が効かない（黙って無視）ので、A2〜A4 が 5330 や QEMU の `mixer=off` で満たせない。
  - 百分率から振幅: 0 は無音、他は 60 dB の幅の対数（`gain = 10^((p - 100) * 0.6 / 20)`）。codec の amplifier の段数への換算（pci-hda）が線形なら、人の耳にそろうように pci-hda の側も対数に合わせるかを p002 で決める（QEMU の wav で段ごとの振幅を測る）。
- `DEVICE_VOLUME` の後の `VOLUME_CHANGED` の配りは既存のとおり（zdesktop と Settings が互いの変更を知る）。

### 3.3 libkeiland（p003）

WS089 の案 `plan/ws089/proposed/libkeiland-audio.md` の API をそのまま入れ、1 つ足す:

- `int keiland_audio_feedback(struct keiland_audio *audio);`（`AUDIOD_FEEDBACK`。古い audiod の `EINVAL` は 0 を返して黙る）
- file `userland/desktop/libkeiland/audio.c`、`include/libc/keiland.h` の節、`exports.map`、Makefile、`KEILAND_VERSION` を 1 つ上げる（15）。
- 待たない: socket は non-blocking、`keiland_audio_update` が届いた `VOLUME_CHANGED` と `WELCOME` を読み、切れていれば 1 秒に 1 回つなぎ直す（network と同じ）。
- `keiland.h`・`exports.map`・Makefile は他の WS（WS081・WS090・WS094）と衝突しうるので、適用の前に main と順序を合わせる。

### 3.4 zdesktop（p004、`userland/desktop/wayland/`）

新しい file `volume.c`（network.c と同じ形）と、`shell.c`・`seat.c` への小さな hook。

- **icon（A1、A6）**: network の icon の左（`bar->volume_x = signal_x - 36`、`status_line` はその左へ）。絵は `icons.c` の線の図形で描く speaker と、音量の段に応じた波 0〜3 本（0%・1〜33%・34〜66%・67〜100%）。mute は speaker と ×。audiod に届かない（`reachable == 0`）か device が無い（`device == 0`）ときは、薄い speaker に斜線（「音なし」）。
- **popup（A2、A6）**: icon の click で開く（touch の tap も、`touch.c` が system bar の指を mouse の左 button として shell に渡すので同じ経路で届く）。network の menu と同じ glass の形（幅 260、角 12）。
  - 上の行: 「Sound」と今の百分率（mute なら「Muted」）。
  - slider の行: track（幅 200）と knob。press・drag で 0〜100 を変える（knob の外の track の press もその位置へ）。
  - mute の行: switch。
  - 音の無いとき: slider と switch を薄くして押せなくし、「Sound service is not running」か「No sound output」の行を出す。
  - popup の外の click と Esc で閉じる。network の menu が開いていれば閉じる（同時に開くのは 1 つ）。
- **wheel（A3）**: `seat.c` の `zwl_seat_axis` の hook の連鎖（App Home・title bar の後）に `zwl_volume_axis` を 1 行足す。pointer が音量の icon（か開いた popup）の上なら、1 notch で 5%（上で増、下で減）、取ったら client へは渡さない。**`seat.c` は IME の hook を持つ file で、人が作業中**。hook の 1 行が IME の部分と重ならないことを p004 の前に main と確かめる（重なるなら `shell.c` の bar の wheel の経路を探す）。
- **確かめの音（A4）**: 変えた時に `keiland_audio_feedback`。wheel は notch ごと、slider は離した時と、drag の間は 250 ms に 1 回まで、mute は外した時だけ。重なりは audiod が防ぐ（§3.2）。
- **audiod へ送る間隔**: drag の間の `DEVICE_VOLUME` は 50 ms に 1 回まで（最後の値は必ず送る）。
- **保存（A5）**: §3.5。
- log（試験用）: `ZWL VOLUME icon x= y= state=`・`ZWL VOLUME set value= muted= via=wheel|slider|mute`・`ZWL VOLUME popup open|close`・`ZWL VOLUME reachable=0|1 device=0|1`。

### 3.5 保存と Settings の関係（A5）

- 保存する所: 利用者の設定の file `~/.config/keiland/desktop.conf` の `sound.volume`（0〜100）と `sound.muted`（0・1）。audiod は root の常駐で利用者を知らないので持たない（greeter の間の音量は audiod の起動時の値のまま、音も鳴らさない）。
- zdesktop（session）は、始めに設定を読み、audiod につながったら `DEVICE_VOLUME` で当てる。設定に無ければ audiod の今の値を使い、書かない。
- 利用者が bar で変えたら、zdesktop が `keiland_preferences_set` で書く（drag の間は書かず、離した時と wheel の最後の notch から 1 秒後に 1 回）。
- 設定の file を他が変えたら（Settings、手の編集）、zdesktop の既存の 1 秒ごとの読み直しで `sound.*` が変わったのを見て audiod に当てる（壁紙と同じ経路、`preferences.c`）。
- audiod の `VOLUME_CHANGED`（Settings が audiod に直に送った、別の client）で icon を直す。設定の file への書きは、変えた本人（zdesktop か Settings）だけが行う（二重に書かない）。
- Settings の Sound の頁（p005）: slider と mute を足し、`keiland_audio_set_volume` と `keiland_preferences_set("sound.volume" …)` を行う。今の「coming in a later version」の文を外す。WS089 の source なので main の許可と順が要る。

### 3.6 QEMU での確かめ方（A1〜A6）

- image: 基準の image（WS099 の criteria）に audiod と、試験の client（`audiod-client`）を足した WS100 の config（`plan/ws100/tests/config-amd64-volume.mk`）。kernel の `CONFIG_DRIVER_PCI_HDA := y`。
- QEMU の引数（`plan/ws035/tests/zdesktop-guest.sh` は `--qemu-extra` を中で組み立て、外から足す口が無い。WS100 の試験用に、同じ起動に音の引数を足す起動の script `plan/ws100/tests/volume-guest.sh` を作る。共有の `zdesktop-guest.sh` は変えない）: `-device intel-hda,id=hda -device hda-duplex,bus=hda.0,audiodev=w -audiodev wav,id=w,path=OUT.wav,out.frequency=48000,out.channels=2,out.format=s16`。`hda-duplex` は `mixer=on`（既定）で、codec の amplifier の値を QEMU が wav に当てる（音量の段が wav の振幅に出る）。`mixer=off` の組も走らせ、software の音量（§3.2）を確かめる。
- 試験（`plan/ws100/tests/`）:
  1. A1: 画面の icon の絵（段ごと・mute）を撮る。
  2. A2・A3: QMP の pointer で click・drag・wheel、log の `ZWL VOLUME set value=` と audiod の値（`audiod-client` で get）が合う。
  3. A4: wav を `hda-wav-check.py windows` で 0.25 秒ごとの最大の振幅に分け、操作ごとに約 100 ms の音があること、振幅が音量の順に並ぶこと、wheel の連打で音が重ならない（1 つの窓の最大が単独の音を超えない）こと。
  4. A5: 設定の file の `sound.volume`、guest を再起動して session の後の audiod の値。
  5. A6: HDA の device の無い QEMU（今の既定）と、audiod を止めた guest で、icon の「音なし」と popup の文、zdesktop が落ちない（`ZWL ERROR` 0）。
- WS099 の回帰（C9）: p004 の後に `plan/ws099/tests/criteria.sh … C9`。

## 4. Phase の分け方（案）

| Phase | 目的 | 基準 | 依存・順 |
| --- | --- | --- | --- |
| p002 | audiod: `AUDIOD_FEEDBACK`（内蔵の短い音、重ならない）と software の音量（amplifier の無い device）。audiod-client の試験と QEMU の wav（`mixer=on`・`off`） | A4 の土台、A2 の土台 | — |
| p003 | libkeiland: `keiland_audio_*`（WS089 の案 + `keiland_audio_feedback`）、`KEILAND_VERSION` 15。host の試験（偽の audiod の socket） | 土台 | p002（feedback の request）。keiland.h の順は main と合わせる |
| p004 | zdesktop: icon・popup・wheel・確かめの音・設定の保存。QEMU の試験（§3.6 の 1〜5）、WS099 の C9 | A1〜A6 | p003。`seat.c` の hook は main と確認（IME） |
| p005 | Settings の Sound の頁に slider と mute（WS089 の source、main の許可） | A5 の関係の整合（基準の外。ユーザーの判断で入れるか） | p003 |
| p006 | 5330: Kei を直に起動して pci-hda の codec の列挙と pin を log で読み、鳴るかをユーザーが聞く。要るなら pci-hda の Intel 固有の設定と diagnostics。鳴らなければ A7 を Future Work へ | A7（デモに必須ではない） | p002（音を出す道具）。ユーザーの実機 |
| p007 | WS の全ての source の変更の、規約（`plan/coding-style.md` の全文）への合わせと、全体の確かめ（Awesome Plan の code を作る WS の最後の Phase） | 全体 | p002〜p006 |

## 5. 見直し（誤り・欠落・矛盾）

p001 の中で、上の設計を次の観点で見直し、直したもの・残るものを記す。

| 観点 | 見つけたこと | 扱い |
| --- | --- | --- |
| 誤り | 最初、音量の保存を audiod に置く案を考えたが、audiod は root の常駐で利用者ごとに分けられず、greeter の後に利用者が替わると混ざる | 設定の file（利用者ごと）に置く形にした（§3.5） |
| 誤り | 「DEVICE_VOLUME を送れば音量が変わる」と前提していたが、amplifier の無い codec では kernel の set が無く、audiod は ioctl の失敗を無視する（`device.c` 223 行）ので、黙って変わらない | audiod の software の音量を p002 に入れた（§3.2） |
| 欠落 | 確かめの音を zdesktop が stream で鳴らすと、compositor が共有 memory の ring の世話と時刻を持つことになり、重なりの制御も要る | audiod の内部の音にした（`AUDIOD_FEEDBACK`）。zdesktop は 1 つの request を送るだけ |
| 欠落 | 古い audiod（`FEEDBACK` を知らない）と新しい libkeiland の組 | `EINVAL` を「音なし」として黙る（§3.3） |
| 欠落 | 同時に 2 つの popup（network と音量）が開く | 1 つだけにする（§3.4） |
| 矛盾 | Settings も zdesktop も設定の file を書くと、互いの書き込みが追い越しあう恐れ | 変えた本人だけが書く。`keiland_preferences_set` は flock と読み直しで他の key を消さない（WS089 の作り）。同じ key を同時に変えた時は後の方が勝つ（許す） |
| 矛盾 | ws.md は `hda-duplex` と書き、ws035 の試験は `hda-output` を使う | どちらも出力を wav に書く。入力（capture）の試験も兼ねるので `hda-duplex` にする。amplifier の有無は `mixer=on`・`off` で両方を試す |
| 危険（残る） | 5330 の analog codec の widget はまだ誰も見ていない（§2.1） | p006 で Kei を直に起動して読む。試験機の Linux で読むには i915 の blacklist を外す必要があり、約束の範囲の外 |
| 危険（残る） | `seat.c` の wheel の hook は IME の hook と同じ file（人が作業中） | p004 の前に main と確認（§3.4） |
| 欠落（直した） | QEMU に音の device を足す口が共有の `zdesktop-guest.sh` に無い | WS100 の起動の script を別に作る（§3.6） |
| 危険（残る） | 百分率と codec の段の対応が線形だと、低い音量で急に変わる | p002 で QEMU の wav で測り、要るなら対数に合わせる |
| 基準の外 | Settings の頁（p005）は A1〜A7 に無い | ユーザーの判断で入れるかを決める（ws.md に書く） |
