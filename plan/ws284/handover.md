<!-- awesome-plan project=zedbsd record=ws284-handover -->

# WS284 の引き継ぎ（YOLOX-Nano、試験から続ける）

- 書いた日: 2026-10-29 23:23、P6（q1667-i05）。宛先: 別の tree の session。
- 2026-10-29 ユーザー（Q1 経由）「別なツリーのセッションで再開したときに試験から継続できるように、P6からハンドオーバーしてください。」
- **source の状態**: main（`7be3822e6`、Q1 が P6 の `212cbdd4a` を merge）に次の物が入っている。
  - `userland/desktop/libyolox/`: CPU の参照、`Makefile`、`Makefile.linux`、公開の 12 関数だけを出す `exports.map`
  - `userland/firmware/yolox-nano/yolox-convert.py`
  - `plan/ws284/tests/`: `make-reference.{sh,py}`、`znet-host-test.c`、`run-znet-host-test.sh`、`config.mk`、`config-rpi5.mk`
  - `platform/common/userland-dynamic.mk` の `libyolox.so` の規則
- **Vulkan の backend（p004）は未着手**。どの Phase も cleared ではない。design は [design.md](design.md)（第 2 版、§4 が試験の表 T0〜T9）。

## 0. 共通

### host の試験（流せば PASS の筈）

- `sh plan/ws284/tests/run-znet-host-test.sh`（T2b）
  - 合成の ZNET の load と CPU の detect（64 個）
  - 1 か所ずつ壊した 42 の file が全部 `YOLOX_ERROR_FORMAT`
  - API の引数の誤り
  - 通常と ASan/UBSan で、最後の行が `znet-host-test: PASS`

### build

- zedBSD:
  - `make -j16 ZEDBSD_CONFIG=plan/ws284/tests/config.mk BUILD=build/ws284/amd64 build/ws284/amd64/dynamic/libyolox.so`
  - rpi5 は `plan/ws284/tests/config-rpi5.mk` で同じ
  - どちらも rc 0・warning 0 が条件
- Linux の host:
  - `make -f userland/desktop/keiland-linux.mk KEILAND_LINUX_PACKAGES=userland/desktop/libyolox/Makefile.linux KEILAND_LINUX_BUILD=build/ws284/linux build/ws284/linux/lib/libyolox.so`
  - Keiland の Linux 版の package の一覧には入れていない（配布の判断は別）。

### 前提

| 物 | 要る試験 | 状態（2026-10-29） |
| --- | --- | --- |
| 重み `yolox_nano.pth`（7,694,953 byte、SHA-256 `cd28f55f…feff7b`、design §0 の URL） | T0・T1・T2 | P6 は前の session の scratchpad の物を使った（tree に無い）。新しい tree では package の取得か `make-reference.sh` が GitHub の release から取る（web の GET の条件は master.md の「統合の注意」） |
| T1 の venv（torch の CPU 版・torchvision・opencv-python-headless・onnxruntime など、design §4 の F17） | T1・T2・T5 | 未作成。`make-reference.sh` が `build/ws284/venv` に作る（network が要る） |
| 試験の画像 3 枚（COCO val2017 の license 7・8、D11 は「入れる」に決定） | T1・T2・T5 | 未選定 |
| host の lavapipe（`/usr/share/vulkan/icd.d/lvp_icd.json`、あり）と `vulkan-validationlayers`（無い、sudo で入れる） | T3〜T6・T9 | p004 の前に入れる |
| Pi 5 の GPU（WS279 の V3D）、WS283 の compute（p004・p006・p007） | T7・p006・p008 | WS283 の状態は Q1 に確かめる |

### 決まっている事（design §7）

- D10: license の同意（yes/no）は出さない。LICENSE と NOTICE を重みの横に置く。
- D11: COCO の試験の画像を tree に入れる。
- **まだ決まっていない事: 無し**（2026-10-29 の時点）。

