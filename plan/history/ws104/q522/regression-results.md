# q522 / ws104-p008 最終回帰

最終 amd64 build exit 0・自前 warning 0、全文規約の変更範囲違反 0（理由つきの保持と tool 誤検出は standards-review.md）。境界 C1〜C5 PASS、故意の uapi include は C1 FAIL / exit 1、同一に復元。GPU V1 54 source、dedicated host 18 case × ordinary / sanitize、decode host 17 case × ordinary / sanitize、forge / fence guest（600 fence、generation 1、600 frame / 64 s）PASS。compositor C1/C2/C9 は 13/13 PASS、glass p059・Notes pen/PDF・Settings host / guest 8 本・host audio 14/14・音量 p004/p005 PASS。必須 PNG は目視し、boot の login PNG をユーザーに提示した。実機・Linux compositor・他 platform は未実施。証拠と各試験の summary は plan/history/ws104/q522/。

最終 source（最後は成功注釈 1 行のみ、runtime image と再 build の compositor ELF は全て SHA256 同一）: `cd48e74d110a1e504c2b87ac288d41cdc928367c`（WIP）。各 test image の build と guest 実行は直列。各 build に専用 BUILD、boot に専用 OUTPUT を指定。QEMU console / serial log は判定に使用しない。試験の wrapper は set -eu、各 named script の exit と PASS marker、criteria の 13 全行を確認した。

## 実行した command と記録

| 試験 | command / 結果の記録 |
| --- | --- |
| amd64 | `make -j64 disk-image`、自前 warning 0、image check OK。review build も exit 0 |
| GPU | `v1-check.sh build/amd64`、`run-dedicated-host.sh`、`run-gpu-zedbsd-host.sh`（plan/tools/gpu-boundary/）。普通 / sanitizer の PASS はそれぞれ 2 回。`build-forge-image.sh build/ws104-p008-forge`、zdesktop-guest start / wait 240、`forge-guest.sh build/ws104-p008/forge`、`fence-guest.sh build/ws104-p008/fence`、stop |
| boot | `OUTPUT=build/ws104-p008/boot plan/tools/boot-test.sh build/amd64/hdd-image.img`、[login PNG](login.png) |
| compositor | `build-criteria-image.sh build/ws104-p008-criteria`、`criteria.sh build/ws104-p008-criteria/hdd-image.img build/ws104-p008/criteria C1 C2 C9`（plan/ws099/tests/）、[全行結果](criteria-results.txt) |
| glass | 専用 runtime で criteria image を start / wait 240、`plan/ws035/tests/zdesktop-p059.sh build/ws104-p008/p059`、stop |
| pen / PDF | `build-notes-image.sh build/ws104-p008-notes`、専用 runtime start / wait 240、`notes-pen.sh build/ws104-p008/notes-pen`、stop（plan/ws079/tests/）。qpdf・ink pressure/tilt/eraser/undo/hover・app ERROR 0 を含む |
| Settings / audio | `host-build.sh`、`build-settings-image.sh build/ws104-p008-settings`、`settings-guest.sh start`、`settings-regress.sh build/ws104-p008/settings-regress`、stop（plan/ws089/tests/）、`plan/ws100/tests/host-audio.sh` |
| 音量 | WS100 guide §5.1 の target clang compile/link で audiod-feedback を作り、`build-volume-image.sh build/ws104-p008-volume`、`volume-p004.sh IMAGE build/ws104-p008/volume-p004`、`volume-p005.sh IMAGE build/ws104-p008/volume-p005`（plan/ws100/tests/）。capture WAV の feedback / mute / volume 検査を含む |

正確な再利用コマンドは [zedBSD commands](../../../tools/keiland-linux/zedbsd-commands.md) §0〜§9。tools version と各 build の分類は [環境](verification-environment.json)、規約の対象・例外・制限は [全文レビュー](standards-review.md)。判定に使った summary log をこの directory に保存し、一時詳細・frame列・capture WAV は `build/ws104-p008/`。全文の未変更の周囲を一律に format したとの主張はしない。

C9 p076 の PASS は既存 BUG-125 の解決を意味しない。物理 hotplug は p005 の仮想 node の証拠と区別する。未実施の実機 / Linux の関門は今回の scope 外。GitHub publication / remote closure は未実施。
