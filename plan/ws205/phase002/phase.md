<!-- awesome-plan project=zedbsd record=ws205-p002 -->

# ws205-p002: libmedia の SBC の encoder・decoder

Parent: [WS205](../ws.md)
Status: in-progress（q931、P1。実装・host 試験済み）
Phase disposition: normal
Queue: q931（[P1 の lane](../../agents/P1/queue.md)）

## 実装

- `userland/desktop/libmedia/media-sbc.h`・`sbc.c`: A2DP 1.4.1 の付録 B（Bluetooth SIG の公開の PDF）から書いた。SBC の全部の設定（16/32/44.1/48 kHz、4/8 subband、4/8/12/16 block、mono・dual・stereo・joint、loudness・SNR、bitpool）と mSBC（syncword 0xAD、固定の設定、CRC は予約の 2 byte をそのまま含める）。filter は仕様の式の impulse 応答を直に（解析 hp(n) cos((m+½)(n−M/2)π/M)、合成 −M hp(n) cos((m+½)(n+M/2)π/M)、表の 2M ごとの符号を戻して hp）。bit 割り当て・再構成・joint・CRC-8 は B.6 のとおり、joint の選びは B.7.3。C の library だけに依る（bluetoothd が中に組み込むこともできる）。
- libmedia の `Makefile` に `sbc.c`、`exports.map` に `media_sbc_*`。

## 確認

| コマンド | 結果 |
| --- | --- |
| `sh plan/ws205/tests/run-sbc-host-test.sh`（FFmpeg の SBC を独立の側に、host の道具としてだけ） | **sbc: all PASS**。自前の encoder を FFmpeg で decode した SNR: 16 kHz mono 4/8 bitpool 16 → 51.7 dB、32 kHz mono 8/16 bp 31 → 70.8、44.1 dual 8/16 bp 32 → 66.3、48 stereo 4/4 bp 20 → 55.4、44.1 joint SNR 8/12 bp 35 → 66.0、48 joint 8/16 bp 53 → 65.5、44.1 joint bp 250 → 66.4。自前の decoder と FFmpeg の decoder の差は全設定で 76〜78 dB（遅れ 0）、FFmpeg の encoder（48 kHz stereo bp 49）の frame も 77.0 dB で一致。mSBC: 自前の encode を FFmpeg が decode 51.6 dB、FFmpeg の mSBC を自前で decode 77.4 dB（CRC の誤り 0）。ASan/UBSan で self と decode |
| `make … BUILD=build/ws157-q937 … dynamic/libmedia.so` | rc 0、warning 0、`media_sbc_*` 9 個を export |
| `plan/tools/style-check.py` sbc.c・media-sbc.h | 0 |

残り: `plan/ws205/tests/sbc-tool.c`（試験の道具）に規約の指摘 37（条件の中の呼び出し・段落の空行）、p011 の全文の見直しで直す。
