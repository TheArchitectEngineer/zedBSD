<!-- awesome-plan project=zedbsd record=ws073-p032 -->

# ws073-p032: Logi Bolt の受信機の HID の report descriptor を parse する（BUG-105）

Status: in-progress（host の修正と試験は完了。素の 5330 での確認はユーザー待ち）
Disposition: normal
Parent: [WS073](../ws.md)
Bug: [BUG-105](../../bugs/BUG-105.md)
Queue: main の依頼（2026-09-29、worktree `.claude/worktrees/ws088-nightly`、branch `wt/ws088` を main に合わせて使用）
Approval: main の依頼「parser がどの item で拒むかを特定し、`src/drivers/usb/usb-hid.c` の parser を直して、3 つの parse と mouse の report の decode を通す」。

## 範囲と受け入れ

- `drv_hid_report_layout_parse` が Logi Bolt の受信機（046d:c548）の 3 つの interface の descriptor を拒む item を特定する。
- parser を直し、keyboard（if0）と mouse（if1）の layout を作り、mouse の report（ID 2: button 16・X/Y 16 bit・wheel・AC Pan）を decode する。
- 既存の HID の host 試験（`plan/ws079/tests/run-hid-pen.sh`・`run-hid-touch.sh`、`plan/ws081/tests/run-hid-scantime.sh`）が回帰しない。
- 素の 5330 で受信機が mouse として動く（ユーザー）。

## 原因（host、gdb で特定）

`plan/bugs/bug105/run-hid-bolt.sh` の試験を -O0 で build し、gdb で `parse_input`・`add_field` の戻り値を追った。

| interface | 拒んだ item | 理由 |
| --- | --- | --- |
| 0（keyboard、67 byte） | 2 つ目の Input（offset 48）: usage 0x04〜0x73 の 112 bit の bitmap（variable） | usage 0x31（Backslash）と 0x32（Non-US # ~）が両方 `KEY_BACKSLASH`（Linux と同じ対応）。`add_field` が同じ report の同じ type・code の 2 つ目の field を EINVAL で拒んだ（`usage=0x70032 code=43`） |
| 1（mouse、133 byte） | report ID 4（System Control）の Input（offset 116）: array（flags 0）で usage が 3 つの列挙（0x82・0x81・0x83） | array は「usage が 1 つの range」でなければ、page を見る前に EOPNOTSUPP。keyboard 以外の array は元々読み飛ばす設計なのに、列挙の形で拒んでいた |
| 2（HID++、54 byte） | report ID 0x10 の Input（offset 20）: vendor page 0xff00 の array、usage 1 つ（range でない） | 同上の EOPNOTSUPP |

report ID 2 の mouse の field（button・X/Y・wheel）自体は受け付けていた。1 つの interface の parse が失敗すると driver はその interface を publish しないので、
受信機の mouse（if1）と keyboard（if0）が input device にならなかった。

## 修正（`src/drivers/usb/usb-hid.c`）

1. `add_field`: 同じ event の 2 つ目の field を拒む規則を、key（`HID_FIELD_KEY`）では適用しない。decoder は key の field を「どれかが立てば押されている」で
   まとめ（`append_value` の `key_already_present`、`usb_hid_publish_report` の held の和）、同じ key の 2 つの field は曖昧にならない。axis と pen の switch は従来どおり拒む。
2. `parse_input`: array（非 variable）の Input は、keyboard の usage を 1 つも名乗らないなら（新しい `local_names_keyboard`）bit を進めて読み飛ばす。
   keyboard の usage を含む array は従来どおり「1 つの range」だけを受ける（違えば EOPNOTSUPP）。
3. `usage_to_event`: Consumer page の AC Pan（0x0c0238、relative）を `REL_HWHEEL` に（desktop の `userland/desktop/wayland/input.c` は `REL_HWHEEL` を扱う）。

interface 2（HID++）は publish する field が無いので、parse は従来の最後の規則（`supported_field_seen` が無ければ EOPNOTSUPP）で失敗し、driver はその interface を
使わない（期待どおり。以前は array の規則で途中で失敗していた）。button は従来どおり 1〜5（BTN_LEFT〜BTN_EXTRA）を対応付け、6〜16 は読み飛ばす（M650 の 5 つの button で足りる）。

## 確認（host と QEMU。実機は未実施）

| 確認 | 結果 |
| --- | --- |
| `sh plan/bugs/bug105/run-hid-bolt.sh build/ws073-p032/bolt`（修正前） | if0 parse 3（EINVAL）、if1・if2 parse 21（EOPNOTSUPP）、FAIL |
| 同（修正後、試験を拡張） | PASS: if0 parse 0（field 128）、if1 parse 0（report 3、field 9、capability 10）、if2 は EOPNOTSUPP（期待）。mouse の report `02 01 00 05 00 fd ff 01 fe` → BTN_LEFT 1、REL_X +5、REL_Y -3、REL_WHEEL +1、REL_HWHEEL -2。keyboard の report（左 Shift、usage 0x04、0x32）→ KEY_LEFTSHIFT・KEY_A・KEY_BACKSLASH の 3 つ |
| `plan/ws079/tests/run-hid-pen.sh` | ok |
| `plan/ws079/tests/run-hid-touch.sh` | ok（193 checks） |
| `plan/ws081/tests/run-hid-scantime.sh` | ok（62 checks） |
| `python3 plan/ws073/tests/style-diff.py src/drivers/usb/usb-hid.c` | 変えた行の finding 0 |
| `make -j16 ZEDBSD_CONFIG=config/ci/config-amd64.mk BUILD=build/ws073-p032/amd64 vmunix` | 成功、warning 0（約 1 分 48 秒） |
| CI の構成の image（`build/ws088/p003/hdd-image.img`、main の `build/ws085-ci` の 19:21 の複写）の ESP の vmunix を差し替え、`plan/tools/boot-test.sh` | PASS（login prompt、`build/ws073-p032/boot-test/login.png`）。QEMU の usb-kbd で起動 |
| 素の 5330 で受信機を挿して mouse・keyboard が動く | 未実施（ユーザー） |

試験の拡張: `plan/bugs/bug105/host-hid-bolt.c`（main が作った BUG-105 の試験）に keyboard の report、AC Pan、if2 の EOPNOTSUPP の確認を足した。

## 残り・再開の条件

- ユーザーが素の 5330（この修正を含む image）で受信機を挿し、mouse の移動・button・wheel と keyboard（あれば）を確かめる → cleared、BUG-105 resolved。
- 列挙の段階（`lsusb` に 046d:c548 が出るか）は素の Kei で未確認。出なければ別の原因（USB の列挙）として調べる。

## Resume point

host の修正・試験・boot test まで完了。ユーザーの実機の確認を待つ。
