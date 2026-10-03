<!-- awesome-plan project=zedbsd record=ws100-guide -->

# WS100 作業の手引き（2026-10-01）

この file だけで WS100（system bar の音量）を続けられるように、ゴール・今の状態・次の作業・未知・command をまとめた。
記録の正は [ws.md](ws.md)・[design.md](design.md)・各 phase.md（食い違えば そちらが正）。
2026-10-01 にこの手引きを書いた agent が確かめたこと: `host-audio.sh` を main の checkout で流して `host-audio: 14/14 passed`、
`audiod-feedback.c` が共有の sysroot（`build/amd64/sysroot`）で compile・link できること、demo の image の build（`build/demo-lcd7`・`demo-lcd8`）に
`pci-hda.o` があること、script の行番号。QEMU・実機・image の build は流していない（未確認）。

## 1. ゴール

### 1.1 デモの場面（fg010、[master.md](../master.md)）

| 場面 | 内容 |
| --- | --- |
| S12 | system bar の音量の icon で音量を変え、確かめの音が鳴る |

ユーザーの判断（2026-09-30）: **5330 で HDA が鳴らなくても、デモで鳴らさないことはクリティカルではない**（A7 はデモに必須ではない）。
鳴らなければ A1〜A6（icon・slider・設定・無音の扱い）だけでデモに出す。

### 1.2 達成基準（ws.md、2026-09-30 朝ユーザー「案のまま確定」）

| # | 基準 | 状態 |
| --- | --- | --- |
| A1 | 右上の通知領域の音量の icon が今の音量（無音・小・中・大）を絵で示す | 済み（p004、QEMU） |
| A2 | click（tap）で slider の popup、drag で 0〜100%、mute、外で閉じる | 済み（p004） |
| A3 | icon の上の wheel で 5% ずつ | 済み（p004） |
| A4 | 変えるたびに約 100 ms の確かめの音、連続で重ならない | 済み（p002・p004、QEMU の wav） |
| A5 | 再起動の後も音量を保つ | 済み（p004、`sound.volume`・`sound.muted`） |
| A6 | audiod や device が無いと「音なし」の印、落ちない | 済み（p004） |
| A7 | 5330 の内蔵 speaker と headphone の端子から音が出る（HDA 8086:51c8） | **未実施**（p006、ユーザーの耳。デモに必須ではない） |
| 基準の外 | Settings の Sound の頁で音量（ユーザー「入れる」） | 済み（p005） |

段の数値目標（ws.md「段の計画」、ユーザー「案のまま」）:

| 段 | 目標 | 状態 |
| --- | --- | --- |
| L1 | A1〜A6 と Sound の頁、同期 3 秒以内 | 済み |
| L2 | 5330 で確かめの音が speaker と headphone の両方から聞こえる。codec の log が 1 行以上 | 未実施（p006） |
| L3 | 操作から音の始まりまで 50 ms 以内。実機の音量の曲線 25・50・75・100% が -30・-15・-7・0 dB ± 3 dB | 遅れ: QEMU の guest の中 31〜37 ms（p008、合否は実機）。曲線: 未着手（p009） |

**完了の定義**: A1〜A6 は済み。A7（L2）は鳴れば満たし、鳴らなければユーザーの判断どおり Future Work に回す（受け入れから外すことを main が記録する）。
L3 の残り（実機の遅れ、曲線）と、p007 の後に変えた source（p008 の計測の log、p006・p009 で変える pci-hda・audiod）の規約の確かめ（下の p011）が cleared になれば完了。
完了の後、ws.md を完了の形に書き直し、後も使う試験（`volume-*.sh`・`audiod-*`・`host-audio.*`・`feedback-latency.sh`・`wav-latency.py`）を `plan/tools/audio/` へ移して
master の Tools 節に登録する（main）。

## 2. 今の状態

### 2.1 済んだもの（証拠）

