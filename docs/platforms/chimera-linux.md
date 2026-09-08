# Chimera Linux での構成例

> **検証済み構成について:** 本文書は以下の環境で動作確認した構成例です。将来のOS更新への追従や継続的な保守は保証しません。

## 検証対象

- 検証日: 2026-09-09
- `px4-userland`: commit `3e6e32a107566689a9b4bf223dca1e4e89f67cfa`, version `0.1.2`
- Chimera Linux, rootfs snapshot 20251220, x86_64, musl 1.2.5_git20240705, kernel 6.18.48-0-generic
- Clang/LLVM/libc++ 22.1.8, libusb 1.0.30, pcsc-lite 2.3.3
- PID 1 は dinit、USB device manager は udev

## USB と PC/SC の設定

PX-Q3U4 のみを対象とした udev ルールは次のとおりです。

```udev
SUBSYSTEM=="usb", ENV{DEVTYPE}=="usb_device", ATTR{idVendor}=="0511", ATTR{idProduct}=="084a", MODE="0660", GROUP="video"
```

実行ユーザーを `video` グループに追加します。非アクティブな SSH セッションから PC/SC を利用する場合は、`video` グループを許可する以下の Polkit ルールも設定します。

```javascript
polkit.addRule(function(action, subject) {
  if ((action.id == "org.debian.pcsc-lite.access_pcsc" ||
       action.id == "org.debian.pcsc-lite.access_card") &&
      subject.isInGroup("video")) {
    return polkit.Result.YES;
  }
});
```

Chimera の dinit で `pcscd` を管理する場合、起動直後は通信ソケットが未生成のことがあります。`/run/pcscd/pcscd.comm` の生成を待ってから PC/SC リーダーの検出を行ってください。

## 実機で確認したこと

Clang 22、musl、libc++ によるネイティブビルド（94/94 ターゲット）および CTest（7/7）の通過を確認しました。実機の地上波・衛星チューナー系統で MPEG-TS を受信し、8 レシーバー同時の受信とカードへの直接 APDU 送受信（100/100）が正常に行えることを確認済みです。また、ビルドした IFD ハンドラが Chimera の pcsc-lite に正常に読み込まれ、一般ユーザー権限の `pcsc_scan` から内蔵リーダーの ATR を取得できることを確認しました。

なお、dinit で `pcscd` を管理する場合は、上記のソケット生成完了を待つ起動順序で検証しています。一般ユーザー実行時に `mlockall` や SCHED_FIFO に関する警告が出力される場合がありますが、今回の検証では非致命であり、受信処理は正常に完了しました。
