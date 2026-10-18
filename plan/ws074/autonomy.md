# WS074 自走の指示（2026-10-18）

ユーザー原文:「では、当分の間、100%を目指して自走をお願いします。」

このchatの直前の文脈から、固定WPT Interop2025の19領域・3,805 unique IDのPASS100%を目標に、Codexが有限Queue（各最大90分）を選定・実行・検証・記録し、次の独立した範囲へ継続する承認として適用する。未実行・runner未対応をPASS扱いせず、分母や受け入れを都合で変えない。必要なrunner整備と失敗の共通原因の修正を含む。対象の拡張・重大なarchitecture/製品判断は既存規則に従う。

単独executorで進め、WS074のsource/tests/plan内に記録する。各Queueの正確な範囲と結果は[Queue](queue.md)、[局所Past Log](history/index.md)に保存する。Master/共有Queueへのdecision/focus/priorityの投影は、WS074のMaster編集禁止と共有Q1の稼働のためpending。GitHubへの公開とpushは既存の保留を維持する。

## 統合しながら継続 / 2026-10-18

ユーザー追加指示（原文）:「マージしながら作業をお願いします。ws.mdには書いていいですが、master.mdには書かないでください。phaseドキュメントには書いていいです。」
このchatのWS074継続作業へ適用: 各有限Queueの検証済みsource/recordsをWIP commitへ保存しmain包含・競合を確認しながら統合を続ける。現在のcheckoutはmainでq44〜q47のsource/terminal commitは全てmain ancestor。既存codex/ws074-acid3はmainとの差がmain側2568/branch側0、未統合commit無し、競合無し。重複/空mergeは作らず、以後別branch成果を統合する場合は対象SHA/差分/checkを固定してWIP統合する。他担当branchの一括mergeやpushは含めない。
ws.mdとPhaseに進捗/統合/指示根拠を記録してよい。master.mdはこのchatから編集しない。共有Master/Queue/cache/GitHub投影は従来どおりQ1所有/pendingで、必要な記録はWS074局所に残す。今回の追加指示は実装範囲/whole acceptance/scoreを変更しない。