| Phase | 結果 | 証拠 |
| --- | --- | --- |
| p001 | 設計と 5330 の HDA の調べ（Linux も legacy の `snd_hda_intel` を選ぶ、DMIC なし、codec は未確認） | [design.md](design.md) §2、[phase001](phase001/phase.md) |
| p002 | audiod の `AUDIOD_FEEDBACK` と software の音量 | [phase002](phase002/phase.md)、`audiod-qemu.sh` PASS |
| p003 | libkeiland の `keiland_audio_*`（KEILAND_VERSION 15） | [phase003](phase003/phase.md)、`host-audio.sh` 14/14（2026-10-01 にも main の checkout で PASS） |
| p004 | zdesktop の icon・popup・wheel・音・保存（A1〜A6）、WS099 の C7・C8・C9 | [phase004](phase004/phase.md)、`volume-p004.sh` PASS |
| p005 | Settings の Sound の頁 | [phase005](phase005/phase.md)、`volume-p005.sh` PASS、WS089 の `settings-regress.sh` PASS |
| p007 | 規約の合わせ（変えた関数）と全体の確かめ | [phase007](phase007/phase.md) |
| p008 | 確かめの音の遅れの計測（guest の中 31〜37 ms、host の WAV 106〜111 ms は参考） | [phase008](phase008/phase.md)。ws.md は「cleared（Q1 の判断）」、phase.md の Status 行は「uncleared」のまま（記録の食い違い。直すのは main） |

### 2.2 開いているもの

| 項目 | 状態 |
| --- | --- |
| p006（A7、L2） | planning → 2026-10-01 に [phase006](phase006/phase.md) を作った（手順あり） |
| p009（L3 の音量の曲線） | 2026-10-01 に [phase009](phase009/phase.md) を作った。**今の曲線は目標に合わない**（§4 の 1 行目） |
| 実機の遅れ（L3） | p008 の Q1 の判断で「合否は実機」。p006 の実機の時間の中で測る（phase006 の手順 b-5） |
| kernel の fragment（4096 byte = 21.3 ms）を小さくする直し | 入れない（Q1）。再検討は実機で 50 ms を超えたとき |

### 2.3 既知の bug

WS100 に開いた bug は無い（2026-10-01 の [known-bugs.md](../known-bugs.md)）。関係するもの:

| 項目 | 内容 |
| --- | --- |
| F-063（[future-work.md](../future-work.md)） | `plan/ws035/tests/boot-hda.sh` は host の全ての QEMU を kill する。**使わない**（WS100 の試験は `volume-guest.sh`・`audiod-qemu.sh` の自分の run だけを止める） |
| `config-demo-hdmi.mk` の古い注釈 | 「the kernel here has no HDA driver yet」とあるが、`Makefile:205` の `CONFIG_DRIVER_PCI_HDA ?= $(if $(filter amd64,…),y,n)` で amd64 は既定で入り、`build/demo-lcd7`・`demo-lcd8` に `kern64/src/drivers/pci/pci-hda.o` がある（2026-10-01 確かめ）。demo の image はそのまま A7 に使える。注釈の修正は WS075 の file なので main に頼む |

## 3. 次の作業の順番

master の優先: WS099・WS079・WS090・WS089・WS094・**WS100**・WS078・WS102（2026-09-30 夜ユーザー）。WS104・WS105（Linux への移植）が master の 1 番。
2026-10-10 ごろからは bug と実機の調整だけ。

| 順 | Phase | 目的 | 実行 | 受け入れ |
| --- | --- | --- | --- | --- |
| 1 | [ws100-p006](phase006/phase.md) a | pci-hda の codec・pin・EAPD・GPIO の診断の log（QEMU で形を確かめる）と、log の解析の script | agent（phase-runner。kernel の driver） | QEMU で `dmesg` に codec の行、`audiod-qemu.sh`・`volume-p004.sh` PASS、boot test |
| 2 | [ws100-p006](phase006/phase.md) b | 5330 で直に起動し、codec の log を読み、speaker と headphone で聞く（A7）。同じ時間に遅れも測る | ユーザー（耳・操作）＋ agent（SSH） | 両方から聞こえる。codec の行が 1 行以上。遅れの中央値の記録 |
| 3 | [ws100-p009](phase009/phase.md) | 音量の曲線を目標（25・50・75・100% → -30・-15・-7・0 dB ± 3 dB）に合わせる（audiod の software と pci-hda の codec の段の両方） | agent（host と QEMU）、実機の確かめは p006 b の後 | host 試験の表、QEMU の wav の dB、実機の codec の段の読み戻し |
| 4 | ws100-p010（提案、鳴らないときだけ） | Intel 固有の設定（TCSEL・EM2）か codec の quirk（EAPD・GPIO・COEF） | agent ＋ 実機 | 5330 で鳴る。だめなら A7 を Future Work へ（ユーザーの判断どおり） |
| 5 | ws100-p011（提案） | 最終の規約と回帰: p007 の後に変えた source（p008 の audiod・volume.c の計測の log、p006・p009・p010 の pci-hda・audiod）を全文の規約で見直し、全ての試験 | agent | style-check 0（既知の例外を除く）、host・QEMU の全て PASS、boot test |

