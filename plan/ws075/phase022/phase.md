<!-- awesome-plan project=zedbsd record=ws075p022 -->

# ws075-p022: 性能: encoder の scoreboard の直列の緩和

Phase ID: `ws075-p022`
Parent: [WS075](../ws.md)
Status: uncleared（2026-09-29。実装し host で正しさを確かめたが、実機の 10 app で compositor の 1 run の engine の時間が縮まず（8.57 → 8.70 ms）、受け入れの「縮む」に届かない。source の変更は branch から戻し、差分は `scoreboard-pass.patch` に残した。検査の道具は残した）
Phase disposition: normal
承認: 2026-09-29 main の判断（p021 の後、「全ての shader に効くので先に」）。

## 範囲

i915 の compiler の EU encoder（`compiler/eu.c`）は、out-of-order の命令（MATH・SEND）の直後に必ず sync.nop（token 0 の .dst、宛先の無い
SEND は .src）を置き、結果を待ってから次へ進む。この Phase: その待ちを、結果を使う（または payload を書き換える）直前まで遅らせ、
out-of-order の命令ごとに別の token（16 個）を使う。in-order の命令の @1 の鎖は変えない。

受け入れ（main）: guard/run.sh と vk の host 試験、`test-hw.sh` の vkx・vke1・vke2・vkc が PASS。実機の passthrough で 10 app の
compositor の 1 run の engine の時間と latency が p021 より悪くならず縮む。画面が前後で画素で一致。

## 設計

- `drv_i915_eu_schedule()`（`eu.c`、compile の最後に `compile.c` が呼ぶ）: 出来上がった program を 1 回の走査で組み直す。
  1. encoder の sync.nop（out-of-order の命令の直後の token 0 の .dst・.src）を落とし、WHILE の飛び先（loop の最初の命令）に印。
  2. 命令ごとに書く・読む general register の範囲を encoding から読み戻す（SEND は descriptor の rlen・mlen・ex_mlen、ALU は
     subregister・channel 数・型の大きさ・stride）。
  3. in flight の token ごとに: 宛先を読む・書く命令の前に `sync.nop $n.dst`、読む source を書く命令の前に `$n.src`。loop の最初の命令・
     WHILE・thread を終える SEND の前は全ての token を待つ。IF・ENDIF では待たない（IF の body を飛ぶ channel は命令が減るだけで、
     ENDIF の後の in flight の token は body の後の部分集合）。
  4. out-of-order の命令は空いている次の token を取る（全て使用中なら一番古いものを待って使う）。
  5. IF の JIP・UIP、ENDIF・WHILE の JIP を、間に入った・抜けた sync.nop の分だけ動かす（飛び先の待ちから）。
- 独立の検査 `plan/ws075/tests/guard/scoreboard-check.h`: kernel を順に歩き、in flight の token の宛先を待たずに触る命令、source を待たずに
  書く命令、使用中の token を取る命令、loop の境目と EOT の前に残る token を fault にする。register の範囲は encoder と別に書いた。
  compile の host fixture（`i915-vk-compile-test.c`）の EU model が走らせる全ての kernel と、`guard/run.sh` の module に掛ける。

## 記録（2026-09-29）

### 実装と host の確認

- `eu.c` の `drv_i915_eu_schedule()` と補助（`i915_eu_schedule_*`・`i915_eu_touches`・`i915_eu_operand_run`・`i915_eu_wait` ほか）、
  `compile.c` が compile の最後に呼ぶ。panel.frag の sync.nop は 8+ の直後の待ちから、使う直前の 19 個（.src 含む）へ。
- host: `guard/run.sh`（scoreboard の検査: 全 module sound、sync.nop を抜くと fault を見つける）、vk の host 試験 spirv・lower・eu・compile・pipe・
  resdispatch（compile は EU model の走らせる 4271 の kernel 全てに scoreboard の検査）、`run-vk-gentool-test.sh`（Mesa 25.0）: 全て PASS。

### 実機（5330 の passthrough、`measure-apps.sh`、同じ tree の p022 の前（base.img: compiler の file だけ a85ea4cc）と後（sched.img））

| 物差し | p022 の前 | p022 の後 |
| --- | --- | --- |
| desktop だけ rate・latency 中央値 | 58.1/s・15.9 ms | 58.3/s・16.0 ms |
| 10 app rate | 8.5/s（8.2/s） | 8.1/s（8.0/s） |
| 10 app latency 中央値（範囲） | 66.0 ms（15.7〜113） | 49.3 ms（32〜115） |
| compositor の engine の占有・1 run | 50.4%・8.57 ms（58.7 run/s） | 48.5%・8.70 ms（55.8 run/s） |

- 画面: 10 app の各段で前後が画素で一致（上の bar と Notes の file 名の時刻、動いている Gears と前面の X terminal の窓の中を除く）。
- latency の中央値は run ごとのばらつきが大きく（p021 の測りでは同じ image で 31.5 ms）、この差は改善と言えない。
  compositor の 1 run は変わらない。

### 所見

- 待ちを遅らせても、今の compiler の出力では send の結果がすぐ次の数命令で使われ、payload の register もすぐ書き換えられる
  （panel.frag: send の 2〜8 命令後に .src と .dst の待ち）。命令の並べ替え（send を前へ）が無いと重ならない。
- EU は 1 つに 7 thread を持ち、thread の間で待ちを隠すので、1 thread の中の直列は全体の速さをほとんど決めていない（推測、結果と合う）。
- compositor の GPU の時間は、分岐の中の ALU（panel.frag の約 380 命令を全 pixel で）か、draw ごとの pipeline の停止と cache の flush
  （`render/state.c`: draw の前後に CS stall、後に RT・depth・DC の flush、draw ごとに STATE_BASE_ADDRESS と cache の invalidate）の方が
  大きいと見る（推測）。p023（分岐の中の ALU を飛ぶ）はこの前者に当たる。

### branch の扱い

効果が測れない変更を demo の前の tree に入れる危険を避け、`eu.c`・`eu.h`・`compile.c` を a85ea4cc に戻した（差分は
`plan/ws075/phase022/scoreboard-pass.patch`、`git apply` で戻せる）。検査の道具 `plan/ws075/tests/guard/scoreboard-check.h`（compile の
host fixture と guard/run.sh で全 kernel に掛ける）は残す（今の encoder の出力にも sound。宛先の無い message の token は .src の待ちで
空く、を今の encoder の前提として書いた）。
