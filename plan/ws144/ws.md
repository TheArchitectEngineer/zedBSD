<!-- awesome-plan project=zedbsd record=ws144 -->

# WS144: VPN（ブリッジ・トンネルなどの汎用の network の基盤と、選んだ VPN の protocol、Settings の VPN の頁の実体、ベータ2）

<!-- awesome-plan-current:start -->
Status: planning
Primary Milestone: MG005
Related Milestones: MG006
Parent: [Master](../master.md)
Queue: なし（ベータ2 の実装の項目、時期は未定）
Resume point: 未着手。ベータ2 の計画の時に p001（調査と設計、protocol の選定）を Queue に入れる。
Target: **ベータ4 以降**（2026-10-05 user「WS037, WS044,WS048,WS141, WS112, WS118, WS124, WS125, WS126, WS119, WS096, WS097, WS039, WS038, WS144, WS143, WS146,WS147, WS152,  WS119, WS080, は、ベータ4以降としてください。…WS027, WS015, WS047, WS028, WS017,  WS077, はキャンセルします。」）
<!-- awesome-plan-current:end -->

## 単一目標

Settings で stub になっている VPN の頁を実体にする。そのために kernel と networkd に汎用の network の基盤（bridge・tunnel など）を作り、その上に選んだいくつかの VPN の protocol を実装して、Settings から設定・接続・切断できるようにする。

## ユーザーの指示（2026-10-04 夜）

「SettingsでスタブになっているVPNは、ブリッジ、トンネルなどの汎用インフラとともに、いくつかのVPNプロトコルを選んで実装します。WSだけ作っておいてください。これもベータ2です。」

## 範囲（p001 で設計して確定）

- 汎用の基盤: kernel の仮想の interface（bridge、tun・tap に当たる tunnel、必要なら GRE・VXLAN などの encapsulation）、route・policy の扱い、networkd・netconf・`net` の command での設定（O3: networkd・netconf の一貫した仕組み）。
- VPN の protocol: いくつかを選ぶ（候補の例: WireGuard、IPsec/IKEv2、OpenVPN）。選定の基準（license の境界、kernel 内か userland か、暗号の実装の出所と監査、相手の server・service との互換）を p001 でユーザーと決める。
- desktop: libkeiland-backend の VPN の口、compositor の拡張、Settings の VPN の頁（今の stub を置き換え）、system bar の表示。Linux・FreeBSD の Keiland では各 OS の仕組みを backend で包む。
- 外部の実装を取り込む場合の license の監査（Guardrail・設計方針、外部 package は tarball の取得と patch）。IPv6 は [WS130](../ws130/ws.md) と関係（zedBSD の IPv6 はまだ無い）。

## Phase（案）

| Phase | 内容 | Status | 依存 |
| --- | --- | --- | --- |
| ws144-p001 | 調査と設計（汎用の基盤の範囲、VPN の protocol の選定、kernel・userland の分担、desktop の経路、試験の方法） | planning | ベータ2 の計画 |
