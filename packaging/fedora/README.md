# Fedora での構成手順（SELinux Enforcing）

本文書は、Fedora 44 / SELinux Enforcing 環境において、PLEX PX-Q3U4（USB ID `0511:084a`）を systemd サービス、SELinux ポリシー、および PC/SC（pcsc-lite / Polkit）と連携させて安全に運用するための導入手順です。

配布物の Linux CLI（`px4d`、`px4-ts`、`px4ctl`）は完全静的バイナリ、IFD Handler（`ifd/px4-userland-ifd.so`）は glibc 版を使用します。初期導入やシステム設定には `sudo` が必要ですが、導入後の日常利用（状態確認、TS 受信、カード操作）は一般ユーザー権限で実行できます。

## 1. 依存パッケージの導入

実行時および SELinux ポリシーのビルドに必要なパッケージをインストールします（ビルド済み配布物を利用する場合、`pcsc-lite-devel` は不要です）。

```sh
# 実行時依存パッケージ（PC/SC・動作確認用ツール・Polkit・USBツール）
sudo dnf install pcsc-lite opensc polkit usbutils

# SELinux ポリシービルド・管理用パッケージ
sudo dnf install selinux-policy-devel policycoreutils-devel
```

## 2. バイナリとファームウェアの配置

配布アーカイブから実行ファイルおよび IFD Handler を `/opt/px4-userland` 配下へ配置します。

```sh
sudo install -d -m 0755 /opt/px4-userland /opt/px4-userland/ifd /opt/px4-userland/firmware
sudo install -m 0755 px4d px4-ts px4ctl /opt/px4-userland/
sudo install -m 0755 ifd/px4-userland-ifd.so /opt/px4-userland/ifd/
```

IT930x ファームウェアは同梱されていません。別途用意し、ファイルサイズ（2,169 バイト）および SHA-256（`5213a5a38872661277a2cc1b2dfdfe88faf06f41205f460f3b51857f0568b484`）を確認した上で、所有者 `root:pcscd`、パーミッション `0640` で配置します。

```sh
sudo install -o root -g pcscd -m 0640 /path/to/it930x-firmware.bin \
  /opt/px4-userland/firmware/it930x-firmware.bin
```

## 3. システム設定ファイルの配置

リポジトリ内の Fedora 向け資材を配置します。

- サービスアカウント（`px4d` を作成し `pcscd` グループに追加）: `packaging/fedora/sysusers.d/px4-userland.conf`
- systemd テンプレートユニット: `packaging/fedora/systemd/px4d@.service`（`User=px4d`, `Group=pcscd`, `SupplementaryGroups=video`, `NoNewPrivileges=yes`, `ProtectSystem=strict`、`PrivateDevices` は不使用）
- Polkit ルール（`org.debian.pcsc-lite.access_pcsc` と `access_card` を `pcscd` グループにのみ許可）: `packaging/fedora/polkit/50-px4-userland.rules`

```sh
# サービスアカウント設定
sudo install -D -m 0644 packaging/fedora/sysusers.d/px4-userland.conf \
  /usr/lib/sysusers.d/px4-userland.conf
sudo systemd-sysusers /usr/lib/sysusers.d/px4-userland.conf

# systemd サービスユニット
sudo install -D -m 0644 packaging/fedora/systemd/px4d@.service \
  /etc/systemd/system/px4d@.service

# Polkit ルール
sudo install -D -m 0644 packaging/fedora/polkit/50-px4-userland.rules \
  /etc/polkit-1/rules.d/50-px4-userland.rules

# udev ルール（PX-Q3U4 へのアクセス権限を video グループに付与）
sudo tee /etc/udev/rules.d/70-px4-q3u4.rules << 'EOF'
SUBSYSTEM=="usb", ENV{DEVTYPE}=="usb_device", ATTR{idVendor}=="0511", ATTR{idProduct}=="084a", MODE="0660", GROUP="video"
EOF
sudo udevadm control --reload-rules
```

> [!NOTE]
> udev ルールを再読込した後、設定を反映させるために PX-Q3U4 の USB ケーブルを物理的に挿し直してください。

## 4. 14 桁 base serial の取得

PX-Q3U4 を接続した状態で `lsusb` を実行し、デバイスのシリアル番号を確認します（udev やグループの反映前でも確実に descriptor を読めるよう `sudo` を付与します）。

```sh
sudo lsusb -v -d 0511:084a 2>/dev/null | grep iSerial
```

出力例:
```text
  iSerial                 3 000012050009601
  iSerial                 3 000012050009602
```

PX-Q3U4 には 2 系統の USB ブリッジが存在し、末尾が `1` および `2` となる 15 桁の `iSerial` を持ちます。この共通する先頭 14 桁を、以降の設定やサービス起動で使用する `<base-serial>` とします。

確認した 14 桁の値をシェル変数 `base_serial` に設定します（以下の値をご自身のデバイスの 14 桁シリアルに置き換えて実行してください）。

```sh
base_serial="00001205000960"
```

## 5. PC/SC リーダー設定

`packaging/pcsc/reader.conf.d/px4-userland.conf.in` を元に、Fedora の reader 設定ディレクトリ `/etc/reader.conf.d/` へ設定を作成します。先ほど設定した `${base_serial}` が展開されます。

```sh
sudo install -d /etc/reader.conf.d
sudo tee /etc/reader.conf.d/px4-userland.conf << EOF
FRIENDLYNAME "PLEX PX-Q3U4 Internal Card Reader"
DEVICENAME   px4-userland:runtime=/run/px4-userland:device=${base_serial}:access=group
LIBPATH      /opt/px4-userland/ifd/px4-userland-ifd.so
CHANNELID    0
EOF
```