## 1. Phase ごと

各 Phase は「状態 → 直した所 → 試験と基準 → 再開点」の順に書く。

### p002 重み（変換・package・参照）

- 状態:
  - 変換の script は動いた。
  - **package `yolox-nano-weights` は未着手**。
  - T0 の (b)〜(e) と参照の生成（T1）は未実施。
- 直した所と理由: `yolox-convert.py` が BN の `num_batches_tracked`（LongStorage、重みでない）で止まっていた。その key を読まずに飛ばすようにした（`a83c3ee83`）。
- 決まった値: 2 回の変換が一致した（T0 の (a)）。**ZNET は 3,631,706 byte、SHA-256 `4469dbeb7ad77909690afe17f9ecd4dd6e7995643ac0a34561656b5c4fe73e6d`**。この値を package の Makefile と manifest に固定する（design §2.2 の 6）。
- 試験:
  - T0:
    - (b) Python の読み手で読み戻して、float64 の期待と一致
    - (c) stem の RGB の並べ替え
    - (d) ONNX の initializer と `|a − b| ≤ 1e-6 × max(|b|, 1e-3 × 層の max|b|)`
    - (e) build の固定の SHA-256 と一致
  - T1: `sh plan/ws284/tests/make-reference.sh IMAGE.jpg...`。PyTorch と ONNX の差 `|d| ≤ 1e-4 + 1e-4·|ref|`、416 と 320。
- 再開点:
  - package（`userland/firmware/yolox-nano/`、`zedbsd-firmware.mk`、install の 4 file、design §2.4）
  - その後 T0
  - その後 T1（venv）

### p003 libyolox の CPU の参照

- 状態:
  - 実装済み。T2b は PASS。
  - 実の ZNET を host で load して CPU で detect できる事は確かめた。灰色の 640x480 で検出 0、416 で 2.4 s。
  - **T2（PyTorch の参照との数値の照合）は未実施**。
- 直した所と理由:
  - `libyolox.so` の build の規則を足した。前は「No rule」で build できなかった。
  - `Makefile.linux` を足した。design §3.1 の host の試験のため。
  - `exports.map` を公開の 12 関数の列挙にした。前は glob で内部の関数も出ていた。
- 試験（T2）: T1 の前処理の済んだ入力を CPU の runner に通して比べる program を書く（未作成。`Makefile.linux` の lib か source の直の compile で）。
- T2 の合格の基準:
  - 85 値が `|d| ≤ 1e-3 + 1e-3·|ref|`（416・320）。
  - 検出の集合が同じ（class が同じ、IoU ≥ 0.99、score の差 ≤ 1e-3。閾値の近くの除外は design §4 の T2 の欄）。
- 再開点: p002 の T1 の参照ができてから T2 の program。

### p004 Vulkan の backend（未着手）

- design §3.4〜§3.6 と付録 A にある。中身は 15 の SPIR-V（`shaders/*.comp` と `regenerate.py`）、固定の graph、分割、debug の runner。
- 試験は T3〜T6・T9。host の lavapipe と validation layer で、error 0 が条件。
- 再開点: p003 の T2 の後に設計どおり実装する。

### p005〜p008

- p005: zedBSD の package と `yolox-test`。
- p006: Pi 5 の実機（T7、85 値 `|d| ≤ 5e-3 + 5e-3·|ref|`）。
- p007: 規約の全文と license の監査、回帰の data を `tests/` へ移す（F20）。
- p008: V3D 7.1 の host の compile。
- どれも未着手（planning）。依存は ws.md の表。

## 2. 再開点（まとめ）

1. main から `run-znet-host-test.sh` と build を流す（§0）。
2. p002 の package `yolox-nano-weights` を固定の値で作る → T0。
3. 試験の画像 3 枚を選び（license の記録）、`make-reference.sh` で T1 の参照（venv、network が要る）。
4. T2 の照合の program → p004 の Vulkan。