提案の Phase の詳細:

| ID（提案） | 目的 | 触る file | 受け入れ |
| --- | --- | --- | --- |
| ws100-p010 | p006 b で鳴らなかった時の直し。Linux の `snd_hda_intel` の Skylake 以降の設定（`AZX_DCAPS_INTEL_SKYLAKE` の TCSEL・snoop、EM2）と pci-hda の比較（今ある Intel 固有は `hda_force_snoop`、`src/drivers/pci/pci-hda.c:904` だけ）、Realtek の codec の Dell `1028:0b02` の quirk（GPIO・COEF・EAPD） | `src/drivers/pci/pci-hda.c` | 5330 で speaker・headphone が鳴る。QEMU の回帰 PASS |
| ws100-p011 | 最終の規約と回帰（Awesome Plan §6: 最後の変更の後に確かめ直す） | WS100 が変えた全て | phase007 と同じ確かめ + p006・p009 の試験 |

## 4. 未知と調べ方

| 未知 | なぜ重要 | 調べ方 |
| --- | --- | --- |
| **音量の曲線が目標に合わない** | L3 の目標は 25・50・75・100% → -30・-15・-7・0 dB。今の audiod の software の音量は 1% あたり 0.6 dB（`userland/base/audiod/mix.c:31` の注釈、`soft_gain` 613 行）で 25% = -45 dB・50% = -30 dB・75% = -15 dB。pci-hda は百分率を段に線形に写す（`src/drivers/pci/pci-hda.c:1852` `left = volume->left * controller->volume_steps / 100U`）ので、codec の段が dB で等間隔なら同じく dB で線形（例: 0.75 dB × 87 段なら 25% = -49 dB） | p009。目標は振幅 = (p/100)^2.5（dB = 50·log10(p/100): 25% = -30.1、50% = -15.1、75% = -6.2、100% = 0）でほぼ合う（± 3 dB の中）。host の試験で表を出し、QEMU の wav の峰で dB を測る（`audiod-qemu.sh … soft hardware`） |
| 5330 の analog codec の speaker・headphone の pin | A7 の可否。誰もまだ見ていない（design §2.1） | p006 a の診断の log を p006 b で SSH の `dmesg` で読む。今の log は `hda: codec %u, %u output pin(s), input …, volume …`（`pci-hda.c:471`）と `hda: codec %u not used: %d`（同 1248）だけ |
| 内蔵 speaker の amplifier の電源（EAPD・GPIO） | 鳴らない時の第一の容疑（design §2.2 の 4） | p006 a で pin の EAPD の能力と値、AFG の GPIO の数を log に出す。鳴らなければ p010 |
| display の電源（HDMI の codec） | Linux は i915 の audio component を待つ。Kei は Intel の codec を飛ばすので要らない見込み（design §2.2 の 1） | p006 b の `hda:` の行で analog codec が列挙されるか（STATESTS） |
| 実機の操作から音までの遅れ | L3 の合否 | p006 b-5。QEMU と同じ guest の log（`ZWL VOLUME set … final=1 at_ms=`・`ZWL VOLUME feedback at_ms=` と audiod の `AUDIOD_TIMING_LOG`）を SSH で取り、p006 a で切り出す解析の script に通す |
| `seat.c` の wheel の hook と IME | `seat.c` は IME の hook を持つ file で、IME は人間が作業中 | WS100 は hook を 1 塊だけ足した（p004、main の許可）。今後 `seat.c` を変えない |

