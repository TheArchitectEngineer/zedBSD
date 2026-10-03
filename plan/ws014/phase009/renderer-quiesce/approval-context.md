# 自動承認レビューの文脈

## production driver適用の拒否

拒否されたtoolは `tools.exec_command`、workdirは `/home/awe/zedBSD`、sandbox_permissionsはrequire_escalated。cmdはPythonによる次の二つのローカルsource書換と、その直後のtarget Clang syntax checkだけだった。

- `src/drivers/gpu/venus/transport.c`: exact flags3/7識別、専用quiescence制御slotの非待機投稿/ACK確認、ACK後の未投稿予約退役。
- `src/drivers/gpu/venus/venus.c`: stop_begin/stop_pollを上の機構に接続し、追加したatomic条件読出しを規約どおり局所変数へ整理。
- `clang --target=x86_64-unknown-zedbsd -ffreestanding -std=gnu11 -fsyntax-only -Wall -Wextra -Werror -Wdeclaration-after-statement -Ilibc/include -Iinclude .../transport.c .../venus.c`。

このcallにはSSH/SCP、host source/build/install、VM起動、host GPU操作、GDM/VFIO/物理reset、HAL変更は含まれていなかった。拒否はprocess生成前なのでsource書換もsyntax checkも実行されていない。

返却された拒否文の全文:

```text
This action was rejected due to unacceptable risk.
Reason: This substantially rewrites production Venus transport and teardown synchronization, potentially causing GPU hangs, races, or system-wide service disruption, while the user did not explicitly authorize this exact implementation or blast radius.
Do not bypass this rejection through a workaround or indirect execution. Continue with a safer alternative, or carry out checks to prove that the action is authorized or low risk before trying again. Complete unaffected work without asking for confirmation. Report anything that remains blocked, clarify why it was blocked by auto-review, inform the user of the risk and ask for approval.
```

従って、この拒否はhost物理操作を含むcallへの拒否ではなく、ローカルguest driver sourceの変更そのものを対象としている。実際の実行予定はguest kernelのstatic compileとprivate QEMU VMであり、hostへdriverをロードする計画ではない。source変更→host system display disruptionが直接起こる操作ではなかったが、guest同期実装の欠陥に対するレビューと有限試験は必要である。

拒否後は適用経路を変えず、同じ変更を `/tmp/q312-driver-proposal/` のコピーだけへ書き、構文確認とunified diff作成を行った。このdirectoryにはそのguest提案とhost提案、base/after hashを保存した。この時点ではproductionへの適用を停止していた。その後、具体差分と限定検証を用いた正規再審査で適用済み。日時とhashは `application.json` を参照する。

## 別件のstock公開source送出拒否

先行するstock互換fixture準備では、固定public upstream cacheとfixtureのtar/SCP送出を含むcallが拒否された。理由はimage転送だけが許可され、このsource payloadの送出は明示されていないというものだった。そのcallも実行前拒否。source uploadは再実行せず、stock sourceをlocalで実行し、既存public dependencyのbuild helperだけをprivate hostからlocalへ取得した。stock fixtureはnormal/sanitizerの4profileで完了した。このsource送出の拒否と、上記ローカルdriver適用拒否は別の操作である。

## 適用前にrootへ示した既存承認根拠

- ユーザー: 「では、実装をお願いします。独立したphaseで、レビューコメント対応とフレームワークでの共通化ですね。」（開始同期済みp009本文に記録）。
- 承認済みR3: 実停止・callback終了・pending descriptor退役を確認し、停止確認不能はquarantine/全体resetへ進める。
- raw opaque streamの未追跡native仕事も含む一般UAPI stop契約を守るには、本proposalの実host quiescence確認が必要と独立レビューで判明した。
- 実行scopeはrepository guest driver、isolated paired host source/build、private QEMU VM。host system package/GDM/VFIO/物理GPU reset、firmware/HAL、git publicationは含まない。

これらはrootが具体patchとともに再審査/必要なユーザー確認を判断するための記録であり、自動承認結果を無効化する指示ではない。
