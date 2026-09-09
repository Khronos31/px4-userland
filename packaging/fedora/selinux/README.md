# Fedora SELinux policy source

Build and install this source on Fedora with the reference-policy development
Makefile:

```sh
cd packaging/fedora/selinux
make -f /usr/share/selinux/devel/Makefile px4d.pp
sudo semodule -i px4d.pp
sudo restorecon -RFv /opt/px4-userland
```

The systemd unit creates `/run/px4-userland` through `RuntimeDirectory` when
it starts. Restore that path only after the unit has started and the directory
exists:

```sh
if test -d /run/px4-userland; then
    sudo restorecon -RFv /run/px4-userland
fi
```

The module's `.fc` file is authoritative for these paths. Remove an old,
conflicting local runtime mapping once, if present, before restoring labels:

```sh
if sudo semanage fcontext -lC | grep -F '/run/px4-userland' >/dev/null; then
    sudo semanage fcontext -d '/run/px4-userland(/.*)?'
fi
sudo restorecon -RFv /opt/px4-userland
if test -d /run/px4-userland; then
    sudo restorecon -RFv /run/px4-userland
fi
```

The repository deliberately ships the policy source rather than a compiled
policy module. The Fedora host's installed reference policy supplies the
interfaces used by `px4d.te`.
