<!-- awesome-plan project=zedbsd record=ws089-p021 -->

# ws089-p021: Wi-Fi の画面の自動の scan（Scan のボタンを無くす）と Disconnect の icon

Status: in-progress（q703-i01、P2 generation14、2026-10-05。実装・host 試験済み、QEMU（T1）の試験待ち。実機は UAT）
Disposition: normal
Parent: [WS089](../ws.md)
Queue: q703-i01（P2 generation14）

## ユーザーの要望（2026-10-04 夜、UAT-3 の後、原文）

「・WiFi設定画面で、Scanボタンは不要にしたい。この画面を表示しているならスキャン開始されているべき。画面表示時点ではスキャンのキャッシュがリストされればよい。画面を閉じたらスキャン停止でOK。ただ、アプリが多重起動されていたり、Settingsと右上WiFiメニューが両方表示されている場合は、考慮が必要。スキャン要求はコンポジタがカウントして、libkeiland-backend経由でnetworkdにスキャンオンオフを要求すればいいかも。
・WiFi APの項目のDisconnectボタンは、アイコンにしたい。」

## 範囲

1. Settings の Wi-Fi の頁の **Scan のボタンを無くす**。頁を表示した時点では networkd の scan の cache の一覧を出し、表示している間は scan を続ける。頁を閉じたら（別の頁へ移る・窓を閉じる・最小化は設計で決める）scan を止める。
2. **scan の要求の数え上げ**: Settings の複数の instance や、Settings と system bar の Wi-Fi の menu が同時に表示されている場合を考える。案（ユーザー）: compositor が scan の要求を client ごとに数え（参照の数）、0→1 で libkeiland-backend 経由で networkd に scan の on、1→0 で off を要求する。client が切れた時（crash を含む）は compositor がその client の要求を外す。拡張の protocol（kl_system_manager_v1）に「scan の要求の開始・終了」を足すか、既存の要求で足りるかを設計する（WS131 の境界の規則: app は libkeiland の kl_system_* だけ、OS 固有は libkeiland-backend だけ）。
3. networkd の scan の on・off の口（未接続の時の自動の探索（BUG-158 の 5 秒ごとの search）との関係、接続中の scan の扱い）。
4. AP の行の **Disconnect のボタンを icon に**する（文字の button をやめる、tooltip か accessible な名前は残す）。system bar の menu の同じ物も揃えるかは設計で決める。
5. [BUG-184](../../bugs/BUG-184.md)（オフのクリックが Scan に取られる）は 1 で Scan のボタンが無くなるので、この Phase と一緒に直す（hit の判定の食い違いの根も確かめる）。

## 受け入れ（案）

- 頁を開くと cache の一覧がすぐ出て、scan が回り一覧が更新される。頁を閉じると networkd の scan が止まる（networkd の状態で確かめる）。
- Settings を 2 つ開く・Settings と system bar の menu を同時に開く・片方を閉じる・client を kill する、の各場合で scan の on・off が数え上げどおり。
- Disconnect は icon で、押すと切断する。
- C の全文の規約、build warning 0、OS の境界の checker（`plan/tools/keiland-os-boundary/check.sh`）。QEMU は T1（RTL8822BU の USB passthrough か networkd の模擬）、実機は UAT。

## 依存

WS131 の libkeiland-backend・kl_system_manager_v1（済み）、WS005 の networkd。BUG-183・185・186・187・188（q700）と同じ経路なので、同じ担当が続けて行う。

## 設計（2026-10-05、q703-i01 P2 generation14）

