#!/usr/bin/env python3
from __future__ import annotations

import importlib.util
import argparse
import hashlib
import io
import json
from pathlib import Path
import tarfile
import tempfile

spec = importlib.util.spec_from_file_location("audit_artifact", Path(__file__).with_name("audit-artifact.py"))
assert spec is not None and spec.loader is not None
audit = importlib.util.module_from_spec(spec)
spec.loader.exec_module(audit)

root = Path(__file__).resolve().parents[1]
payloads = {
    name: (root / "distribution/licenses" / Path(name).name).read_bytes()
    for name in audit.LINUX_RUNTIME_LICENSE_SHA256
}
modes = {name: 0o644 for name in payloads}
audit.verify_linux_runtime_licenses(payloads, modes)

static_properties = {
    "compiler": "/usr/bin/g++",
    "compiler_sha256": "c" * 64,
    "compiler_version": "14.2.0",
    "compiler_target": "x86_64-alpine-linux-musl",
    "libc": "musl",
    "libc_version": "musl-1.2.5-r0",
    "runtime": "libgcc+libstdc++",
    "linkage": "static",
    "libc_archive": "/usr/lib/libc.a",
    "libc_archive_sha256": "a" * 64,
    "libc_archive_owner": "musl-dev-1.2.5-r0",
    "libgcc_archive": "/usr/lib/gcc/x86_64-alpine-linux-musl/14.2.0/libgcc.a",
    "libgcc_archive_sha256": "b" * 64,
    "libgcc_archive_owner": "gcc-14.2.0-r0",
    "libstdcxx_archive": "/usr/lib/gcc/x86_64-alpine-linux-musl/14.2.0/libstdc++.a",
    "libstdcxx_archive_sha256": "d" * 64,
    "libstdcxx_archive_owner": "libstdc++-14.2.0-r0",
}
audit.verify_linux_toolchain_properties(static_properties, "evidence/toolchain-static.properties", "linux-x86_64")

ifd_properties = {
    "compiler": "/usr/bin/g++",
    "compiler_sha256": "c" * 64,
    "compiler_version": "14.2.0",
    "compiler_target": "x86_64-alpine-linux-musl",
    "libc": "musl",
    "libc_version": "musl-1.2.5-r0",
    "runtime": "libgcc+libstdc++",
    "linkage": "shared-ifd-static-gcc-runtime",
    "libc_input": "/lib/ld-musl-x86_64.so.1",
    "libc_input_sha256": "a" * 64,
    "libc_input_owner": "musl-1.2.5-r12",
    "libc_input_package_version": "musl-1.2.5-r12",
    "libgcc_archive": "/usr/lib/gcc/x86_64-alpine-linux-musl/14.2.0/libgcc.a",
    "libgcc_archive_sha256": "b" * 64,
    "libgcc_archive_owner": "gcc-14.2.0-r0",
    "libgcc_package_version": "gcc-14.2.0-r0",
    "libstdcxx_archive": "/usr/lib/gcc/x86_64-alpine-linux-musl/14.2.0/libstdc++.a",
    "libstdcxx_archive_sha256": "d" * 64,
    "libstdcxx_archive_owner": "libstdc++-14.2.0-r0",
    "libstdcxx_package_version": "libstdc++-14.2.0-r0",
}
with tempfile.TemporaryDirectory(prefix="px4-musl-linker-input-") as temporary:
    fixture_root = Path(temporary)
    (fixture_root / "usr/lib").mkdir(parents=True)
    (fixture_root / "lib").mkdir()
    resolved_libc = fixture_root / "lib/ld-musl-x86_64.so.1"
    resolved_libc.write_bytes(b"fixture musl runtime payload\n")
    lookup_libc = fixture_root / "usr/lib/libc.so"
    lookup_libc.symlink_to("../../lib/ld-musl-x86_64.so.1")
    if lookup_libc.resolve(strict=True) != resolved_libc:
        raise SystemExit("musl libc.so symlink fixture did not resolve to the loader payload")
    ifd_properties["libc_input"] = str(lookup_libc.resolve(strict=True))
    audit.verify_linux_toolchain_properties(ifd_properties, "evidence/toolchain-ifd.properties", "linux-x86_64")

wrong_ifd_owner = dict(ifd_properties)
wrong_ifd_owner["libc_input_owner"] = "musl-dev-1.2.5-r12"
wrong_ifd_owner["libc_input_package_version"] = "musl-dev-1.2.5-r12"
try:
    audit.verify_linux_toolchain_properties(wrong_ifd_owner, "evidence/toolchain-ifd.properties", "linux-x86_64")
except audit.AuditError:
    pass
else:
    raise SystemExit("musl IFD metadata accepted the libc.so link-stub owner instead of payload owner")

wrong_ifd_version = dict(ifd_properties)
wrong_ifd_version["libc_input_package_version"] = "musl-1.2.5-r11"
try:
    audit.verify_linux_toolchain_properties(wrong_ifd_version, "evidence/toolchain-ifd.properties", "linux-x86_64")
except audit.AuditError:
    pass
else:
    raise SystemExit("musl IFD metadata accepted a libc payload owner/version mismatch")

unrelated_runtime = dict(static_properties)
unrelated_runtime["libc_archive_owner"] = "external-sysroot"
try:
    audit.verify_linux_toolchain_properties(
        unrelated_runtime, "evidence/toolchain-static.properties", "linux-x86_64")