判定に QEMU の console・serial の log を使わない（AGENTS.md）。guest の操作は SSH（`guest.py run`）、`audiod-qemu.sh` は `plan/tools/guest/serial.py` で
シリアルと対話して command を走らせる（log の読み取りでの判定ではない。判定は WAV と command の出力）。

## 5. コマンド（repo の root から。`<W>` は作業の名前、例 `ws100-p006`）

共通の決まりは [plan/ws104/commands.md](../ws104/commands.md) §0・§1・§4、Settings と音は §8。**§8 の volume の手順には注意が 1 つある**（下の 5.1 の 1）。

### 5.1 image の build

1. 試験の client `audiod-feedback` は `build-volume-image.sh`・`build-audiod-image.sh` が中で `build-audiod-feedback.sh BUILD` を呼んで
   `BUILD/tests/audiod-feedback` に作る（ws136-p001、2026-10-04: 以前の共有の `build/ws100-tests/` はやめた。config は `$(BUILD)/tests/audiod-feedback` を読む）。
   headers は共有の `build/amd64/sysroot`、libc.so は BUILD の `dynamic/`（無ければ先に作る）。手で作るなら `sh plan/ws100/tests/build-audiod-feedback.sh build/<W>-volume`。

2. volume の image（WS099 の基準の image + HDA・audiod・audiod-feedback、kei の autologin）:

```
sh plan/ws100/tests/build-volume-image.sh build/<W>-volume > build/<W>/volume-build.log 2>&1; echo "exit=$?"
grep -E ':[0-9]+:[0-9]+: warning:' build/<W>/volume-build.log | grep -vE '/packages/|^\.\./src/|userland/base/noct/noct/' | wc -l
ls -la build/<W>-volume/hdd-image.img
```

   - PASS: `exit=0`、数が `0`、image がある。**引数を省くと build/amd64**（`build-volume-image.sh` の `build=${1:-build/amd64}`）。

3. audiod の image（`audiod-qemu.sh` 用。userland の試験の image + HDA・audiod・serial の mirror）。`build-audiod-image.sh` は上の sysroot の理由で別の BUILD では
   client の段で止まるので、1 の client を作ってから make を直に流す:

```
make -j64 ZEDBSD_CONFIG=plan/ws100/tests/config-amd64-audiod.mk BUILD=build/<W>-audiod disk-image > build/<W>/audiod-build.log 2>&1; echo "make exit=$?"
```

4. 5330 の demo の image（A7）: WS075 の script。HDA は既定で入る（§2.3）。

```
sh plan/ws075/demo/build-demo-image.sh build/<W>-demo > build/<W>/demo-build.log 2>&1; echo "exit=$?"
ls build/<W>-demo/kern64/src/drivers/pci/pci-hda.o build/<W>-demo/hdd-image.img
```

   - demo の image に `audiod-feedback` は入らない。実機へは scp で入れる（phase006 b）。
   - image の build は同時に 1 つ（commands.md §0）。

### 5.2 host 試験

```
sh plan/ws100/tests/host-audio.sh; echo "exit=$?"
```

- PASS: `host-audio: 14/14 passed`、exit 0（3 秒、2026-10-01 確かめ）。`build/ws100-host/` に作る。
- WS104 の p003 が `libkeiland/audio.c` を `libkeiland/zedbsd/audio-zedbsd.c` へ移すと、`host-audio.sh:13` の path を main が直す（`plan/ws104/phase003/phase.md`）。
  WS104 の p001 で `include/libc/keiland.h` が `userland/desktop/keiland/` へ移ると `host-audio.sh:10` の `ln -sf …/include/libc/keiland.h` も変わる（WS104 の担当）。

### 5.3 QEMU の guest 試験

host の準備（host の起動ごとに 1 回）: `sudo modprobe vgem && sudo chmod 0666 /dev/dri/renderD128`。