- **D1 数え上げ**: kl_system_network_v1 に request 4 `set_scanning(uint on)`（kl_system_manager_v1 の version 3 から）を足す。compositor は network の object ごとに「scan を求めている」の印を持ち、印の付いた object の数と system bar の Wi-Fi の menu が開いていること（1 つと数える）の和が 0→1 で libkeiland-backend に scan の on、1→0 で off を求める。object の destroy と client の切断（crash を含む）は `zwl_object_destroy` を通るので、そこで印を外す。同じ object の on の繰り返しは 1 つ（印であり数ではない）。libkeiland は `kl_system_network_set_scanning(system, on)`（KL_VERSION 24）、compositor が version 3 未満なら ENOTSUP。
- **D2 libkeiland-backend**: `kl_backend_network_set_scanning(network, on)`。要求（one outstanding）とは別の口で、利用者の switch・join を scan の後ろで待たせない（BUG-184 の「オフが効かない」の一因: Settings の頁の scan が一つだけの要求の枠を占め、off が slot で待った）。zedBSD: scan の専用の接続で、on の間は networkd に `WIFI_SCAN_START`（lease 30 秒、10 秒ごとに更新）と 3 秒ごとの `WIFI_LIST`（新しい scan は KL_BACKEND_NETWORK_CHANGED_SCAN）、off で `WIFI_SCAN_STOP`。Linux・FreeBSD（wpa_supplicant）: on の間 10 秒ごとに `SCAN`（結果は event から）。
- **D3 networkd の scan の on・off**: `NETWORKD_OP_WIFI_SCAN_START`（40）・`_STOP`（41）。lease（`NETWORKD_WIFI_SCAN_LEASE_SECONDS` 30 秒）は compositor が黙って消えた時の守り。lease の間、policy が on で未接続（manual-disconnected）なら 5 秒ごとに各 radio で scan を始める（background の作業なので利用者の要求が来れば止まる）。auto-searching は自分で 5 秒ごとに scan する（BUG-158 の search）ので足さない。接続中の radio は kernel が scan を断る（associated の間 EBUSY）ので、一覧は接続の前の scan のまま。disabled は何もしない。watch の Wi-Fi の行と `net wifi` の状態に `scan=0|1`（受け入れの「networkd の状態で確かめる」）。WIFI_LIST と同じく、受け入れた全ての peer が求められる。
- **D4 Settings**: Scan のボタンを無くす。Network と Wi-Fi の頁を表示している間は set_scanning(1)、他の頁へ移る・窓を閉じると 0。最小化は client に知らされない（xdg の toplevel に戻りの通知が無い）ので数えたまま（未接続の時に 5 秒ごとの scan が続くだけ）。頁を開いた時は compositor の持つ一覧（cache）をすぐ出す。Searching... の表示は scan の要求の間ではなく、一覧がまだ無い間の文にする。
- **D5 system bar**: menu を開いている間を 1 つと数える（開いた時の一回の SCAN の要求をやめる）。
- **D6 Disconnect の icon**: Settings の Wi-Fi の頁の switch の横の文字の Disconnect を icon（切断の印）の button にし、接続中の AP の行の右にも置く。tooltip は無いので accessible な名前として log・hit の名前に「Disconnect」を残す。system bar の menu も文字の button を icon に揃える。

## 経過

