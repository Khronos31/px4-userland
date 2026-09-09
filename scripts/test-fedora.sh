#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-only
# Offline/static checks for the Fedora SELinux, systemd, sysusers, and Polkit
# integration. Fedora-only compilation and hardware checks run separately.
set -eu

root=$(CDPATH='' cd -- "$(dirname -- "$0")/.." && pwd)
fedora="$root/packaging/fedora"
policy="$fedora/selinux/px4d.te"
file_contexts="$fedora/selinux/px4d.fc"
unit="$fedora/systemd/px4d@.service"
sysusers="$fedora/sysusers.d/px4-userland.conf"
polkit="$fedora/polkit/50-px4-userland.rules"

for path in "$policy" "$file_contexts" "$unit" "$sysusers" "$polkit"; do
    test -f "$path"
    grep -F 'SPDX-License-Identifier: GPL-2.0-only' "$path" >/dev/null
done

sh -n "$0"
shellcheck -S error "$0"

grep -F 'policy_module(px4d, 1.0.0)' "$policy" >/dev/null
grep -F 'type px4d_t;' "$policy" >/dev/null
grep -F 'type px4d_exec_t;' "$policy" >/dev/null
grep -F 'type px4d_var_run_t;' "$policy" >/dev/null
grep -F 'type px4d_data_t;' "$policy" >/dev/null
grep -F 'files_pid_file(px4d_var_run_t)' "$policy" >/dev/null
grep -F 'files_pid_filetrans(init_t, px4d_var_run_t, dir, "px4-userland")' "$policy" >/dev/null
grep -F 'init_daemon_domain(px4d_t, px4d_exec_t)' "$policy" >/dev/null
grep -F 'init_nnp_daemon_domain(px4d_t)' "$policy" >/dev/null
if grep -F 'allow px4d_t device_t:dir { getattr open read search };' "$policy" >/dev/null; then
    printf '%s\n' 'Fedora SELinux policy must not grant speculative device_t directory access' >&2
    exit 1
fi
grep -F 'dev_rw_generic_usb_dev(px4d_t)' "$policy" >/dev/null
grep -F 'dev_read_sysfs(px4d_t)' "$policy" >/dev/null
grep -F 'udev_read_db(px4d_t)' "$policy" >/dev/null
grep -F 'allow px4d_t self:netlink_kobject_uevent_socket create_socket_perms;' "$policy" >/dev/null
grep -F 'files_search_usr(px4d_t)' "$policy" >/dev/null
grep -F 'read_files_pattern(px4d_t, px4d_data_t, px4d_data_t)' "$policy" >/dev/null
grep -F 'stream_connect_pattern(pcscd_t, px4d_var_run_t, px4d_var_run_t, px4d_t)' "$policy" >/dev/null

# Keep the pcscd rule auditable: no wildcard target, broad domain, or write
# capability may be hidden in a second direct allow statement.
pcscd_allows=$(grep -E '^[[:space:]]*allow[[:space:]]+pcscd_t[[:space:]]' "$policy" || true)
test -z "$pcscd_allows"
test "$(printf '%s\n' "$pcscd_allows" | grep -Ec 'unconfined|self:|domain|\*' || true)" -eq 0

grep -F '/opt/px4-userland/px4d -- gen_context(system_u:object_r:px4d_exec_t,s0)' "$file_contexts" >/dev/null
grep -F '/opt/px4-userland/firmware/it930x-firmware\.bin -- gen_context(system_u:object_r:px4d_data_t,s0)' "$file_contexts" >/dev/null
grep -F '/run/px4-userland(/.*)? gen_context(system_u:object_r:px4d_var_run_t,s0)' "$file_contexts" >/dev/null

grep -F 'User=px4d' "$unit" >/dev/null
grep -F 'Group=pcscd' "$unit" >/dev/null
grep -F 'SupplementaryGroups=video' "$unit" >/dev/null
grep -F 'RuntimeDirectory=px4-userland' "$unit" >/dev/null
grep -F 'RuntimeDirectoryMode=0750' "$unit" >/dev/null
grep -F 'UMask=0007' "$unit" >/dev/null
grep -F -- '--device %i' "$unit" >/dev/null
grep -F -- '--firmware /opt/px4-userland/firmware/it930x-firmware.bin' "$unit" >/dev/null
grep -F -- '--runtime-dir /run/px4-userland --group' "$unit" >/dev/null
# SocketListener validates the explicit runtime base as daemon-owned with mode
# 0750.  The daemon then appends px4-userland/<serial>, so this unit's actual
# socket hierarchy is /run/px4-userland/px4-userland/<serial>/.  Keep the
# dedicated base and group-mode socket permissions aligned with that contract
# (0750 directories and 0660 sockets via UMask=0007).
grep -F 'ReadWritePaths=/run/px4-userland' "$unit" >/dev/null
grep -F 'Restart=no' "$unit" >/dev/null
grep -F 'NoNewPrivileges=yes' "$unit" >/dev/null
if grep -F 'does not opt into the separate NNP transition path' "$unit" >/dev/null; then
    printf '%s\n' 'systemd unit must not claim that the policy omits the NNP transition' >&2
    exit 1
fi
if grep -F 'Restart=on-failure' "$unit" >/dev/null; then
    printf '%s\n' 'systemd unit must not restart indefinitely after hardware failure' >&2
    exit 1
fi
grep -F 'PrivateDevices' "$unit" | grep -Fv 'Do not use' >/dev/null && {
    printf '%s\n' 'systemd unit must not enable PrivateDevices' >&2
    exit 1
}
grep -F 'CapabilityBoundingSet=' "$unit" >/dev/null

awk '
    /^[[:space:]]*(#|$)/ { next }
    count == 0 && $1 == "u" && $2 == "px4d" && $3 == "-" { count++; next }
    count == 1 && $1 == "m" && $2 == "px4d" && $3 == "pcscd" { count++; next }
    { exit 1 }
    END { exit count == 2 ? 0 : 1 }
' "$sysusers"

grep -F 'action.id === "org.debian.pcsc-lite.access_pcsc"' "$polkit" >/dev/null
grep -F 'action.id === "org.debian.pcsc-lite.access_card"' "$polkit" >/dev/null
grep -F 'subject.isInGroup("pcscd")' "$polkit" >/dev/null
if grep -E 'isInGroup\("(wheel|video|users)"\)|action\.id[^=]*==[^=]*\*' "$polkit" >/dev/null; then
    printf '%s\n' 'Polkit rule contains an out-of-scope group or wildcard action' >&2
    exit 1
fi

printf '%s\n' 'Fedora integration static checks: PASS'