```
sh plan/ws100/tests/volume-p004.sh build/<W>-volume/hdd-image.img build/<W>/volume-p004
sh plan/ws100/tests/volume-p005.sh build/<W>-volume/hdd-image.img build/<W>/volume-p005
sh plan/ws100/tests/feedback-latency.sh build/<W>-volume/hdd-image.img build/<W>/latency
RUN=$PWD/build/<W>-audiod-run IMG=build/<W>-audiod/hdd-image.img sh plan/ws100/tests/audiod-qemu.sh build/<W>/audiod
```

| script | guest | runtime の directory | PASS の印 | 時間 |
| --- | --- | --- | --- | --- |
| `volume-p004.sh IMAGE [OUT]` | 自分で起こし、止める（A6 の分で HDA 無しでもう 1 回） | **`build/ws100-run` に固定**（`volume-p004.sh:22`。GUEST_RUNTIME を上書きする） | 最後の行 `volume-p004: PASS`（207 行） | 6〜7 分（commands.md §8） |
| `volume-p005.sh IMAGE [OUT]` | 同じ | **`build/ws100-run` に固定**（`volume-p005.sh:19`） | `volume-p005: PASS`（203 行） | 6〜7 分 |
| `feedback-latency.sh IMAGE [OUT]` | 同じ | **`build/ws100-run` に固定**（`feedback-latency.sh:18`） | 判定の行 `RESULT changes=N total_ms=M (target 50) … within`（`OVER` は超過）。QEMU の値は参考（Q1） | 未計測（数分） |
| `audiod-qemu.sh [OUT] [CASE…]` | case ごとに自分の QEMU を起こす（serial で操作、WAV を検査） | `RUN`（既定 `build/ws100-run`、`audiod-qemu.sh:22`）で変えられる | `audiod-qemu: PASS`（165 行） | 未計測 |

- 上の 3 本（volume-p004・p005・feedback-latency）は同じ runtime を使うので**同時に 1 つ**。audiod-qemu も既定では同じ所なので `RUN` を変える。
- guest を手で起こして見る時: `VOLUME_AUDIO=duplex GUEST_RUNTIME=$PWD/build/<W>-run sh plan/ws100/tests/volume-guest.sh start IMAGE` → `… volume-guest.sh wait --timeout 240` →
  `… volume-guest.sh run 'dmesg | grep hda:'` → `… volume-guest.sh stop`（`volume-guest.sh` は start 以外を `guest.py` に渡す、33〜35 行）。
  `VOLUME_AUDIO=duplex-off` は codec の amplifier を QEMU が wav に当てない組（software の音量の確かめ）、`none` は HDA 無し。WAV は `$GUEST_RUNTIME/out.wav`（guest を止めた後に完成）。

### 5.4 回帰の組

| 変えたもの | 流すもの |
| --- | --- |
| audiod（`userland/base/audiod/`） | host-audio、audiod-qemu（全 case）、volume-p004、volume-p005、boot test |
| pci-hda（`src/drivers/pci/pci-hda.c`） | audiod-qemu（feedback・soft・hardware・no-device）、volume-p004、boot test（音の無い既定の image も起動すること） |
| zdesktop の volume.c・shell.c | volume-p004、volume-p005、WS099 の C7・C8・C9（commands.md §5 の command で `C7 C8 C9`） |
| Settings の sound.c | volume-p005、WS089 の settings-regress（commands.md §8） |
| libkeiland の audio.c | host-audio、volume-p004、volume-p005 |

boot test:

```
OUTPUT=build/<W>/boot plan/tools/boot-test.sh build/<W>-volume/hdd-image.img; echo "exit=$?"
```

- PASS: `exit=0`、`boot-test: PASS build/<W>/boot/login.png`。PNG をユーザーに見せる。

## 6. 実機（Dell Latitude 5330）

一般の手順は [plan/tools/hw5330/README.md](../tools/hw5330/README.md)（2026-10-01）。**A7 は素の 5330（USB から単独の起動、README §2・§3）でしか確かめられない**:
passthrough（README §1）は iGPU（`0000:00:02.0`）だけを QEMU に渡し、HDA（`00:1f.3`）は渡さない。
**A7 はユーザーの実機の確かめ**（ユーザーの耳）。手順は [phase006](phase006/phase.md) の b。要点:

