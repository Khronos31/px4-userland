# Alpine Linux / BusyBox mdev での構成例

> **検証済み構成について:** 本文書は以下の環境で動作確認した構成例です。将来のOS更新への追従や継続的な保守は保証しません。

## 検証対象

- 検証日: 2026-09-08
- `px4-userland`: commit `69ae0e056abb1f3a7291c41cb8836d2c4a2bde1e`, version `0.1.2`
- Alpine Linux 3.24.1, x86_64, musl 1.2.6, kernel 6.18.49-0-lts
- BusyBox mdev 1.37.0, OpenRC, pcsc-lite 2.4.0, OpenSC 0.26.1

Alpine ではホットプラグを mdev で処理し、起動時のコールドプラグ（既存デバイス検出）は `mdev -s` の後に sysfs スキャンヘルパーを実行して処理します。`mdev -s` のコマンド一致処理には `DEVTYPE` と `PRODUCT` が渡されないため、起動時のスキャンを省略しないでください。

## USB ノードの権限

配布物に含まれる mdev 設定行を、汎用の USB / `$MODALIAS` ルールより前に `/etc/mdev.conf` へ追加します。

```text
DEVTYPE=usb_device;PRODUCT=511/84a/.*;bus/usb/[0-9]+/[0-9]+ root:video 0660
```

ヘルパーと OpenRC の local フックを配布物から配置します。

```sh
addgroup '<user>' video
install -d -m 0755 /usr/local/libexec /usr/local/share /etc/local.d
install -m 0755 mdev/px4-userland-mdev.sh /usr/local/libexec/px4-userland-mdev
install -m 0755 mdev/px4-userland-mdev.start /etc/local.d/px4-userland-mdev.start
install -m 0644 mdev/px4-userland-mdev.conf /usr/local/share/px4-userland-mdev.conf
```

`/etc/mdev.conf` の先頭に上記設定行を追加し、`local` サービスを有効化します。local フックにより、起動時に `mdev -s` の実行後スキャンヘルパーが呼び出されます。接続済みのデバイスへ即座に反映する場合は以下を実行します。

```sh
rc-update add local default
mdev -s
/usr/local/libexec/px4-userland-mdev --scan
```

対象ノードのパーミッションが `root:video` かつ `0660` であることを確認後、`px4d`、`px4-ts`、`px4ctl` を `<user>`（一般ユーザー）で実行します。デバイス再接続時は mdev のホットプラグ設定によって自動で適用されます。

## PC/SC

Alpine では musl 版配布アーカイブ（`px4-userland-<version>-linux-musl-x86_64.tar.gz` または `px4-userland-<version>-linux-musl-aarch64.tar.gz`）に含まれる musl 版 IFD ハンドラ（`ifd/px4-userland-ifd.so`）を使用します。動作確認済みのパッケージは `pcsc-lite` と `pcsc-lite-openrc` です（`opensc-tool` で確認する場合は `opensc` も追加します）。

```sh
apk add pcsc-lite pcsc-lite-openrc
apk add opensc
addgroup pcscd video
rc-update add pcscd default
```

リーダー設定を `/etc/reader.conf.d/px4-userland.conf` に配置し、各プレースホルダーを実環境の値に置き換えます。

```text
FRIENDLYNAME "PLEX PX-Q3U4 Internal Card Reader"
DEVICENAME   px4-userland:runtime=<runtime-dir>:device=<base-serial>:access=group
LIBPATH      <ifd-library>
CHANNELID    0
```

`<runtime-dir>` は `px4d` と PC/SC が共有するディレクトリ、`<base-serial>` は対象 PX-Q3U4 の 14 桁ベースシリアル、`<ifd-library>` は musl 版 IFD ハンドラの絶対パスです。今回の検証構成では `px4d` を `pcscd` アカウントで実行したため、同アカウントに USB ノードを開くための補助グループ `video` を付与しています（`pcscd` 自体が USB ノードを開くわけではありません）。

`/run/px4-userland` は揮発性のため、起動ごとに `pcscd` と共有可能な所有者・パーミッションで作成します。グループモードでの運用例では、`pcscd` アカウントで `px4d --group` を先に起動してから `pcscd` を起動（または再起動）し、リーダーの認識完了後に一般ユーザーの PC/SC クライアントを実行します。なお `px4d` 用の OpenRC サービス定義は同梱していないため、システムの起動時に `px4d` を自動起動させる場合は別途サービス登録を行ってください。

## 実機で確認したこと

Alpine Linux x86_64 実機環境にて、一般ユーザー権限で PX-Q3U4 の両 USB ブリッジへアクセスできることを確認しました。musl 版 IFD ハンドラが Alpine の pcsc-lite に正常に読み込まれ、一般ユーザーの PC/SC クライアントから内蔵リーダーの ATR および SW9000 応答を取得できること、8 レシーバー同時の MPEG-TS 受信、カードへの直接 APDU 送受信、ならびにプロセス終了後のクリーンアップが正常に行われることを確認済みです。