except audit.AuditError:
    pass
else:
    raise SystemExit("toolchain metadata accepted an unrelated libc archive owner")

wrong_compiler = dict(static_properties)
wrong_compiler["compiler_target"] = "aarch64-alpine-linux-musl"
try:
    audit.verify_linux_toolchain_properties(
        wrong_compiler, "evidence/toolchain-static.properties", "linux-x86_64")
except audit.AuditError:
    pass
else:
    raise SystemExit("toolchain metadata accepted a compiler for another archive platform")

missing = dict(payloads)
missing.pop(next(iter(missing)))
try:
    audit.verify_linux_runtime_licenses(missing, modes)
except audit.AuditError:
    pass
else:
    raise SystemExit("missing Linux runtime license was accepted")

tampered = dict(payloads)
name = next(iter(tampered))
tampered[name] += b"tampered\n"
try:
    audit.verify_linux_runtime_licenses(tampered, modes)
except audit.AuditError:
    pass
else:
    raise SystemExit("tampered Linux runtime license was accepted")

wrong_mode = dict(modes)
name = next(iter(wrong_mode))
wrong_mode[name] = 0o600
try:
    audit.verify_linux_runtime_licenses(payloads, wrong_mode)
except audit.AuditError:
    pass
else:
    raise SystemExit("Linux runtime license with mode 0600 was accepted")

source_paths = (
    "LICENSE", "README.md", "THIRD_PARTY_NOTICES.md", "scripts/build-linux-static.sh",
    "scripts/build-linux-ifd.sh", "scripts/build-macos-static.sh", "scripts/test-static-relink.sh",
    "packaging/REBUILD.md.in", "packaging/libusb/COPYING",
    "distribution/licenses/gcc-COPYING3", "distribution/licenses/gcc-COPYING.RUNTIME",
    "distribution/licenses/musl-1.2.5-COPYRIGHT",
)
source_payloads = {f"repository/{name}": (root / name).read_bytes() for name in source_paths}
source_payloads.update({
    "BUILD-RELINK.md": (root / "packaging/REBUILD.md.in").read_bytes(),
    "DEPENDENCY-NOTICE.txt": b"dependency.libusb.version=1.0.30\ncorresponding-source-archive=present\n",
    "LICENSE": (root / "LICENSE").read_bytes(),
    "README.md": (root / "README.md").read_bytes(),
    "THIRD_PARTY_NOTICES.md": (root / "THIRD_PARTY_NOTICES.md").read_bytes(),
})
libusb_buffer = io.BytesIO()
with tarfile.open(fileobj=libusb_buffer, mode="w:bz2") as stream:
    info = tarfile.TarInfo("libusb-1.0.30/COPYING")
    payload = b"fixture libusb COPYING\n"
    info.size = len(payload)
    stream.addfile(info, io.BytesIO(payload))
source_payloads["third_party/libusb-1.0.30.tar.bz2"] = libusb_buffer.getvalue()
fixture_libusb_hash = hashlib.sha256(libusb_buffer.getvalue()).hexdigest()
old_libusb_hash = audit.LIBUSB_SHA256
audit.LIBUSB_SHA256 = fixture_libusb_hash
version = (root / "VERSION").read_text(encoding="utf-8").strip()

def source_archive(path: Path, files: dict[str, bytes]) -> None:
    manifest = {
        "schema": 1,
        "version": version,
        "libusb_version": "1.0.30",
        "libusb_archive_sha256": fixture_libusb_hash,
        "files": {
            name: {"size": len(data), "sha256": hashlib.sha256(data).hexdigest()}
            for name, data in sorted(files.items())
        },
    }
    files = dict(files)
    files["source-manifest.json"] = (json.dumps(manifest, sort_keys=True) + "\n").encode()
    files["SHA256SUMS"] = ("\n".join(
        f"{hashlib.sha256(files[name]).hexdigest()}  {name}" for name in sorted(files)
    ) + "\n").encode()
    with tarfile.open(path, "w:gz") as stream:
        for name, data in sorted(files.items()):
            info = tarfile.TarInfo(name)
            info.size = len(data)
            info.mode = 0o644
            stream.addfile(info, io.BytesIO(data))

with tempfile.TemporaryDirectory(prefix="px4-runtime-source-audit-") as temporary:
    archive = Path(temporary) / f"px4-userland-{version}-source.tar.gz"
    source_archive(archive, source_payloads)
    audit.audit_source_archive(argparse.Namespace(archive=archive))

    missing = dict(source_payloads)
    missing.pop("repository/distribution/licenses/gcc-COPYING3")
    source_archive(archive, missing)
    try:
        audit.audit_source_archive(argparse.Namespace(archive=archive))
    except audit.AuditError as error:
        if "source archive is missing" not in str(error):
            raise
    else:
        raise SystemExit("source auditor accepted an archive missing a runtime license")

    altered = dict(source_payloads)
    altered["repository/distribution/licenses/gcc-COPYING3"] += b"tampered\n"
    source_archive(archive, altered)
    try:
        audit.audit_source_archive(argparse.Namespace(archive=archive))
    except audit.AuditError as error:
        if "source archive runtime license is altered" not in str(error):
            raise
    else:
        raise SystemExit("source auditor accepted an altered runtime license")
audit.LIBUSB_SHA256 = old_libusb_hash

print("Linux runtime license and source archive audit self-test: PASS")