| # | 確かめ | 誰 | 判定 |
| --- | --- | --- | --- |
| H1 | `dmesg` の `hda:` の行（controller の attach、codec の vendor・pin の一覧、選んだ出力の pin） | agent（SSH） | analog codec が 1 つ以上使われる（`hda: codec N, M output pin(s)…`） |
| H2 | `audiod-feedback volume 50 feedback` で内蔵 speaker から音 | ユーザーの耳 | 聞こえる |
| H3 | headphone を差して同じ | ユーザーの耳 | 聞こえる（speaker は消えるのが普通。消えなくても A7 は満たす） |
| H4 | system bar の icon の click・wheel で音量が変わり、確かめの音が鳴る（S12） | ユーザー | 聞こえて、大きさが変わる |
| H5 | 操作から音の始まりまで（L3） | agent（SSH の log） | 中央値 ≤ 50 ms |
| H6 | 音量の曲線（L3、p009 の後） | agent（codec の段の読み戻し）＋ユーザー | 25・50・75・100% が -30・-15・-7・0 dB ± 3 dB（計算） |

鳴らなくても A1〜A6 はデモに出せる（ユーザーの判断）。その時は H1 の log を残して main に報告し、p010 か Future Work を main が決める。

## 7. 注意

- 判定に QEMU の console・serial の log を使わない。push しない。commit は `git commit -m WIP -- <自分の path>`（AGENTS.md）。
- `plan/ws035/tests/boot-hda.sh` を使わない（host の全ての QEMU を kill する、F-063）。
- runtime の衝突: `build/ws100-run` は volume-p004・volume-p005・feedback-latency の固定、audiod-qemu の既定。`build/ws035-sq-run` は criteria.sh の固定（C7〜C9）。
  これらを並べない。他の agent が同じ runtime を使っていないか、`ls build/ws100-run/qemu.pid` と `ps` で確かめてから流す。
- `seat.c`（IME の hook がある）と `userland/desktop/ime/` は触らない（WS095 は人間が作業中）。
- toolchain（`build/llvm` ほか）を変えない。`audiod-feedback` の compile は `build/llvm/bin/clang` を使うだけで、toolchain の tree に書かない。
- `pci-hda.c` は kernel の driver で HAL の API ではないので、事前承認は要らない（AGENTS.md「禁止と承認」）。`include/hal/hal.h` は変えない。
- **WS104 との関係**（WS104 は planned、master の 1 番）:
  - WS104 p001 は `include/libc/keiland.h` などを `userland/desktop/keiland/` へ移す。WS100 の source が `<keiland.h>` を読む所は変わらないが、`host-audio.sh:10` の
    symlink の元の path が変わる（WS104 の Phase が直す）。
  - WS104 p002 は `keiland_audio_available()`（audiod の socket があるか）を `libkeiland/audio.c` に足し、Settings の `se_look_sound()` を置き換え、
    KEILAND_VERSION を 21 にする。WS100 の後の Phase で `keiland_audio_*` を足すときは、WS104 の p002 の後の版から続け（version を 1 つ上げる）、`exports.map` の順を合わせる。
  - WS104 p003 は `libkeiland/audio.c` を `libkeiland/zedbsd/audio-zedbsd.c` へ移す（`host-audio.sh:13` は main が直す）。WS100 の Phase が同じ時期に audio.c を
    変えると衝突するので、**WS104 の p001〜p003 の間は libkeiland の audio を変えない**（main が順を決める）。p006・p009 は pci-hda と audiod で、libkeiland は触らない予定。
  - WS104 p008 の回帰（commands.md §8）は WS100 の試験を使う。§5.1 の 1 の client を作らないと volume-p004・p005 が失敗する点を main に知らせる。
- WS075 の demo の config（`plan/ws075/demo/config-demo-hdmi.mk`）は WS075 の file。HDA の注釈の修正や config の変更は main に頼む。
- ユーザーの判断: A7 はデモに必須ではない（2026-09-30）。L3 の数値は「案のまま」（2026-09-30 朝）。Settings の Sound の頁は「入れる」（済み）。
