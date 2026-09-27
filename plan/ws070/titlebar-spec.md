<!-- awesome-plan project=zedbsd record=ws070-titlebar-spec -->

# zedBSD Titlebar Presentation Specification（仕様案の原文）

2026-09-27 ユーザーが WS070 への仕様の追加として示した「zedBSD Floating Titlebar / Docked System Bar Presentation Specification のたたき台」。
ユーザー:「WS070に仕様追加します。WS071のサブエージェントでスケジューリングするのがいいと思います。」以下は原文（Markdown の見出しだけ整えた）。

---

## 1. 概要

zedBSD のウィンドウタイトルバーは、単なるウィンドウ装飾ではなく、アプリケーションが公開する主要操作を表示する system-owned presentation surface として扱う。
クライアントはタイトルバーを直接描画しない。代わりに、

- アプリケーション識別情報
- メニューモデル
- コントロールモデル
- タブモデル

などの意味情報を shell / compositor に提供する。
compositor は、その情報を通常時には Floating Titlebar、最大化時には System Bar Application Zone に表示する。
タイトルバーの主要コンテンツは、原則として以下の3種類の presentation mode のいずれかひとつを選択する。

```
MENU
CONTROLS
TABS
```

これらは相互排他的とする。

## 2. 目的

本仕様の目的は以下である。

### 2.1 一貫したタイトルバーUI

アプリケーションごとに異なるclient-side decorationを避け、タイトルバーの、

- サイズ
- 色
- blur
- typography
- spacing
- input behavior
- maximize transition

をsystem-wideで統一する。

### 2.2 「1バー1役割」

メニュー、検索UI、タブ列などを同時に詰め込まず、タイトルバーの主要用途を明確にする。
これにより、

- 視認性
- 操作性
- 最大化時の再配置
- 狭い画面での縮退

を単純化する。

### 2.3 最大化時のSystem Bar統合

Floating Titlebar の内容は最大化時に失われず、System Bar内へ再配置される。
クライアント側は表示先を意識しない。

## 3. タイトルバーの基本構造

タイトルバーは概念上、以下の3領域から構成される。

```
┌──────────────────────────────────────────────────────┐
│ Identity │ Presentation Content │ Window Management │
└──────────────────────────────────────────────────────┘
```

**Identity Zone**: アプリケーションまたはウィンドウの識別情報。例: app icon、app name、window title

**Presentation Zone**: アプリ固有の主要操作領域。ここに `MENU`、`CONTROLS`、`TABS` のいずれかひとつを表示する。

**Window Management Zone**: OSが所有するウィンドウ操作。minimize、maximize / restore、close

## 4. Presentation Mode

### 4.1 MENU Mode

メニューバーを主表示とするモード。例:

```
[Terminal]   Shell   Edit   View   Session   Help      — □ ×
```

主用途: Terminal、traditional desktop applications、editor、IDE、utility applications

MENU Mode のPresentation Zoneには、原則としてトップレベルメニュー項目のみ表示する。
各項目は、text、optional icon、submenu、enabled state、shortcut、semantic role を持つことができる。

### 4.2 CONTROLS Mode

ナビゲーションや検索などの操作コントロールを主表示とする。例:

```
[←] [→] [⌂] Home       [ Search... ]       [Grid] [List]    — □ ×
```

主用途: file manager、settings、store、media browser、content-oriented applications

配置可能な代表的control: back、forward、home、breadcrumb、search、view selector、sort、filter、primary action、overflow menu

### 4.3 TABS Mode

タブ管理を主表示とする。例:

```
[ Site A × ] [ Site B × ] [ + ]                       — □ ×
```

主用途: browser、terminal multiplexer、document editor、multi-document applications

TABS ModeではPresentation Zoneの大部分をtab stripが占有する。

## 5. 排他性

`MENU`、`CONTROLS`、`TABS` は同時に有効化してはならない。つまり以下は禁止する。

```
File Edit View   [Search...]   [Tab 1] [Tab 2]
```

理由: 過密化、レイアウト優先順位の複雑化、System Bar docking時の破綻、touch target不足、narrow window対応の難化

## 6. 共通要素

presentation modeに関係なく、以下は配置可能とする。

- application icon
- application name
- window title
- window controls
- overflow button
- optional primary status indicator

これらはPresentation Zoneとは別に扱う。

## 7. MENU Mode詳細

### 7.1 Top-level item

例: File、Edit、View、Window、Help

トップレベル項目は基本的にtext-onlyとする。アイコン使用はoptionalとするが、常用は推奨しない。

### 7.2 Popup

menu itemをactivateすると、compositorがsystem menu popupを表示する。popup presentationはcompositorが所有する。

### 7.3 Overflow

幅不足時は優先順位の低いmenu itemを `[…]` にまとめてもよい。

## 8. CONTROLS Mode詳細

各controlは具体的なwidgetではなく、semantic roleとしてクライアントから公開されることが望ましい。例:

```
BACK
FORWARD
HOME
BREADCRUMB
SEARCH
VIEW_GRID
VIEW_LIST
SORT
FILTER
PRIMARY_ACTION
OVERFLOW
```

compositorはroleに応じて、icon、size、spacing、compact presentation を決定する。

## 9. Search Control

検索はCONTROLS Mode専用の代表的controlとする。状態:

```
inactive
focused
query-active
```

幅不足時には `[ Search field ]` から `[🔍]` へ縮退可能とする。

## 10. Breadcrumb

breadcrumbは階層ナビゲーションを表現する。例: `Home > Projects > zedBSD > src`

幅不足時は `… > zedBSD > src` のように省略できる。

## 11. TABS Mode詳細

