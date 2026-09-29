<!-- awesome-plan project=zedbsd record=ws075p025 -->

# ws075-p025: BUG-117 の原因と修正（窓の多いとき draw が拒まれ compositor の描画が止まる）

Phase ID: `ws075-p025`
Parent: [WS075](../ws.md)
Bug: [BUG-117](../../bugs/BUG-117.md)
Status: cleared（2026-09-30。原因は executor の object 表を複数の session が lock 無しに同時に変えていたこと。表と allocation の list に mutex。実機の passthrough の stress 100 回で set の消失が修正前 2 件 → 修正後 0 件、vkx・vke1・vke2・vkc PASS、boot test PASS）
Phase disposition: normal
承認: 2026-09-30 main の指示（最優先、p024 の後）。compositor（`userland/desktop/wayland/`）に原因があれば直してよい（main の許可、IME の file は除く、変更は小さく、区切りごとに main を取り込む）。

## 進め方（main の指示）

1. 再現の率を上げる方法（窓の開閉の繰り返し、descriptor の更新を速める等）を見つける。
2. executor の descriptor の追跡か、compositor の descriptor の更新・view の破棄の順序か、原因を切り分ける。
3. 直し、再現の方法で起きないことを確かめる。

## 分かっていること（コードから、2026-09-30）

- 拒む所（`render/state.c`）の条件は「set が bind されていない、または slot の view か sampler が NULL」の 3 つで、以前の文言は区別しない。
- libvulkan（`userland/desktop/libvulkan`）は create・destroy・vkUpdateDescriptorSets・vkAllocateDescriptorSets を同期で送り（返事を待つ）、
  wire id は増えるだけ（再利用しない）。command buffer の記録は client の側に溜め、vkEndCommandBuffer でまとめて送る（bind の set の
  lookup は End の時、slot の中身は submit の時に読む）。
- executor の set・view・sampler は calloc で作られ、slot は vkUpdateDescriptorSets でだけ書かれる。update の時に set・view・sampler の
  identity が表に無ければ、その書き込みは黙って捨てられるか NULL が入る。
- compositor（`compose.c`）は set を 1 つずつ解放せず spare の表に戻して次の image に使い回す。import・shm の image の解放
  （`import_release`・`image_release`）は set を spare に戻し、view を壊し、構造体を 0 で埋める。draw は `import->set` を bind する。
- 起きた 2 回とも、その直後に kei の session が終わり（status 256）、greeter が 3 回失敗して console に落ちた。compositor の出力
  （/run/user/1000/session.log）は集めていなかった。

## 診断（executor、常設）

どれが欠けたかを分ける log を足した（最初の 16 件、BUG-117）:

- `render/state.c`: 拒む行に「no set bound / no view / no sampler」。
- `render/command.c`: vkCmdBindDescriptorSets の set の identity が session の set でない時。
- `render/descriptor.c`: vkUpdateDescriptorSets の set が session の set でない時、image の descriptor の sampler・view が見つからない時。

harness（`hdmi-h4-hw.sh stop`）は kei の session の log（/run/user/1000/session.log）も集める。

## 再現と切り分け（2026-09-30、実機の passthrough、診断の image `build/ws075-p025/diag.img`）

- 窓の角の drag（resize）: X terminal は大きさが変わらず、Model viewer は窓が消えた（原因は見ていない、BUG-117 と別）。使わない。
- 10 app の上にさらに App Home の 10 app を 2 回開く（計 30 窓）: `gt memory: object pool exhausted (128 slots)` が続き、ある app の
  vkUpdateDescriptorSets と bind が「0x6a1 は session の descriptor set でない」で拒まれた。compositor は止まらない。GPU の object の
  128 の枠が尽きて作れない物が出るのは別の制限（元の 2 回の log には exhausted が無い）。
- stress C（`plan/ws075/tests/hdmi/stress-117.sh`: 10 app の上で Model viewer を App Home から開き q で閉じる、を繰り返し、毎回 flip の数で
  描画が生きているか見る）: 40 回のうち 1 回、exhausted 無しに、ある app の set 0x1c1 が alloc の後に表から消えていた
  （update と bind が「session の set でない」）。その瞬間の log は別の session の pipeline の compile（表への追加）と入り混じっていた。

## 原因

executor の object 表（`render/object.c`、Vulkan の object の identity → object）は device に 1 つで、全ての session の object を持つ。
各 process の command stream は GPU の core から lock 無しに driver の command を呼び、i915 も executor の stream は device の mutex の
外で実行する（`command.c` の i915_command・i915_command_submit）。SMP（passthrough の VM は複数の vCPU）で 2 つの session が同時に
表を変えると:

