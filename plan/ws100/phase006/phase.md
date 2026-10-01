<!-- awesome-plan project=zedbsd record=ws100-p006 -->

# ws100-p006: 5330 の HDA で鳴らす（L2、A7）— a: 診断の log、b: 実機で読んで聞く

Phase ID: `ws100-p006`
Parent: [WS100](../ws.md)
Status: planning → planned（2026-10-01 に phase.md を作った。手順は下）
Phase disposition: normal
Queue: なし
依存: p002（音を出す道具 `audiod-feedback`）、b はユーザーの実機の時間
基準: A7（デモに必須ではない、ユーザーの判断 2026-09-30）。L2 の数値: 5330 で確かめの音が speaker と headphone の両方から聞こえる、codec の log が 1 行以上。

## 範囲

- a（agent、QEMU）: `src/drivers/pci/pci-hda.c` に診断の log を足す。controller の PCI の id、全ての codec の vendor・subsystem・revision、使った codec の
  pin の一覧（nid・config default・pin caps・EAPD）、AFG の GPIO の数、選んだ出力の pin と volume の widget、amplifier の段（段数・1 段の dB・offset）。
  log の数は codec あたり 20 行以内。あわせて、遅れの解析（`feedback-latency.sh` 91〜127 行の python）を `plan/ws100/tests/latency-analyse.py ZDESKTOP_LOG AUDIOD_LOG [TARGET]`
  に切り出す（実機の log にも使う。`feedback-latency.sh` はその script を呼ぶ形にする）。
- b（ユーザー＋agent、実機）: 5330 で demo の image を起動し、log を SSH で読み、speaker と headphone で聞く。同じ時間に操作から音までの遅れも測る。
- 範囲の外: 鳴らない時の直し（Intel 固有の設定・codec の quirk）は p010（提案）。音量の曲線は p009。HDMI の音、DMIC、SOF。

## 手順（2026-10-01 追記）

### a: 診断の log（agent）

1. `pci-hda.c` を読む: `hda_codecs_probe`（1228 行）、`hda_codec_probe`（1262 行。vendor を 1280 行で読み、Intel の codec を飛ばす）、`hda_widgets_read`（1364 行。
   pin の caps を 1414 行）、`hda_outputs_find`（1625 行）、`hda_publish` の log（471 行）、`hda_volume_write`（1843 行付近、段の写し 1852 行）、`hda_force_snoop`（904 行）。
2. 足す log（`kern_logf`、行頭は全て `hda: `。形は例で、名前は実装で整える）:
   - attach の始め: `hda: controller %04x:%04x subsystem %04x:%04x class %06x`（PCI の config。drv_pci の読み方は同じ file の attach を見る）。
   - `hda_codecs_probe` の各 codec（Intel の codec も含めて飛ばす前に）: `hda: codec %u vendor 0x%08x subsystem 0x%08x revision 0x%08x`
     （vendor は parameter 0x00、revision は parameter 0x02、subsystem は AFG への verb 0xF20。新しい `#define` を既存の並び（94〜102 行）に足す）。
   - 使った codec の pin ごと（`hda_widgets_read` の後）: `hda: pin 0x%02x config 0x%08x caps 0x%08x eapd %s`（config default は verb 0xF1C、既存の
     `HDA_VERB_GET_CONFIG_DEFAULT` 84 行。EAPD は `HDA_PIN_CAP_EAPD` 120 行）。
   - AFG: `hda: afg 0x%02x gpio %u`（parameter 0x11 の下位 8 bit）。
   - 出力: `hda: output pin 0x%02x path …`（選んだ path の nid の並び）、`hda: volume nid 0x%02x steps %u step_db %u.%02u offset %u`（amp caps の
     numsteps・stepsize（0.25 dB 単位）・offset）。
   - 音量を書く時（`hda_volume_write`）: `hda: volume %u%% -> step %u/%u`（試験と p009 のため。音量を変えるたびに 1 行なので、変わった時だけ）。
3. build（warning 0）と style:
   ```
   mkdir -p build/ws100-p006
   make -j64 ZEDBSD_CONFIG=plan/ws100/tests/config-amd64-volume.mk BUILD=build/ws100-p006-volume build/ws100-p006-volume/vmunix > build/ws100-p006/kernel-build.log 2>&1; echo "make exit=$?"
   python3 plan/tools/style-check.py src/drivers/pci/pci-hda.c; git diff --check
   ```
   （`$(BUILD)/vmunix` の規則は `platform/amd64/vmunix.mk:360`。style は新しい違反 0）
4. image（[guide.md](../guide.md) §5.1 の 1・2・3。client を作ってから volume と audiod の image）。
5. QEMU で log の形を確かめる（SSH の `dmesg`。console・serial の log は読まない）:
   ```
   export GUEST_RUNTIME=$PWD/build/ws100-p006-run
   VOLUME_AUDIO=duplex sh plan/ws100/tests/volume-guest.sh start build/ws100-p006-volume/hdd-image.img
   sh plan/ws100/tests/volume-guest.sh wait --timeout 240
   sh plan/ws100/tests/volume-guest.sh run 'dmesg | grep "hda:"' | tee build/ws100-p006/qemu-hda.txt
   sh plan/ws100/tests/volume-guest.sh run 'audiod-feedback volume 50 feedback get; dmesg | grep "hda: volume" | tail -2'
   sh plan/ws100/tests/volume-guest.sh stop
   unset GUEST_RUNTIME
   ```
   QEMU の `hda-duplex` の codec は vendor の上位が 0x1af4（Red Hat/QEMU）。pin・output・volume の行が出ること。
