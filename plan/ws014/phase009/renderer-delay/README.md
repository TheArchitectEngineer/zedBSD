# 実native完了の通知を遅らせる独立テスト成果物

このpatchはp009の有限故障試験だけに使う。zedBSD production、p008のstrict pair、通常VM受入用library/serverは変更しない。実GPUの計算を15秒に延ばしたものではなく、実native VkFenceのSUCCESSを確認した後、Kへの完了通知を15秒遅らせる。CPU0 decoderや他contextのqueueを待たせない。

## 実装境界

固定p008 strict `vkr_queue_thread` の実WaitForFencesがSUCCESSを返した直後、queue sync mutexを再取得する前へfixture専用関数を挿入する。native queue mutexもこの位置では保持されていない。環境 `Q312_COMPLETION_GATE` の絶対pathを `unlink` できた1workerだけがgateを取得し、CLOCK_MONOTONICで実測して15秒nanosleep後に従来のretireへ進む。EINTRでは残り時間を待つ。native statusを改変せず、失敗・pendingに通知を捏造しない。

harnessだけが当該run/capture内の通常gate fileを作る。全guest context初期化後の `GPUFENCE DELAY_READY` に合わせて作成し `armed` を送る。Aだけがsubmitした後、harnessはacquired markerを確認して `running` を送る。その後Bは別contextで継続する。gate fileを先に常設すると別の初期化jobが取得し得るので、この二段階同期を必須にする。

```text
Q312_COMPLETION_DELAY acquired pid=P context=C fence=F native=SUCCESS duration_ms=15000
Q312_COMPLETION_DELAY released pid=P context=C fence=F elapsed_ms=T
```

PID/context/fenceの一致、acquired/released各1件、T>=15000、failed markerなしを要求する。guest側でもAの期待結果、Bの20秒以上の進行・実buffer内容、cleanup/子process回収を確認する。ログのacquiredだけでは完了成功としない。短い実行期限ケースはAが先に論理ERRORとなり、15秒後にbackend所有権が退役することを検証する。未完了native処理を強制cancelした証拠ではない。

## 配置と再生成

production baselineは `dependencies/q311-strict`、試験専用copyはprivate hostの `dependencies/q312-delay/{source,build,install}`。同じp008 sourceへ本patchだけを追加する。library/server/source hashはprovenance.json。環境hookはこの独立テスト成果物だけに存在し、productionへ取り込まない。

```sh
# Private hostの既存Meson依存を利用し、この隔離prefixだけに構築する。
PYTHONPATH=/home/awe/zedbsd-q306-venus/dependencies/q310-opaque/python \
  python3 -m mesonbuild.mesonmain setup \
  /home/awe/zedbsd-q306-venus/dependencies/q312-delay/build \
  /home/awe/zedbsd-q306-venus/dependencies/q312-delay/source \
  --prefix=/home/awe/zedbsd-q306-venus/dependencies/q312-delay/install \
  -Dvenus=true -Dplatforms=egl -Dtests=false -Dvideo=false \
  -Drender-server-worker=process -Dminigbm_allocation=false -Dbuildtype=release
PYTHONPATH=/home/awe/zedbsd-q306-venus/dependencies/q310-opaque/python \
  timeout 1200 ninja -C /home/awe/zedbsd-q306-venus/dependencies/q312-delay/build -j2
PYTHONPATH=/home/awe/zedbsd-q306-venus/dependencies/q310-opaque/python \
  python3 -m mesonbuild.mesonmain install \
  -C /home/awe/zedbsd-q306-venus/dependencies/q312-delay/build --no-rebuild
```

初回ninjaは生成子processへのPYTHONPATH継承不足でC compile前に停止した。原因を修正した再実行は全90stepとinstallが成功した。system package/ldconfig/GDM/VFIO/host GPU resetには触れない。上流MIT licenseを維持し、追加fixture patchを隔離して保存する。実VM結果は同Phaseのintegration evidenceを正本とする。

## Quiescence対応pairへの再構築

上のq312-delayは旧strict3の歴史的試験artifactとして保持する。最終p009用には同じdelay patchを新flags7 sourceへ適用した `/home/awe/zedbsd-q306-venus/dependencies/q312-quiesce-delay/install` を別途構築した。gate/env/実nativeSUCCESS後15秒遅延の契約とqueue source hashは同じで、all-device quiescence ACKを持つ。新pairの正確なlibrary/server/source hash、process worker構成、INIT結果は `../renderer-quiesce/evidence/build-delay.json` と `../renderer-quiesce/evidence/handshake-delay.json` に保存した。旧prefixを上書きせず、root統合VMは新prefixを使用する。
