<!-- awesome-plan project=zedbsd record=ws174-p005 -->
# ws174-p005: 規約の全文の見直し（WS174 が変えた C）

Parent: [WS174](../ws.md)
Status: cleared（2026-10-06 P1。下の範囲で指摘 0、残りは記録した例外。Q1 の確認待ち。p003 の T1 の結果で C が変わったら、変わった所を見直し直す）
Disposition: normal
Queue: Q1 の dispatch（2026-10-05 夜、P1 へ p002・p003・p005）
依存: p002・p003 の実装（p004 BIOS は後日で、やるならその後にもう一度）

## 範囲

`plan/coding-style.md` の全文（§1〜§14 の checklist）と照らした C:

- 新しい file の全文: `bootloader/common/boot-override.c`・`.h`、`bootloader/uefi/boot-keys.c`・`.h`、`plan/ws174/tests/boot-override-host-test.c`・`boot-override-kernel-host-test.c`・`boot-keys-host-test.c`。
- 変えた所: `bootloader/uefi/bootx64.c` の新しい static 関数 `apply_boot_keys()`・`notice_boot_keys()` と、`efi_main()` に足した・変えた行（S0、S1、video の希望の if 連鎖、`notice_boot_keys()` の呼び出し、GOP の段落の comment）、`TEXT_MODE_*`、`struct loader_context` の member。`bootloader/uefi/include/uefi.h` に足した型・定数・GUID。

## 方法とコマンド

- 全文を読んで checklist の項目ごとに確かめた（file の順・forward declaration・型と file scope の変数の comment・公開関数の複数行の comment・宣言の位置・guard の if・関数を条件の中で呼ばない・条件演算子なし・ループの意図の comment・閉じ括弧の後の空行・最後の文が成功の return・`_local` 等の名前・著作権の header）。
- `timeout 120 python3 plan/tools/style-check.py bootloader/common/boot-override.c bootloader/common/boot-override.h bootloader/uefi/boot-keys.c bootloader/uefi/boot-keys.h plan/ws174/tests/boot-override-host-test.c plan/ws174/tests/boot-keys-host-test.c plan/ws174/tests/boot-override-kernel-host-test.c` → rc 0（指摘 0）。
- `python3 plan/tools/style-check.py bootloader/uefi/bootx64.c` の WS174 の行（1086〜1140、1475〜1575）の指摘: 下の例外だけ。

## 直したこと（見直しの中で）

- `boot-override.c`: 終端の代入と古い text を消すループを別の段落に。走査の変数を `read`/`write` から `read_position`/`write_position` に（POSIX の関数名と紛れない名前）。ループの中の段落の comment（token の検出、次の token へ）。`logo` の判定の段落の comment。
- `boot-keys.c`: `EFI_ERROR()` を条件の中で使わず `failed` に受けてから判定（`show_logo()` の前例と同じ形）。
- `bootx64.c`: video の希望の if 連鎖と `zbl_uefi_video_select()` の呼び出しを別の段落にし、それぞれ comment。S1 の判定を `status != EFI_SUCCESS`（`apply_boot_keys()` は SUCCESS か INVALID_PARAMETER だけを返す）。S0 の後に空行を入れたので、続く既存の GOP の段落に comment を足した。
- 試験の C: tally の printf を別の段落に、mock の file scope の変数を 1 つずつ comment、O8 の段落を分けた、parse の失敗の if に comment。
- 動作は変えていない: 直した後に host 試験 O1〜O11（43/0 ×2、72/0）・K1・K2（30/0 ×2）を流し直し、`BOOTX64.EFI` と image を build し直した（warning 0、`BOOTX64.EFI check: PASS`、`check-amd64-native-image: OK`）。

## 例外（記録）

- `efi_main()` の既存の本体（WS174 が触っていない行）は旧い書き方のまま（`EFI_ERROR()`・`framebuffer_from_gop()` を条件の中で呼ぶ、段落の comment なし、宣言の初期化子など。style-check で 1479・1498・1500・1503・1505・1512・1516・1560・1564 行）。WS174 の範囲は key の機能で、`efi_main()`（約 300 行）全体の書き直しは動作の危険に比べて得る物が小さく、この WS の範囲外とした。1560 行の `if (EFI_ERROR(status))` は WS174 が変えた `zbl_uefi_video_select()` の段落の既存の判定で、`!= EFI_SUCCESS` にすると firmware の warning の status で振る舞いが変わりうるので残した。`efi_main()` の規約の書き直しが要るなら Q1 が別 Phase にする。
- `uefi.h` は旧い書き方の宣言の file で、足した宣言は周りの形（1 行の macro、短い comment）に合わせた。新しい型には役目の comment を付けた。
- `-std=c11`（EFI_CFLAGS）の file の中だが、新しい C は ANSI C の宣言の位置（関数の先頭、`for` の中で宣言しない）を守った。