各tabは最低限、title、optional icon、active state、attention state、closable、identifier を持つ。例:

```
[ README.md × ] [ main.c × ] [ + ]
```

タブの具体的な見た目はcompositor / system themeが決定する。

## 12. 幅不足時の縮退

Presentation Zoneが利用可能幅を超えた場合、compositorは段階的にcompact化する。推奨順序:

- MENU: 1. spacing縮小 2. 低優先度itemをoverflowへ 3. app title省略
- CONTROLS: 1. breadcrumb省略 2. searchをicon化 3. secondary controlをoverflowへ 4. primary navigationのみ残す
- TABS: 1. tab幅縮小 2. title省略 3. tab scrolling 4. tab overview buttonへ集約

## 13. 通常状態

通常ウィンドウではFloating Titlebarを表示する。例:

```
        ┌──────────────────────────────────────────┐
        │ [T] Terminal  Shell Edit View Help — □ × │
        └──────────────────────────────────────────┘
        ┌──────────────────────────────────────────┐
        │ application content                      │
```

titlebarはwindow bodyとは視覚的に分離してもよい。

## 14. 最大化状態

ウィンドウが最大化されるとFloating TitlebarはSystem Barにdockされる。例:

```
[zedBSD] | [T] Terminal Shell Edit View Help | — ▣ × | desktops | status
```

またはFile Manager:

```
[zedBSD] | ← → ⌂ Home [Search] [Grid/List] | — ▣ × | desktops | status
```

Floating Titlebarの独立背景は消える。

## 15. Docking時の原則

最大化時、

- controlsは失われない
- semantic identityは維持される
- maximizeはrestoreへ変わる
- System Barのsystem-owned領域は侵食しない
- presentation zoneのみapplication-owned modelを表示する

## 16. System Bar Zones

最大化時のSystem Barは以下に分ける。

```
System Identity
Application Zone
Window Management
Workspace Zone
System Status Zone
```

例:

```
[zedBSD]
    |
[App + Controls]
    |
[— ▣ ×]
    |
[Desktops]
    |
[Network Battery Clock]
```

## 17. Docking Animation

最大化時は以下の連続遷移を推奨する。

1. window bodyが拡大
2. floating titlebarが上方向へ移動
3. shadowが減少
4. corner radiusが減少
5. titlebar contentがSystem Bar内のApplication Zoneへ移動
6. floating titlebar surfaceが消える

遷移時間の目安: 200–300 ms

## 18. Restore Animation

restore時は逆方向のanimationを行う。

1. Application Zoneの内容が下方向へ移動
2. Floating Titlebarの背景が形成
3. corner radiusとshadowが復帰
4. window bodyが元のgeometryへ戻る

## 19. Drag / Swipe

Docked Titlebar状態でもApplication Zoneの特定領域はwindow drag targetとして扱える。
最大化時: downward drag、downward swipe によってrestore操作を開始できる。
これにより、`pull window out of system bar` という空間モデルを維持する。

## 20. Double Click

Docked Titlebar領域をdouble-clickするとrestoreできる。
Floating Titlebarをdouble-clickするとmaximizeできる。
両者は対称な操作とする。

## 21. Client API Model

クライアントはpresentation modeを宣言する。概念例:

```
titlebar.set_mode(MENU)
titlebar.set_menu(menu_model)
```

または、

```
titlebar.set_mode(CONTROLS)
titlebar.set_controls(control_model)
```

または、

```
titlebar.set_mode(TABS)
titlebar.set_tabs(tab_model)
```

同時に複数modelを設定しても、active mode以外は表示しない。
より厳密には、active modeと不整合なmodel設定をprotocol errorとしてもよい。

## 22. Runtime Mode Switching

アプリは実行中にmodeを変更できる。例: `CONTROLS → TABS`

ただしmode変更はtransaction単位でatomicに行うことを推奨する。例:

```
begin_update()
set_mode(TABS)
set_tabs(...)
commit()
```

途中状態を表示しない。

## 23. System Menuとの関係

MENU Modeでは、先に定義した`xdg_toplevel_menu_v1`相当のmenu modelを利用できる。CONTROLS / TABSとは別モデルとする。
つまり概念的には、

```
Titlebar Presentation
 ├─ MENU     → menu model
 ├─ CONTROLS → control model
 └─ TABS     → tab model
```

とする。

## 24. Security / Trust Model

クライアントは任意のpixelsをタイトルバーに描画できない。指定可能なのはsemantic modelのみ。
クライアントは以下を直接指定しない: font、color、blur、shadow、corner radius、exact pixel position、arbitrary compositor surface

これらはshell policyが決定する。

## 25. Accessibility

semantic modelをcompositorが保持するため、screen reader labels、keyboard navigation、high contrast、larger controls、touch sizing をsystem-wideで統一できる。

## 26. Input Device Adaptation

同じモデルでも、input deviceに応じてpresentationを変更できる。

- pointer mode: compact spacing、small controls
- touch mode: larger hit targets、larger tab height、larger popup rows

クライアントはこれを意識しない。

## 27. Fallback

本拡張をサポートしないcompositorでは、toolkitまたはclientが通常のclient-side titlebar / menu / tabsを使用してよい。

## 28. 設計原則

本仕様の中心原則は以下である。

**One titlebar, one primary presentation role.** 日本語では、ひとつのタイトルバーは、ひとつの主要な役割だけを持つ。

そして、

**The client provides semantics; the shell provides presentation.** つまり、クライアントは意味を提供し、shellが見た目と配置を決定する。

この2つを柱にすると、zedBSDのFloating TitlebarとSystem Bar Dockingの仕様がかなり一貫します。