- 2 つの insert が同じ `entries[count]` に書き `count++` が 1 回分失われる、
- insert の配列の拡張（新しい配列へ copy して古い配列を free）と別の session の lookup・remove が重なる、
- remove・forget（最後の entry を空いた所へ移す）と insert が重なる、

のどれでも、ある session の object（set・view・sampler など）が表から消える。compositor の set・view・sampler が消えると、その後の
vkUpdateDescriptorSets は黙って捨てられるか slot に NULL が入り、draw が「set 0 binding 0 has no image view and sampler」で拒まれ、
submit の失敗で compositor が終わる（BUG-117）。app の object が消えると、その app の command が拒まれる（stress C で見たもの）。
新しい client が開く時（多くの object を作る）に他の session と重なりやすく、元の 2 回（Model viewer・X terminal を開いた直後）と合う。

同じ前提（「呼び手が同時に走らない」）の共有の状態がもう 1 つあった: 全ての VkDeviceMemory の list（`render/memory.c` の
`i915_gfx_memories`、blob の attach・detach が探す）。コメントに「callers not running an executor command and a blob attach or detach
at once」とあるが、別の session の stream や blob の作成・破棄とは同時に走る。

## 直し

- `render/object.c`: 表に mutex（`LOCK_RANK_DEVICE`、他の lock を取らない葉）を持たせ、insert・lookup・remove・take・forget を
  その下で行う（本体は `i915_object_*_unlocked` に分け、公開の関数が lock を取る）。take の match（pool の set を選ぶ）は object を
  見るだけ。
- `render/memory.c`・`internal.h`・`vulkan.c`: allocation の list を executor の device（`vk->memories`）に移し、その mutex
  （`vk->memories_lock`）の下で追加・削除・attach・detach を行う（XXX「device と session に共有」も解消）。
- host の stand-in（`i915-vk-render-stubs.inc`・`i915-vk-cmd-test.c`）: mutex_init を足し、mutex_lock・unlock は持っている mutex を
  覚え、同じ mutex を 2 回取る（kernel では自分との deadlock）・持っていない mutex を返すと fixture を落とす。

表の scan は線形で全 session の entry を見る（20 app で数千）。lock の下の scan の長さは性能の候補（session ごとの表）として残す。

## 検証（2026-09-30）

| 確認 | 結果 |
| --- | --- |
| build（`build-demo-image.sh build/ws075-p008/pt passthrough ...`） | 成功、warning 0 |
| `sh plan/ws031/tests/run-vk-host-tests.sh`（全 10 個、mutex の二重取得・未保持の解放を検査する stand-in） | PASS |
| `plan/tools/style-check.py`（変えた render の file、前との差分） | 新しい指摘は critical section の形（規約 §5 の lock と unlock の段落）だけ |
| 実機の passthrough: stress C 100 回（`stress-117.sh 100`、10 app の上）修正の無い版（`diag.img`、診断の log だけ） | 描画は止まらなかったが、set の消失 2 件（0x1f: update・bind が「session の set でない」→ `draw refused: set 0 binding 0 has no image view and sampler (no set bound)` → command buffer の停止、BUG-117 と同じ形。0x25c3: update・bind の拒否） |
| 同 修正の版（`fix.img`、ef0f3e94） | set の消失 0 件、描画は 100 回とも続く（3 s に 27 flip 以上） |
| `test-hw.sh`（修正の code） | vkx 9/9・vke1 6/6・vke2 17/17・vkc 9/9 PASS |
| measure-apps 2 回（修正の版） | compositor の 1 run 6.68・6.66 ms（p024 の sel 6.39〜6.69 ms と同じ）、C6 中央値 113.9・125.0 ms（2 run、判定ではない）、draw の拒否 0 |
| QEMU の boot test（GPU なし、fix.img の複写） | PASS（`build/ws075-p025/boot-test/login.png`） |

QEMU と実機: 再現・修正の確認は全て 5330 の QEMU の VFIO passthrough（複数の vCPU）。素の 5330 での確認は未実施。

## 残り

- 素の 5330 での確認: 未実施。
- 30 窓まで開くと GPU の object の 128 の枠が尽きる（`gt memory: object pool exhausted`）。別の制限（候補、main の判断）。
- Model viewer の窓の角の drag で窓が消えた（1 回、原因は見ていない）。別件（候補）。
- 表の scan は線形で全 session の entry を lock の下で見る。session ごとの表は性能の候補。
- 元の 2 回の停止で、compositor が終わった後に greeter が 3 回失敗して console に落ちる（`ZWL EXIT error=21`）。停止の後の回復は別件。
