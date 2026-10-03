<!-- awesome-plan project=zedbsd record=ws088-p003 -->

# ws088-p003: `make kei-nightly-zip`（取得・検証・image の追加・zip）

Status: in-progress（準備まで。p002 の upload の前なので URL からの取得は未確認）
Disposition: normal
Parent: [WS088](../ws.md)
Queue: main の依頼（2026-09-29、worktree `wt/ws088`、「答えを待つ間に p003 の準備を進める」）
Approval: main の依頼。upload・GitHub への書き込み・push はしない。CI の変更は案だけ（p004）。

## 範囲と受け入れ

- `make kei-nightly-zip`: base の zip を取得（`build/releases/` に cache）→ SHA-256 を確かめる → `$(BUILD)/hdd-image.img` を
  `Kei-nightly/data/hdd-image.img` として加える → `$(BUILD)/Kei-nightly.zip`。
- 名前と SHA-256 は `tools/release/kei-nightly.mk` に固定（今は p001 の draft の値。確定したら差し替える）。
- host での確認: zip の中身、`data/hdd-image.img` の有無、展開した image が Linux の QEMU で起動するか。
- CI（`.github/workflows/ci.yml`）の変更の案を `plan/ws088/` に置く。

## 設計

- `tools/release/kei-nightly.mk`（top-level の Makefile から `include`。Makefile の変更はこの 1 行だけ。platform の Makefile の include の直後）:
  - 固定: `KEI_NIGHTLY_BASE_TAG := rev-0`、`KEI_NIGHTLY_BASE_ASSET := kei-nightly-base-winq-a10-1.zip`、`KEI_NIGHTLY_BASE_SHA256`（draft の
    `81120981…`）。URL は `https://github.com/awemorris/zedBSD/releases/download/rev-0/<asset>`（LLVM の cache と同じ形）。
  - `kei-nightly-base`: `build/releases/<asset>` があれば SHA-256 を確かめ、無ければ一時 file に curl で取り、確かめてから置く（違えば消して失敗）。
  - `kei-nightly-zip`: amd64 の config だけ（他の platform は parse の時点で失敗の recipe にし、その platform の image を作り始めない）。
    `KEI_NIGHTLY_IMAGE`（既定 `$(BUILD)/hdd-image.img`）が無ければ「run make first」で失敗。**image を作り直さない**（CI は build の直後に呼ぶ。
    image の rule の依存は深く、prerequisite にすると CI で image を作り直す危険がある）。出力は `KEI_NIGHTLY_ZIP`（既定 `$(BUILD)/Kei-nightly.zip`）。
  - `KEI_NIGHTLY_ALLOW_DRAFT=1` のときだけ draft の base（THIRD-PARTY.txt に「not yet confirmed; draft package」）を受け付ける。CI では使わない。
- `tools/release/make-kei-nightly-zip.py`: base の SHA-256・member の名前（`Kei-nightly/` の下、`..`・`\` 無し、重複無し）・必須の file・
  `data/hdd-image.img` が無いこと・CRC を確かめ、base を複写して image を 1 entry だけ追記する（base の entry は byte 単位で変わらない）。
  image は deflate（既定の level）、ZIP64（2,216,689,664 byte は python の 2 GiB の閾を超える）、時刻は image の mtime（`SOURCE_DATE_EPOCH` があればそれ）。

## 確認（2026-09-29、QEMU・host だけ）

image は main の `build/ws085-ci/hdd-image.img`（CI と同じ構成）を worktree の `build/ws088/p003/hdd-image.img` に sparse の複写をして使った
（main が途中で作り直したため。複写の SHA-256 `6b39ceac…8759`）。base は p001 の draft を `build/releases/` に置いた。

| 確認 | 結果 |
| --- | --- |
| `make ZEDBSD_CONFIG=config/ci/config-amd64.mk BUILD=build/ws088/p003 KEI_NIGHTLY_ALLOW_DRAFT=1 kei-nightly-zip` を 2 回 | 成功（約 21 秒）。2 回とも `7467d737…d18e`、148,377,930 byte（同じ image・同じ mtime なら同じ zip） |
| draft の base を `KEI_NIGHTLY_ALLOW_DRAFT` 無しで | 「the base archive is a draft」で失敗（期待どおり） |
| cache が無いときの取得（`KEI_NIGHTLY_BASE_URL=file://…` で curl の経路） | 取得して SHA-256 一致で置いた |
| cache の破損（1 byte 足す） | 「cached … does not match the pinned SHA-256」で失敗 |
| 違う file の取得 | 「downloaded base archive SHA-256 mismatch」で失敗、一時 file は残らない |
| image が無い | 「missing …/hdd-image.img; run make first」で失敗 |
| i386（`config/ci/config-pcat.mk`） | 「carries the amd64 image; use an amd64 config」で失敗（image を作らない） |
| `unzip -tq`、全 entry の比較 | No errors。base の 211 entry は CRC・圧縮後の大きさ・時刻が同じ、足したのは `Kei-nightly/data/hdd-image.img` だけ（圧縮後 92,034,905 byte） |
| 展開した image の SHA-256 | 元の image と一致 |
| 展開した image を zip の中の firmware（`data/edk2-x86_64-code.fd`・`data/ovmf-vars.fd`）で `plan/tools/boot-test.sh`（NVMe、TCG、std VGA） | PASS。login prompt（`build/ws088/p003/boot-test/login.png`） |
| Venus（`virtio-vga-gl,venus=on`）での起動・Windows | 未実施（p005） |
| 実際の Release の URL からの取得 | 未実施（p002 の upload の後） |

## CI の案（p004、適用しない）

[ci-kei-nightly.diff](../ci-kei-nightly.diff): build の job の image の gzip の後に `make kei-nightly-zip` と `artifacts/Kei-nightly.zip` への複写、
artifact の path と nightly の Release の `files`・表に `Kei-nightly.zip` を足す。`git apply --check` と YAML の読み込みは PASS。python3 と curl は
ubuntu-latest にある（curl は既存の apt の行）。

## 残り・再開の条件

- p001 の base が確定（fork の commit を入れて作り直す）→ p002 で upload → `KEI_NIGHTLY_BASE_SHA256`（と版を変えるなら asset の名前）を差し替える。
- 差し替えの後、`build/releases/` の cache を消して実際の URL から `make kei-nightly-zip` を 1 回（draft の許可無し）→ p003 cleared。

## Resume point

準備まで完了。p002（upload）の後に、上の「残り」の 2 つ目から。
