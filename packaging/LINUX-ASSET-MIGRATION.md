# Linux asset migration

Linux Beta assets are libc-qualified. The old generic `linux-x86_64` and
`linux-aarch64` archives are not production release names and must not be
selected by installers.

For each architecture, use the matching pair:

- `px4-userland-VERSION-linux-glibc-x86_64.tar.gz`
- `px4-userland-VERSION-linux-musl-x86_64.tar.gz`
- `px4-userland-VERSION-linux-glibc-aarch64.tar.gz`
- `px4-userland-VERSION-linux-musl-aarch64.tar.gz`

All four archives contain the same fully static musl `px4d`, `px4-ts`, and
`px4ctl`. Select the archive whose IFD Handler matches the host `pcscd` libc:
the glibc archive for a glibc host and the musl archive for a musl host. The
IFD Handler is a host-loaded shared object; the production executables have no
interpreter or dynamic `DT_NEEDED` entries.

Do not infer host compatibility from the executable libc. Verify the archive
`metadata.json` and `evidence/binary-audit.json` before installation.