- 2026-10-05 q703-i01: networkd（protocol.h の opcode 40・41、`wifi_scan_request`・`run_requested_scan`・lease、watch の `scan=`）と libkeiland-backend（header、zedBSD の scan の接続、wpa の SCAN）を実装、networkd と backend の object は build warning 0（`ZEDBSD_CONFIG=plan/ws089/tests/config-amd64-settings.mk BUILD=build/p2-q703`）、wpa は host の cc で compile。未試験。同じ commit に、保留にした BUG-185・187・189 の networkd・backend の途中の差分が入る（各 ticket の「保留」）。
- 2026-10-05 q703-i01（続き）: compositor（`system.c` の `set_scanning`、`objects.c` の destroy の hook `zwl_system_network_gone`、`network.c` の `zwl_network_scan_hold`（holders の数え上げ、menu を開いている間を 1 つ、開いた時の SCAN の要求をやめ、空の一覧は最初の scan まで「Looking for networks...」）、Disconnect を `GLASS_ICON_CLOSE` の icon の button（26×22）に）、protocol（`kl-system-protocol.h` の manager version 3・network の request 4、libkeiland の `kl_system_network_set_scanning`・KL_VERSION 24・exports.map）、Settings（Scan のボタンと周期の scan の要求を削除、Network・Wi-Fi の頁の間 `set_scanning(1)`、Ethernet・他の頁・窓を閉じると 0、「Searching...」は最初の scan まで、接続中の AP の行に Disconnect の icon の button（`se_icon_button_draw`・`SE_GLYPH_DISCONNECT` の丸に×）、Wi-Fi の頁の見出しの文字の Disconnect を削除）、network-probe（op=40・41、`scan=`、`route default=em9`）。
- 確認（host）: `sh plan/ws089/tests/host-slot.sh` PASS（scan を占める要求に使っていた case を join・switch・disconnect に替え、case 5 に頁ごとの set_scanning の on・off と close を足した）、`sh plan/ws131/tests/host-system.sh build/p2-q703/host-system` PASS（set_scanning の一つの object が 1 holder、off、kl_system_close で 0 を足した。stub の destroy に objects.c と同じ hook）、`sh plan/ws089/tests/host-build.sh` と settings-render の Wi-Fi・Network の頁の絵（接続中の行に icon、click=1036,296 で NETWORK disconnect）。build: zedBSD の `bin/wayland`・`bin/settings`・`bin/networkd`・`bin/network-probe`・`dynamic/libkeiland.so`（`ZEDBSD_CONFIG=plan/ws089/tests/config-amd64-settings.mk BUILD=build/p2-q703`）warning 0、Linux（`make -f userland/desktop/keiland-linux.mk KEILAND_LINUX_BUILD=build/p2-q703-linux all`）warning 0。FreeBSD は未実施（ユーザー 2026-10-05: Linux・FreeBSD の backend はベータ1 まで不要）。OS の境界の checker: B2 以外 PASS、B2 は compositor の `kl_motion_*` を許可の表（`keiland_motion_` の旧い名前）が知らないための FAIL で、この変更の前から（Q1 に報告）。
- BUG-184 の根（確かめた）: hit の表は毎 frame に描いた位置で作り直し、press と release は同じ frame の表で引く（`ui.c` の `ui_hit_at`）。Wi-Fi の頁の switch（1070,135 52×32）と Scan（二つ目の card の見出し）の矩形は重ならない（settings-render の `hits`）。食い違いではなく、頁が 20 秒ごとに出す Settings 自身の scan の要求が一つだけの要求の枠を占め、その間の off は slot で待ち（switch は state のまま）、見出しが「Searching...」・Scan が押せない形になっていた。scan を要求の枠から外した（D2・D4）ので、この形は無くなる。
- 試験の依頼（T1、Q1 経由）: `plan/ws089/tests/settings-p021.sh`（image は `plan/ws089/tests/build-settings-image.sh BUILD`、settings-guest.sh の guest）。networkd の変更（scan の lease の on・off、watch の `scan=`・`route default=`）は part 1 の実 networkd で確かめる。manual-disconnected の時の 5 秒ごとの scan は radio の要る確かめで、QEMU（radio 無し）では見られない（RTL8822BU の passthrough か実機の UAT）。

## 追加の仕様（2026-10-05 ユーザー）

「scan の要求の数え上げですが、アプリが要求を下げる前にクラッシュした場合に、要求が取り下げできない設計になっていると思いました。1つのスキャン要求は1分だけ有効にしましょう。」→ compositor が数える scan の要求（holder）は、**1 つ 1 分だけ有効**にする。要求した client は表示している間 1 分より短い間隔で要求を出し直し（更新）、更新が無い要求は 1 分で compositor が外す。client の切断（crash を含む）で外す今の仕組み（objects.c の destroy の hook）も残す（二重の守り）。networkd への lease（30 秒）は compositor が holders > 0 の間更新する今のまま。system bar の menu の要求も同じ規則。試験（settings-p021.sh）に「更新を止めた client の要求が 1 分で外れる」を足す。
- 2026-10-05 q703-i01（追加の仕様、1 分の有効期限）: compositor の `system.c` に `SYSTEM_SCAN_MS`（60 秒）、`set_scanning(1)` のたびに object の `network_scanning_until` を今＋60 秒にし、`zwl_system_tick` の `system_scanning_expire` が期限の過ぎた object の印を外す（`ZWL SYSTEM scanning expired`）。切断の hook は残す。Settings は表示の間 30 秒ごとに `set_scanning(1)` を出し直す（`NETWORK_SCANNING_RENEW_MS`）。system bar の menu の holding は compositor 自身の物で、menu を開いている間だけ（compositor より長く残れないので期限は要らない、同じ規則の意図を満たす）。protocol と keiland.h の文に期限を書いた。host: `host-system.sh` に時計を 61 秒進めて期限で外れる check、`host-slot.sh` に case 10（30 秒ごとの出し直し）、どちらも PASS。`settings-p021.sh` の part 1 に SIGSTOP で更新を止めた Settings の要求が 1 分で外れ（holders=0、`net watch` の scan=0）、SIGCONT ですぐ出し直す（holders=1）確かめを足した。
