<!-- awesome-plan project=zedbsd record=ws075p022 -->

# ws075-p022: 性能: encoder の scoreboard の直列の緩和

Phase ID: `ws075-p022`
Parent: [WS075](../ws.md)
Status: in-progress（2026-09-29）
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

## 記録

（実施中）
