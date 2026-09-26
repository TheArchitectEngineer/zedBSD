<!-- awesome-plan project=zedbsd record=ws049p003 -->

# ws049-p003: 評価器（method、制御、式の opcode、参照、変換、Store の規則）

Phase ID: `ws049-p003`
Parent: [WS049](../ws.md)
Status: cleared（2026-09-27）
Phase disposition: normal
Queue: なし（2026-09-27 ユーザーのサブエージェント指示。branch の上で実行）

## 目的と受け入れ

p002 で書いた評価器（`aml-eval.c`・`aml-operator.c`・`aml-define.c` の Create*Field ほか）を、自作の ASL の試験で確かめる。
受け入れ: region を使わない ASL の試験が aml-host と acpiexec の両方で全部成功する。規約の検査 0 件。

## 試験

`plan/ws049/tests/asl/*.asl`（各 file の `\MAIN` が確かめ、失敗した検査の番号を返す。0 は成功）と `plan/ws049/tests/run-asl.sh`
（`iasl -oa` で `build/ws049/asl/` へ compile。定数の畳み込みを止めて opcode をそのまま出す。aml-host `--main` と `acpiexec -b "evaluate MAIN"` の
両方で走らせ、両方が 0 のときだけ成功）。

| file | 確かめること |
| --- | --- |
| `arith.asl` | 算術（Divide の商と余り、Mod）、shift（幅以上）、bit 演算、FindSet*Bit、BCD、Increment/Decrement（local・name・package の要素、桁あふれ）、論理と比較（Ones/Zero）、文字列・buffer の比較、暗黙の整数変換 |
| `control.asl` | If/Else/ElseIf、While と Break/Continue、入れ子の loop からの Return、再帰（階乗）、7 引数と 8 local、値を返さない method、Switch（compiler が作る一時の名前）、method の中の Name の生成と後始末 |
| `data.asl` | SizeOf、Index/DerefOf（string・buffer・package、要素への Store）、Concatenate（型ごと）、Mid、ToInteger/ToBuffer/ToString/ToDecimalString/ToHexString、名前付きの object への Store の型変換（buffer の長さの保持）、CopyObject、package の複製、VarPackage、Match、ObjectType |
| `refs.asl` | RefOf/DerefOf、参照の引数を通した Store、CondRefOf（有る・無い）、名前を指す package の要素、Alias、device の object、buffer の共有と整数の値渡し、local に保った Index の参照、文字列の path の DerefOf |
| `bufferfield.asl` | Create{Bit,Byte,Word,DWord,QWord}Field と CreateField の読み書き（byte の境界をまたぐもの、幅を超える値、buffer の値）、method の中の local の buffer の field、資源 template の書き換え、ConcatenateResTemplate |
| `int32.asl` | revision 1 の DSDT の 32 bit の整数（Ones、桁あふれ、ToBuffer の 4 byte、8 桁の文字列） |

## 結果（2026-09-27、iasl/acpiexec 20250404）

`plan/ws049/tests/run-asl.sh` → 6 file 全部 passed（aml-host と acpiexec の両方）。`style-check.py` 0 件。p002 の namespace の比較（q35・pc・
PRIMERGY・QEMU の 63 組）も同じまま。`make -C plan/ws049/tests kernel-check` warning 0。

## 試験で見つけて直した振る舞い（acpiexec を基準にした）

1. SuperName・Target の位置の method の名前を呼び出していた（`ObjectType (MAIN)` が再帰して深さの上限で止まった）。呼び出さず、名前の object とする。
2. buffer から文字列への暗黙の変換は、各 byte を `0x` つきの 2 桁で空白区切り（`"0xAB 0x01"`）。ACPICA 20250404 と同じにした。
3. CreateField で作った buffer field は、幅によらず常に buffer として読む（Create*Field の固定幅のものは整数）。
4. 名前を指す package の要素を `DerefOf (Index (...))` で読むと、その名前の object の値になる（1 回の DerefOf で値）。

acpiexec が拒み、zedBSD が受け入れる（寛容な）もの: `SizeOf` に buffer field、`Store` の target に `DerefOf (...)`。
試験からは外した（firmware が頼る振る舞いではない）。

## 未実施・制限

- OperationRegion を通る field は p004、Mutex・Event・Notify・Load 系は p005。
- 0 除算などの誤りで method を中断する経路は ASL の MAIN では確かめられない（評価が失敗する）。harness の `--eval` で個別に見る（p005）。
