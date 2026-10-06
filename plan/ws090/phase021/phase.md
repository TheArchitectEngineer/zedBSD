# ws090-p021: 全ての app の窓の中身の padding を 0 に（title bar と同じ幅、title bar との間は compositor の定数）

Status: planned（2026-10-06 Q1 が作成）
WS: [WS090](../ws.md)
Related: [WS099](../../ws099/ws.md)（compositor の title bar）・[BUG-218](../../bugs/BUG-218.md)（Phone の padding）

## 出典

2026-10-06 ユーザー（Mahora の外観の画面を見て）:「Text Editorのウィンドウの中身が、ウィンドウタイトルバーより幅が小さいので、パディングを入れているように見えます。他のアプリもすべてそうですが、パディング0にして、ウィンドウタイトルバーの幅と、ウィンドウの中身の幅は、同一でお願いします。同様に、ウィンドウタイトルバーとウィンドウコンテントの間のスペースの高さは、すべてのアプリで同一にしたいので、パディングを入れているアプリは修正して0にしてください。アプリ側で0にすれば、コンポジタが定数でその高さを入れることになりますよね。アプリすべてチェックしてください。」

## 範囲

- 全ての app（Text Editor・Terminal・Files・Settings・PDF Viewer・Image Viewer・Notes・Phone・Mailer・Calendar・System Monitor・Video Player・Browser・Music など、libkeiland の glass・panel を使う物と自前で描く物の全て）の窓の中身の左右・上の padding（margin・inset・glass の panel の外側の余白）を 0 にし、中身の幅を title bar と同じにする。
- title bar と中身の間の高さは、app 側を 0 にして compositor の定数だけにする（ユーザーの理解「アプリ側で0にすれば、コンポジタが定数でその高さを入れる」が今の compositor で正しいかを最初に確かめ、違えば compositor の側を定数にする）。全ての app で同じ高さになること。
- libkeiland の共通の部品（glass・panel・cards・list の外側の余白）にある既定の padding も 0 にして、app ごとに残らないようにする。
- 試験: host の描画、QEMU は T1 で全ての app の窓の撮影（title bar と中身の左右の端が揃う、間の高さが同じ）をユーザーが見る。
