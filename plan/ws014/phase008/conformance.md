# p008: 変更規約と独立レビュー

`plan/coding-style.md` の全文とGuardrailを適用し、今回変更したCコードの配置・宣言・段落・制御フロー・所有権・失敗処理を確認した。過去のp007やユーザーの整形作業を新規成果として数えず、今回必要な差分に適用した。

| 担当範囲 | 確認した規約と境界 |
| --- | --- |
| GPU共通層・driver fence・Venus登録 | public/static順、static前方宣言、型とfile-scope stateの役割コメント、先頭local宣言、処理段落・lock区間、個別の失敗分岐と成功return。GPU所有の型・配置・build単位を確認。 |
| transport・strict host接続 | 新しい予約/commit/cancel、wrapper参照交換、queue範囲・DMA所有権の分岐を確認。意味を持たない圧縮や三条件以上の一行化を整理。外部virglrendererパッチはライセンスとupstream側の書式を保持し、zedBSDのHALへ持ち込まない。 |
| libvulkan | queue/device/context lock順、native受理前のallocation、未受理と不確定失敗の区別、slotごとのpool/cb所有と最終回収、errorの永続性、条件待機を確認。新しいproducer監視threadは作らない。 |
| console/text・実機試験アプリ | 通知登録の型と寿命、public/static順、lock段落、停止・解除・最終returnの目的コメントを確認。テスト用public peerの配置と、SIGSTOP後のfinal closeについて不正確だったコメントを修正。 |
| test/runner | 新規のC testは通常とsanitizerで同じproduction経路をリンクし、productionに試験用env switchを追加しない。Python runnerは独立VM・timeout・attempt・hash・所有PIDの確認を保持。 |

## 独立レビューと修正

- rootは共通GPUのsetup/action observer pin、copyout直後の並行lookup、admit-before-post、exact generation、CONSUMEとcallback再利用、cancelとDMA quarantineを照合した。
- EuclidはU→K境界の回収を確認し、別threadがERROR recordを消費してcontext errorを公開する間にENOENTから成功を返しうる競合を発見した。修正前に失敗する実コードfixtureを追加し、context mutexで回収とerror公開を直列化した後の通常・ASan/UBSan成功をU側の検証記録へ残す。
- Humeはtext registry 137 → console condition 140 → scheduler 200の順、同期unobserve後のstorage寿命、controller mutex外join、主画面だけの所有判定、observe-before-renderと同sequenceでのsleepを確認した。lock rankの説明を新しい通知境界に合わせた。
- Euclidもroot担当の表示・text通知・producer停止試験を独立確認した。機能上の追加指摘はなく、test peerのpublic順とコメントを修正した。
- HumeはWSIのacquire状態とcond登録、Wayland releaseの順序、mutex外dispatch、単一monotonic期限を確認した。pool/cbは同時jobごとに所有し、未完了slotをBeginしないこと、OOM/未受理rollback、partial allocationとjoin後の一回だけの破棄を照合した。
- queue idleはstrict hostのFIFO marker退役により先行sparseも待つ。Vulkanの空submit自体の保証という誤った説明を訂正し、pending sparseを後続ready fenceが追い越さないhost fixtureを追加した。

## 検証の範囲

対象Clangのwarnings-as-errorsと必要なsyntax検査、`git diff --check`、各実コードfixtureの通常・ASan/UBSanを使用する。最終判定と対象source hashは [final-evidence/verification.json](final-evidence/verification.json) にまとめる。formatterの一括適用やaggregate `make check`を規約確認の代替にしていない。正式Vulkan CTS適合や一般Wayland toolkit互換は主張しない。

HAL source/headerの追加変更はない。作業中のユーザーcommitと無関係な変更を保持し、git add/commit/pushは行っていない。GitHub Issues/Projectで同期する計画・受入記録と、ユーザーが行うsource/docのrepository公開は別である。
