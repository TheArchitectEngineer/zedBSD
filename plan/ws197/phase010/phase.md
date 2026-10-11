<!-- awesome-plan project=zedbsd record=ws197-p010 -->

# ws197-p010: MMSの写真・動画の受信と送信

Phase ID: ws197-p010
Parent: [WS197](../ws.md)
Status: in-progress
Phase disposition: normal
Queue: [Codex承認済み実行記録](../codex-queue.md)、2026-10-10ユーザーの4機能実装指示。
Branch: codex/ws197-mms-media、base main 9dfebc99b。
Prerequisites: 既存MAP Get/Push・MMS text reader・PBAP/Phone保存、mainの実sourceを確認済み。
Standards: [Guardrail](../../guardrail.md)、[C全文](../../coding-style.md)、[automation](../../standards/automation.md)。例外なし。master/共有Queue/他P tree/toolchainは書かない。自分のnamed buildだけ行い、QEMU/全make checkを追加しない。

## 承認・範囲

ユーザー「写真の受信、動画の受信、写真の送信、動画の送信も一緒に実装してしまいましょう。」を4機能全体の承認として記録。p004の旧MMS添付除外をこの範囲で変更する。p005の完了条件は変更しない。HFPは変更しない。実機からの実際の送信はユーザーが操作する。

## 設計・手順