6. `latency-analyse.py` を切り出し、`feedback-latency.sh` から呼ぶ。同じ入力で前と同じ `RESULT` の行が出ることを、p008 の log（`build/ws100-p008/` に残っていれば）
   か新しい run で確かめる。
7. 回帰（guide.md §5.3・§5.4 の pci-hda の行）: `audiod-qemu.sh`（`RUN=$PWD/build/ws100-p006-audiod-run IMG=build/ws100-p006-audiod/hdd-image.img … build/ws100-p006/audiod`）、
   `volume-p004.sh build/ws100-p006-volume/hdd-image.img build/ws100-p006/volume-p004`、`feedback-latency.sh …`、boot test（`OUTPUT=build/ws100-p006/boot`）。

### b: 5330（ユーザー＋agent）

素の 5330（USB から単独の起動。passthrough ではない）の手順は [plan/tools/hw5330/README.md](../../tools/hw5330/README.md) §2（流れ・ユーザーに頼む確かめ・返るもの）と §3（demo の image）。
SSH の関数（以下の手順で使う）:

```
IP=<ユーザーから聞いた素の 5330 の Kei の IP（DHCP。前例 10.0.30.5）>
ssh5330() { ssh -i plan/tmp/guest/id_ed25519 -o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null root@"$IP" "$@"; }
scp5330() { scp -i plan/tmp/guest/id_ed25519 -o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null "$1" root@"$IP":"$2"; }
```

（[README](../../tools/hw5330/README.md) §2.3 の形。README では「未確認」の command。centris から届かないことがある。**入ってよいかをユーザーに確かめてから**入る。
root の password は `root`、kei は `kei`。）

1. image（agent）: a の kernel を含む demo の image。`sh plan/ws075/demo/build-demo-image.sh build/ws100-p006-demo > build/ws100-p006/demo-build.log 2>&1; echo "exit=$?"`、
   `ls build/ws100-p006-demo/kern64/src/drivers/pci/pci-hda.o`。
2. ユーザー: USB に書いて 5330 を起動（kei の autologin）。内蔵 speaker の音量の物理の key があれば上げておく。
3. agent: `ssh5330 'dmesg | grep "hda:"' > build/ws100-p006/hw-hda.txt`。controller の id が `8086:51c8`、codec の vendor（Realtek なら上位 0x10ec）、
   speaker と headphone の pin（config default の device の欄: 1 = speaker、2 = HP out）、EAPD、GPIO、選んだ出力を記録する。
   `hda: … not used` か attach の失敗の行があれば、その error を記録して c へ。
4. 音（agent が送り、ユーザーが聞く）:
   ```
   scp5330 build/ws100-tests/audiod-feedback /usr/bin/audiod-feedback
   ssh5330 'chmod 755 /usr/bin/audiod-feedback; audiod-feedback get volume 50 feedback sleep 800 feedback sleep 800 volume 100 feedback'
   ```
   （`get` の行 `FEEDBACKTEST welcome device=1` で audiod が device を持つことを確かめる。`device=0` なら audiod が HDA を見つけていない）
   - ユーザー: 内蔵 speaker で 3 回の短い音が聞こえるか（2 回目より 3 回目が大きいか）。
   - ユーザー: headphone を差して同じ command。聞こえるか。
   - ユーザー: system bar の音量の icon を click し、slider と wheel で変えて音を聞く（S12）。
5. 遅れ（L3、聞こえた時だけ）:
   ```
   ssh5330 'service stop audiod; sleep 1; rm -f /tmp/audiod-timing.log; AUDIOD_TIMING_LOG=/tmp/audiod-timing.log /sbin/audiod > /tmp/audiod.out 2>&1 </dev/null & sleep 2; echo started'
   ```
   ユーザーが icon の上で wheel を 10 notch（1 秒あけて上下交互）回す。その後:
   ```
   ssh5330 'grep -E "ZWL VOLUME (set|feedback)" /run/user/1000/session.log' > build/ws100-p006/hw-zdesktop.log
   ssh5330 'cat /tmp/audiod-timing.log' > build/ws100-p006/hw-audiod.log
   python3 plan/ws100/tests/latency-analyse.py build/ws100-p006/hw-zdesktop.log build/ws100-p006/hw-audiod.log 50 | tee build/ws100-p006/hw-latency.txt
   ```
   （`AUDIOD_TIMING_LOG` と再起動の仕方は `feedback-latency.sh:51` と同じ。session の log の path は `userland/desktop/sessiond/session.c:589`、kei の uid は `ls /run/user` で確かめる）
6. 記録: この phase.md に、image、`hda:` の行の要約、聞こえたか（speaker・headphone・system bar）、遅れの `RESULT` の行、**実機の証拠として**書く（QEMU の a の証拠と分ける）。

## 完了の条件

- a: QEMU で `hda:` の診断の行（codec・pin・output・volume）が出る。`audiod-qemu: PASS`、`volume-p004: PASS`、`feedback-latency.sh` の `RESULT` が前と同じ程度、
  `boot-test: PASS`（PNG をユーザーに見せる）。build の warning 0、style の新しい違反 0。
- b: 5330 で speaker と headphone の両方から確かめの音が聞こえる（ユーザーの言葉で記録）。`hda:` の行が 1 行以上。遅れの中央値を記録（≤ 50 ms なら L3 の遅れも満たす）。
- b で鳴らない時は uncleared にし、`hda:` の行と試したことを記録して main に報告する（p010 で直すか、A7 を Future Work に回すかは main とユーザー）。
