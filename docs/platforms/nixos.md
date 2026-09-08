# NixOS での構成例

> **検証済み構成について:** 本文書は以下の環境で動作確認した構成例です。将来のOS更新への追従や継続的な保守は保証しません。

## 検証対象

- 検証日: 2026-09-08
- `px4-userland`: commit `69ae0e056abb1f3a7291c41cb8836d2c4a2bde1e`, version `0.1.2`
- NixOS 26.05.9227.c25784012c99, x86_64, glibc 2.42, kernel 6.18.49
- systemd 260, udev, pcsc-lite 2.4.1

## 最小構成例

USB のアクセス権限、PC/SC、および非アクティブな SSH セッションからの PC/SC 利用に関する最小限の設定例です。

```nix
{
  users.users."<user>".extraGroups = [ "video" "pcscd" ];

  services.udev.extraRules = ''
    SUBSYSTEM=="usb", ENV{DEVTYPE}=="usb_device", ATTR{idVendor}=="0511", ATTR{idProduct}=="084a", MODE="0660", GROUP="video"
  '';

  services.pcscd = {
    enable = true;
    readerConfigs = [
      ''
        FRIENDLYNAME "PLEX PX-Q3U4 Internal Card Reader"
        DEVICENAME   px4-userland:runtime=<runtime-dir>:device=<base-serial>:access=group
        LIBPATH      <ifd-library>
        CHANNELID    0
      ''
    ];
  };

  security.polkit.extraConfig = ''
    polkit.addRule(function(action, subject) {
      if ((action.id == "org.debian.pcsc-lite.access_pcsc" ||
           action.id == "org.debian.pcsc-lite.access_card") &&
          subject.isInGroup("pcscd")) {
        return polkit.Result.YES;
      }
    });
  '';
}
```

`<runtime-dir>`、`<base-serial>`、`<ifd-library>` は、使用する環境や機器に合わせて適切な値へ置き換えてください。

なお、上記 Nix 設定だけでは `px4d` は起動しません。トップ README の共通 PC/SC 手順に従い、ランタイムディレクトリの作成と `px4d` のグループモード（`--group`）起動を別途構成してください。

## 実機で確認したこと

NixOS 環境にて、Linux 向け配布アーカイブを一般ユーザー権限で実行し、PX-Q3U4 の両 USB ブリッジに対する udev パーミッション、8 レシーバー同時の MPEG-TS 受信、およびカードへの直接 APDU 送受信が正常に行えることを確認しました。また、glibc 版 IFD ハンドラが NixOS の pcsc-lite に正常に読み込まれ、一般ユーザーの PC/SC クライアントから内蔵リーダーの ATR と SW9000 応答を取得できることを確認済みです。
