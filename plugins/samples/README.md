# AutoConsole v3 Plugin Development

このフォルダには、外部プラグインのサンプルと開発用情報を配置します。

## 概要

AutoConsole v3 はイベント駆動型のプラグインシステムを持ちます。

外部プラグインは DLL として配置することで読み込まれます。

配置先:

```
plugins/installed/
```

## プラグインの役割

プラグインは以下を行えます:

* イベント受信（process / stdout / stderr / custom）
* アクション実行
* 他プラグインの呼び出し

## 基本構造

プラグインは以下の要素を持ちます:

* metadata（id / version / description など）
* initialize
* event handler
* action handler

## エントリポイント

外部プラグインは、Coreからインスタンス生成できるように
明確なエントリポイントを公開する必要があります。

## イベント

代表的なイベント:

* process_started
* process_exited
* stdout_line
* stderr_line
* manual_trigger

イベントはすべてのプラグインに配信されます。

## アクション

CLIまたは他プラグインから呼び出されます。

例:

```
plugin call_plugin sample.echo echo text=hello
```

## デバッグ方法

1. DLLを `plugins/installed/` に配置
2. AutoConsole を起動
3. `plugins` で読み込み確認
4. `plugin info <id>` で metadata 確認
5. CLIまたはイベントで動作確認

## サンプル

* EchoExternalPlugin

  * 最小構成の外部プラグイン
  * metadata / event / action の基本実装

## 注意

* プラグインは失敗しても Core を落とさない設計にすること
* version / apiVersion の互換性を意識すること
* 過度な依存を持たないこと

## 今後

* API連携プラグイン
* Web UI連携
* 外部プロセスプラグイン