1. OSに依存しない独自Zlib MIME codecを `userland/desktop/libmms/` に置く。既存bluetoothdのMMS text readerをwrapperで維持し、PhoneとzedBSD phone backendが同じcodecを使用。base64/quoted-printable/identity、既存charset/階層上限を維持。text/plainとimage/*/video/* partを取得し、decoded媒体は計8MiB、MIME bodyは16MiB、media最大16個。上限超過/読めない媒体は明示エラー、内容を文字として表示しない。
2. MAP GetMessageで添付を要求し、MMSのMIME bodyをblobとして中継、写真/動画だけのメッセージも取り込む。送信はPhoneが標準MIMEを作り、libkeilandのFD request→compositor→zedBSD backend→bluetoothd→TYPE:MMS PushMessageへ運ぶ。送信成功/失敗/切断は既存request状態へ接続。daemonは永続保存しない。
3. Wayland phone interfaceをversion 29、libkeilandを81へ拡張。MIMEを匿名FDで渡し、public item末尾に有無付きdescriptorを追加。旧callerには旧サイズのfieldsのみを渡しFDを解放、新callerはdescriptorをcloseする。backend itemは次takeまで所有、public itemはcallerに所有移譲。本文は従来のUTF-8、写真データはwireの文字列に入れない。OS別unsupported backendはENOTSUP。
4. Phoneのstoreが画像/動画を `~/Documents/Phone/` の管理下へ保存し、再open・同期重複/同一画像複数partを扱う。番号/本文をlogに保存しない。写真は既存PNG/JPEG/GIF decoderで実画像を描く。動画はtimelineに実媒体の項目を表示し、クリックでVideo Playerを開く（codec対応/fallbackは既存appの責任）。Attach File chooserで写真/動画を選び、既存送信操作で送る。自動変換/圧縮は今回追加しない。
5. 最終変更source全体をC全文/境界/format/style-checkで確認、MIME roundtrip/画像・動画part/バイナリ/上限、Get/Pushのbyte、FD所有/旧ABI、backend→Phone保存/再open/重複、実画像描画、chooser/sendの接続のhost回帰と named build warning0。実機4項目はユーザーに具体的な手順を提示して検証し、未実施と成功を分ける。

## 完了条件・検証境界

4機能の取得/中継/保存/表示または再生起動/送信が実装され、host回帰とzedBSDのbluetoothd/wayland/libkeiland/Phone named build warning0、Linuxの変更した共通interface build、全文規約が通る。carrier/スマホ側のサイズやMMS Push未対応は明示エラーで表示し、SMSやMMS本文を壊さない。実機UATは別途ユーザーの結果を記録する。unknown filename代替textの詳しいbMessageはまだ未取得。

## 2026-10-10 追加と設計更新

p004のMMS text-only判断から、ユーザー指示で写真/動画の双方向転送へ拡張。p010を追加し、p004とWSへ相互参照を保存。WS全体はincomplete、p009の最終WS conformanceはp010も含む。共有master/Queueへの投影はQ1へ保留。

## 2026-10-10 メディア管理を先行する指示

ユーザーがCLI所有のメディアDB、compositor経由の取得・追加・監視、Photos再読込、Phoneの＋とDnDを先行指定。MMS送受信接続はその後（確認質問の前提と回答）。従来のPhone独自メディア保存の設計を置き換える。現在のMIME/Get/Push/backend変更は未統合・未完了の途中資料としてこのworktreeに保存。共有codecの画像/動画各262147 bytes roundtripとbMessageはhost PASS、bt-bmsg 90 checks PASS、MAP回帰は旧仕様assertを修正中。新メディアAPIの完成後、FD運搬と送受信を新基盤へ接続して再検証する。Phaseはclearedにしない。

2026-10-10再設計の反映: メディアCLI/compositorの[ws157-p006](../../ws157/phase006/phase.md)とPhoneの[ws197-p011](../phase011/phase.md)の出力を次回の前提にする。取得/保存はパスとメタデータのAPI、draft選択は共通media chooserに接続する。元の写真/動画4機能の義務は保持するが、ユーザーの最新限定により今回Queueで実装再開しない。旧MIME試作のscope/evidenceは[履歴](../history/mms-prototype-20261010.md)へ保存。

2026-10-10前提の更新: ws157-p006/p007とws197-p011はhost/build scope cleared、main `874e12d3b`。後続MMSはmediastorageのMedia JSON/Filesとlibkeiland path-list APIへ接続する。p010自体はunclearedを維持し、今回finite Queueは終了。新しい試行にはQueue選択/承認を記録する。

## 2026-10-10 受信修正の再開

最新ユーザーの写真受信不具合とSSH確認指示により、[受信部分Queue](../codex-queue.md)を開始。実機c43a01797のMNS通知を確認、Media/Filesなし、MAP Attachment=0とPhone media未接続が実sourceの原因。host鍵削除・更新もユーザー承認済み。manager29/lib81はメディア基盤で使用済みなのでMIME受信eventはmanager30/lib82へ追加し、旧item eventを維持。保存はmediastorageのパスAPIでのみ行い、Phoneのmessageに保存後の絶対パスを永続化する。写真は既存Photos decoderを共有し、動画は保存とtimeline項目（外部再生は既存Video app）。送信は未完でPhase全体のcleared条件を満たさない。

## 2026-10-10 受信部分の実装・最終reviewと実機更新

受信部分のsourceはcodex/ws197-media-receive (base c43a01797)、[検証記録](../tests/media-receive-verification-20261010.md)。写真/動画Get、MIME匿名FD v30/lib82、mediastorage original/path、message再open、実Photo decoderをhostで確認し、named buildと変更source全文規約を実施した。旧prototypeの送信APIは今回sourceへ導入しない。shared codecはOS非依存のdesktop/libmmsに置き、A5を含むOS境界checkerを通した。

4ファイル交換とdesktop/Bluetooth restartはユーザーが明示承認。新desktop自動loginとPhone起動まで実機確認したが、Bluetoothはunreachable、再restart後SSHもtimeout。受信実機UATと最終Phone更新は接続回復待ち。部分実装はmain統合可能な状態だが、Phase全体はin-progress、4機能の完了を主張しない。原本バックアップ/CRC/再開手順は上記証拠へ保存。

mainへ受信source/evidence `035d1d25b` をfast-forward統合済み。source同一/clean、pushなし。実機受信UAT未確認・全p010未完は維持。

## 2026-10-10 イメージ再作成後のMIME相互運用修正

ユーザーがテキストのMIMEヘッダ露出と画像MMSの通知欠落を報告。[Queue i02](../codex-queue.md)として修正を再開。実機50feed3は4ファイルCRC一致・mediastorage導入済みで、mixed binaryを原因としない。MNS NewMessage/MMSは届いていたがMAP本文解析はENODATA、別画像は保存EOPNOTSUPP、既存本文にはboundaryとpart headerが残っていた。

`mms_part`がmultipart/*だけで区切りを解析していたことが原因。明示boundaryを先に評価してleaf/WAP型でもpartを取り出す。標準multipartのboundary欠落は従来どおりエラー。公開API・上限・SMS動作は変更しない。独自codec/Phone実fixtureで本文と画像byteの分離・原本保存/decode/再openを確認し、3target build warning0、全文規約/変更3C style-check0。既存承認の3ファイル交換とBluetooth/desktop restartを実施し、MAP/PBAP ready、履歴画像の保存成功をSSH確認。i02部分scopeはcleared、新規受信の通知/実表示はユーザー確認待ち、Phase全体はin-progress。詳細/CRC/復旧用backupは[証拠](../tests/media-receive-verification-20261010.md)。旧試行の実機失敗は履歴として保持する。

確認結果追記: テキスト/写真MMSの実機確認依頼にユーザー「受信し、表示されました。」。SSHでもMNS MMS通知と写真保存件数の増加を確認した。source `89d487814`、受信部分check cleared、有限Queue finished。これはp010全体のclearanceではない。送信/動画player起動等は残す。

## 2026-10-10 viewer起動の補完

最新ユーザーの5項目を扱う[ws197-p012](../phase012/phase.md)が受信媒体をdouble-clickでImage Viewer/Video Playerへ開くsourceを実装する。保存pathをview requestへcopyして後続syncの並べ替えから切り離す。p010のplayer起動の出力はこのPhaseの成果を参照する。送信の未完条件とin-progress状態は保持。

2026-10-10結果: p012の添付viewer起動/thumbnail/起動NULL参照修正はmain `e654733f1eb6e1296eacf34b5dff6002a5dd17e1`、scoped cleared。p010の送信残件とin-progress statusは維持。

## 2026-10-11 q927（P1）: 写真・動画の送信（新しい mediastorage・path の API の上に作り直し）

承認: 2026-10-11 ユーザー（P1 lane の先頭）「MMSの添付ファイル送信、画像送信、動画送信」。旧試作（`.claude/worktrees/ws197-media`、未 commit）は読むだけで、今の main（受信の MIME FD・libmms・Media JSON）に合わせて書き直した。

経路: Phone（下書きの写真・動画と文字 → `ph_draft_mime` が libmms の `mms_write_parts` で MIME を private の file に書く）→ 同じ MIME を `ph_receive_media` で library に取り込み（hash で重複は既存の path）、timeline の item（PH_MMS、送信中）に media の path を保存 → `kl_system_phone_send_mms(system, to, fd, &request)`（KL_VERSION 86、manager 32、phone の request 9 `send_mms(uint request, string to, fd mime)`）→ compositor `phone-shell.c` が fd を取り backend へ（読んだら close）→ `kl_backend_phone_send_mms`（zedBSD: 正規 file だけ、16 MiB まで pread、`PHONE SEND to=… type=mms length=N` の後に本体、答えの期限は 120 秒＋本体 16 KiB/秒）→ bluetoothd（phoneio が type=mms で 16 MiB まで、`btd_map_send_mms` が MMS の MIME を検査して bMessage TYPE:MMS で PushMessage、大きい物は 1 つずつ、phone が MMS を受けなければ no-send）→ 状態は text と同じ（done、PHONE SENT の sent・delivered）。Linux・FreeBSD の backend は ENOTSUP。

上限: 1 通に写真・動画 16 個まで、元の bytes で合計 8 MiB（libmms の MMS_MEDIA_MAX）。越えると「too large」の notice、下書きは残す。変換・縮小はしない（carrier の上限はスマホ側の結果として失敗の状態で出る）。

### 確認（host・build）

| コマンド | 結果 |
| --- | --- |
| `sh plan/ws197/tests/bt-phone-host-test.sh`（OUT=build/ws197-p010-bthost） | 全 PASS。runner に libmms を足した（main で bt-mms・bt-bmsg・bt-map が link できなかった）。bt-map の試験が btd_map_init で MAS の body を捨てて LSan が leak を出していたので、試験の start で free（試験の直し）。新: bt-map の MMS の push（TYPE:MMS、MIME の本体、大きい物の複数 Put、2 つ目の大きい物は busy、MIME でない物は argument、番号、MMS を受けない phone は no-send）、bt-phoneio の type=mms の長さ（16 MiB まで、type 2 回・他の type は拒否） |
| `sh plan/ws197/tests/phone-backend-host-test.sh` | PASS（新: MMS の line と本体、結果、pipe と fd 無しは EINVAL） |
| `sh plan/ws197/tests/phone-shell-host-test.sh` | PASS（新: version 32 の send_mms が backend に届き fd を compositor が close、done OK、version 27 は EPROTO） |
| `sh plan/ws157/tests/run-host-mediastorage.sh`（host-media-select に libmms を足し、下書きの MIME の試験を追加） | 全 PASS（写真と動画と caption が 1 つの MIME、他の会話の下書きは残る、無ければ ENOENT） |
| zedBSD named build（config/current-uat.mk、BUILD=build/ws197-p010）: libkeiland.so・phone・wayland・bluetoothd・photos・settings | rc 0、warning 0 |
| Linux: libkeiland.so・wayland・mediastorage | rc 0、warning 0 |
| style-check（変えた file）・`git diff --check`・`exports.py --check`・`keiland-os-boundary/check.sh` | 新規 0・PASS |

未実施: 実機（5330＋Android の実際の MMS の送信、写真・動画）。ユーザーの UAT に回す: Phone で会話を開き「＋」→ Photos & Videos で 1〜2 枚 → 文字を書いて Send → timeline に送信中の項目（写真の thumbnail）→ スマホの送信済みに MMS、相手に届く。動画も同じ。失敗の時は項目が Failed と notice。
