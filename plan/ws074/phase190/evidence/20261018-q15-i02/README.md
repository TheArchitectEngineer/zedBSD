# q15-i02 uncleared / native post-font RAF prerequisite

Strict schema2 runnerはpinned WPT test-wait.jsをruntimeで実行し、単一complete/URL/nonce/readyState/exit/valid paintを要求する。wrong/duplicate/malformed messageと画像だけを拒否し、JS exception単独をcrash扱いしない。parser8 controls/syntax pass。native7 controlsはpermanent waitの非PASSだけが通り、6 positive controlsはreadiness_missing。final runnerでも再確認し、fixed48本実行は前提未達のため行っていない。正式PASS増分0、全48 unknownを維持。

native ordinary load→timer→RAF1→RAF2は実行される一方、同じpageのfonts.ready→RAF1/RAF2はFONTREADYだけで終了することをown API probeで確認。runtime wait scriptの診断もwait_paints到達後に停止。view.c browser_view_settleは最初のpage_settle後にlayout/font readyを解決し、bind_checkpointから予約されたRAFを再pumpしない。loopのbudgetを増やすことで待機条件を弱めない。p209のnative producer修復を次のQueueへ設計する。JS source/FontFaceSetをrunnerでmock/早期解決しない。

全control PPM/HTML/console/rawはbuildに保持。third-party wait script sourceは製品/このarchiveへ転載せず、pinned path/commit/hashを保存した。C/engine/WPT FS/他runnerは不変、q14の1330/3805確認済みを保持。i01 retention clearedの履歴は変更しない。共有projection/Tools登録はpending。