## 6. SELinux ポリシーのビルドと適用

SELinux ポリシーソース（`packaging/fedora/selinux/px4d.te`、`px4d.fc`）からモジュールをビルドしてインストールします。

このポリシーにより専用ドメイン `px4d_t` が定義され、USB デバイスノード、sysfs、libudev db、netlink kobject uevent ソケット、ファームウェア（`px4d_data_t`）、および専用ランタイムソケット（`px4d_var_run_t`）に対し、Fedora 参照ポリシーの既存 interface を用いて必要なアクセスを許可します。また、標準の `pcscd_t` から専用ソケットへの UNIX ドメインソケット接続が許可されます。

```sh
cd packaging/fedora/selinux
make -f /usr/share/selinux/devel/Makefile px4d.pp
sudo semodule -i px4d.pp
sudo restorecon -RFv /opt/px4-userland
```

クリーンインストール時点ではランタイムディレクトリ `/run/px4-userland` はまだ存在しません。SELinux ポリシーの `files_pid_filetrans` および `.fc` 定義により、サービス（`px4d@.service`）起動時に systemd の `RuntimeDirectory` 経由で正しいコンテキスト（`px4d_var_run_t`）で自動作成され、サービス起動後の確認対象となります。

## 7. サービスの起動

systemd の設定を再読込し、`pcscd.socket` と PX-Q3U4 のデーモンサービス（`px4d@<14桁base-serial>.service`）を有効化して起動します。

```sh
sudo systemctl daemon-reload
sudo systemctl enable --now pcscd.socket
sudo systemctl enable --now "px4d@${base_serial}.service"
```

`--runtime-dir /run/px4-userland` は専用の安全なランタイムルートです。実際のエンドポイントは `/run/px4-userland/px4-userland/<base-serial>/` 配下に作成されます（同名が 2 段になるのは現行 API 契約通りの仕様です）。サービス停止時には systemd の `RuntimeDirectory` 管理によりランタイム階層ごと自動削除されます。

## 8. 一般ユーザーの設定と利用

SSH 等の非アクティブセッションから PC/SC を利用する一般ユーザーは、`pcscd` グループ（補助グループ）に追加します。

```sh
sudo usermod -aG pcscd "$USER"
```

グループ所属の変更を反映するため、一度ログアウトして再ログインしてください。再ログインにより前段のシェル変数はリセットされるため、再度 `base_serial` を設定します（取得済みの 14 桁シリアルを指定）。

```sh
base_serial="00001205000960"
```

一般ユーザーからグループモードでアクセスする場合、`--runtime-dir /run/px4-userland` と `--group` を指定します。`sudo` は不要です。

```sh
/opt/px4-userland/px4ctl --device "${base_serial}" --runtime-dir /run/px4-userland --group status
```

## 9. 動作確認

導入後の確認コマンド例です。

```sh
# 1. SELinux が Enforcing であることを確認
getenforce

# 2. サービスの稼働状態を確認
systemctl status "px4d@${base_serial}.service"

# 3. プロセスドメインを確認（px4d が px4d_t、pcscd が pcscd_t）
ps -eZ | grep -E 'px4d|pcscd'

# 4. デバイス状態およびカードリーダー状態を確認（一般ユーザー権限）
/opt/px4-userland/px4ctl --device "${base_serial}" --runtime-dir /run/px4-userland --group status
/opt/px4-userland/px4ctl --device "${base_serial}" --runtime-dir /run/px4-userland --group card-status

# 5. PC/SC リーダーの認識とカード応答を確認
pcsc_scan
```

## トラブルシューティング

### 過去のローカル SELinux fcontext ルールとの競合

クリーンインストールのファイルコンテキスト定義はモジュールの `.fc` が正本です。過去に同一のランタイムパスに対してローカルで `semanage fcontext` override を手動作成していた場合は、競合を解消するために override を削除し、ランタイムディレクトリが存在するときだけラベルを再適用してください。

```sh
sudo semanage fcontext -d '/run/px4-userland(/.*)?'
test -d /run/px4-userland && sudo restorecon -RFv /run/px4-userland
```

## 実機検証について

Fedora 44（x86_64、DG-STK5S、SELinux Enforcing）実機環境にて、以下を確認済みです。

- `px4d` が `px4d_t`、`pcscd` が `pcscd_t` ドメインで動作し、SELinux AVC 拒否（Access Vector Cache denial）が 0 件であること
- `pcscd` 補助グループに属する一般ユーザーから `px4ctl` による状態取得、地デジおよび衛星放送の 10 秒間同時受信、内蔵カードへの直接 APDU 送受信（10/10 成功）
- PC/SC 経由での内蔵カードリーダー列挙、ATR（`3B F0 12 00 FF 91 81 B1 7C 45 1F 03 99`）取得、および APDU SW9000 応答
- サービス停止時に systemd の `RuntimeDirectory` を含めランタイム階層が安全に削除されること
- OS 再起動後も SELinux Enforcing が維持され、`px4d@.service` の自動起動（`NRestarts=0`、ブート中 AVC 0 件、`NoNewPrivileges=yes`）、`px4d_var_run_t`（ディレクトリ `0750`、ソケット `0660`）でのランタイム生成、一般ユーザーからの `px4ctl status`、標準 PC/SC リーダー認識および APDU 送受信、ならびに地デジ・衛星の 10 秒間同時受信（地デジ 21,773,032 bytes / 115,814 packets、衛星 28,814,948 bytes / 153,271 packets、TS 各エラー 0 件）が正常に成功すること
