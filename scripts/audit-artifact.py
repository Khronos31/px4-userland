#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-only
"""Audit px4-userland binaries and deterministic release archives."""

from __future__ import annotations

import argparse
import hashlib
import io
import json
import os
from pathlib import Path, PurePosixPath
import re
import stat
import struct
import subprocess
import sys
import tarfile
import tempfile
import zipfile


PLATFORMS = {
    "linux-glibc-x86_64": "linux-glibc",
    "linux-glibc-aarch64": "linux-glibc",
    "linux-musl-x86_64": "linux-musl",
    "linux-musl-aarch64": "linux-musl",
    "darwin-arm64": "darwin",
    "android-aarch64": "android",
    "android-armv7a": "android",
    "android-x86_64": "android",
    "windows-x86_64": "windows",
}
LINUX_TARGETS = {
    "linux-glibc-x86_64": {
        "machine": "Advanced Micro Devices X86-64",
        "libc": "libc.so.6", "floor": "2.31",
        "needed": {"libc.so.6", "libpthread.so.0", "ld-linux-x86-64.so.2"},
    },
    "linux-glibc-aarch64": {
        "machine": "AArch64",
        "libc": "libc.so.6", "floor": "2.31",
        "needed": {"libc.so.6", "libpthread.so.0", "ld-linux-aarch64.so.1"},
    },
    "linux-musl-x86_64": {
        "machine": "Advanced Micro Devices X86-64",
        "libc": "libc.musl-x86_64.so.1", "floor": None, "needed": None,
    },
    "linux-musl-aarch64": {
        "machine": "AArch64",
        "libc": "libc.musl-aarch64.so.1", "floor": None, "needed": None,
    },
}
VERSION_RE = re.compile(r"^[0-9]+\.[0-9]+\.[0-9]+$")
LIBUSB_SHA256 = "fea36f34f9156400209595e300840767ab1a385ede1dc7ee893015aea9c6dbaf"
LIBUSB_COPYING_SHA256 = "5df07007198989c622f5d41de8d703e7bef3d0e79d62e24332ee739a452af62a"
LINUX_RUNTIME_LICENSE_SHA256 = {
    "toolchain/gcc-COPYING3": "8ceb4b9ee5adedde47b31e975c1d90c73ad27b6b165a1dcd80c7c545eb65b903",
    "toolchain/gcc-COPYING.RUNTIME": "9d6b43ce4d8de0c878bf16b54d8e7a10d9bd42b75178153e3af6a815bdc90f74",
    "toolchain/musl-1.2.5-COPYRIGHT": "f9bc4423732350eb0b3f7ed7e91d530298476f8fec0c6c427a1c04ade22655af",
}
LINUX_RUNTIME_LICENSE_MEMBERS = frozenset(LINUX_RUNTIME_LICENSE_SHA256)
COMMON = {
    "LICENSE",
    "README.md",
    "THIRD_PARTY_NOTICES.md",
    "DEPENDENCY-NOTICE.txt",
    "libusb/COPYING",
    "manifest.json",
    "SHA256SUMS",
    "evidence/binary-audit.json",
}
LINUX_MDEV = {
    "mdev/px4-userland-mdev.conf", "mdev/px4-userland-mdev.sh", "mdev/px4-userland-mdev.start",
}
PROGRAMS = ("px4d", "px4-ts", "px4ctl")
TERMUX_LAUNCHER = "px4-termux"

# Windows PE contract: ABI, static libusb marker, permitted import
# closure, and the exact zip member allowlist. libusb-1.0.dll is not shipped.
WINDOWS_PLATFORM = "windows-x86_64"
WINDOWS_LIBUSB_DLL = "libusb-1.0.dll"
WINDOWS_LIBUSB_MARKER = b"https://libusb.info"
WINDOWS_PROGRAM_ARTIFACTS = tuple(f"{program}.exe" for program in PROGRAMS)
WINDOWS_WINSCARD_ARTIFACT = "winscard/WinSCard.dll"
WINDOWS_WINSCARD_DOCUMENT = "docs/platforms/windows-winscard.md"
# Match the public Windows SDK ABI, including PCI structures exported as data.
WINDOWS_WINSCARD_FUNCTIONS = (
    "SCardEstablishContext", "SCardReleaseContext", "SCardListReadersA",
    "SCardListReadersW", "SCardConnectA", "SCardConnectW", "SCardDisconnect",
    "SCardTransmit", "SCardIsValidContext", "SCardFreeMemory", "SCardStatusW",
    "SCardGetStatusChangeW",
)
WINDOWS_WINSCARD_PCI = {"g_rgSCardT1Pci": 2}
WINDOWS_WINSCARD_EXPORTS = frozenset(WINDOWS_WINSCARD_FUNCTIONS) | set(WINDOWS_WINSCARD_PCI)
IMAGE_FILE_MACHINE_AMD64 = 0x8664
IMAGE_FILE_DLL = 0x2000
PE32_PLUS_MAGIC = 0x20B
IMAGE_DLLCHARACTERISTICS_DYNAMIC_BASE = 0x0040
IMAGE_DLLCHARACTERISTICS_NX_COMPAT = 0x0100
# Only System32 components are allowed. libusb is inside px4d.exe. libc++,
# winpthread, and the MinGW runtime are statically linked, so their DLLs are
# rejected. UCRT and other api-ms-win-crt imports stay dynamic.
WINDOWS_SYSTEM_IMPORTS = {
    "advapi32.dll", "bcrypt.dll", "kernel32.dll", "ole32.dll",
    "shell32.dll", "user32.dll", "ws2_32.dll",
}
WINDOWS_UCRT_IMPORT_PREFIX = "api-ms-win-crt-"
# Exact narrow license texts for the statically linked llvm-mingw runtimes
# (LLVM/libc++/libunwind, MinGW-w64, winpthreads, winstorecompat). They are
# sourced from the pinned, checksum-verified llvm-mingw toolchain archive and
# are the only toolchain material shipped; no compiler binary or source bulk.
WINDOWS_TOOLCHAIN_LICENSE_SHA256 = {
    "toolchain/LICENSE.TXT": "8d85c1057d742e597985c7d4e6320b015a9139385cff4cbae06ffc0ebe89afee",
    "toolchain/mingw32/COPYING": "99a69660981156c21336fdb5661f89341b013c94e4bf9e1c7467b4745718397f",
    "toolchain/mingw32/COPYING.MinGW-w64-runtime.txt": "e9b2dc02451ea29092a1f25fa0f3c07207ed421f1807dffb0c4e6dce69dee7bd",
    "toolchain/mingw32/COPYING.MinGW-w64.txt": "f38e6194bd3bfa1b654f118e5acefe0aead437bbe669eee43957ccc65a7127f1",
    "toolchain/mingw32/COPYING.winpthreads.txt": "63263614cdd29f2f93cba85e992f041b31f9fc7b4033692f31269489a8a1b177",
    "toolchain/mingw32/COPYING.winstorecompat.txt": "fa7368d1f93d890c173177475d8ec7aff265a7d08f1f364d66f74d138978ce36",
}
WINDOWS_TOOLCHAIN_LICENSE_MEMBERS = frozenset(WINDOWS_TOOLCHAIN_LICENSE_SHA256)
WINDOWS_TOOLCHAIN_LICENSE_NOTICE = ",".join(sorted(WINDOWS_TOOLCHAIN_LICENSE_MEMBERS))
WINDOWS_ARCHIVE_MEMBERS = (set(COMMON) | set(WINDOWS_PROGRAM_ARTIFACTS)
                           | {WINDOWS_WINSCARD_ARTIFACT, WINDOWS_WINSCARD_DOCUMENT}
                           | set(WINDOWS_TOOLCHAIN_LICENSE_MEMBERS))
DARWIN_IFD_ARTIFACT = "ifd/px4-userland-ifd.bundle/Contents/MacOS/libpx4-userland-ifd.dylib"
IFD_EXPORTS = (
    "IFDHCreateChannel", "IFDHCreateChannelByName", "IFDHCloseChannel",
    "IFDHGetCapabilities", "IFDHSetCapabilities", "IFDHSetProtocolParameters",
    "IFDHPowerICC", "IFDHTransmitToICC", "IFDHControl", "IFDHICCPresence",
)
READER_PLACEHOLDERS = {
    "@PX4_RUNTIME_DIR@",
    "@PX4_BASE_SERIAL@",
    "@PX4_ACCESS@",
    "@PX4_IFD_LIBRARY@",
}
ANDROID_INVENTORY_MEMBERS = tuple(
    f"evidence/inventory/{program}-static-archives.tsv" for program in PROGRAMS
)
FORBIDDEN = re.compile(
    r"(^|/)(?:firmware|windows|win32|vendor|drivers?|dkms|kernel|apk|addon|add-on)(?:/|$)"
    r"|(?:\.apk$|\.ko$|\.sys$|\.inf$|\.dll$|\.exe$|\.bin$)"
    r"|(?:px4-ts-probe|px4-.*-probe)",
    re.IGNORECASE,
)
SOURCE_FORBIDDEN = re.compile(
    r"(^|/)(?:\.git|__pycache__|build(?:-[^/.]+)?|out|dist|firmware|vendor|drivers?|dkms|kernel|apk|addon|add-on)(?:/|$)"
    r"|(?:\.o$|\.a$|\.so(?:\.|$)|\.dylib$|\.apk$|\.ko$|\.sys$|\.inf$|\.dll$|\.exe$|\.bin$|\.py[co]$|\.pyd$)"
    r"|(?:px4-ts-probe|px4-.*-probe)", re.IGNORECASE
)
# Windows source lives only in the narrow adapter/test/build/doc paths.
# Any other windows/win32-segment member is legacy or vendor material and is
# rejected, so this is not a blanket allow for old Windows trees.
SOURCE_WINDOWS_TOKEN = re.compile(r"(^|/)(?:windows|win32)(?:/|$)", re.IGNORECASE)
WINDOWS_SOURCE_ALLOWED = re.compile(
    r"^repository/(?:"
    r"userland/src/windows/[\w./+-]+"
    r"|userland/tools/[\w.+-]*windows[\w.+-]*"
    r"|userland/tests/windows_[\w./+-]+"
    r"|userland/include/px4/[\w.+-]*windows[\w.+-]*"
    r"|scripts/build-windows\.sh"
    r"|docs/[\w./+-]*windows[\w./+-]*"
    r"|third_party/dtv-userland-core/adapters/(?:windows_winscard|host_runtime|host_ts|host_facade|host_daemon)/[\w./+-]+"
    r")$", re.IGNORECASE
)
WINDOWS_SOURCE_REQUIRED = {
    "repository/scripts/build-windows.sh",
    "repository/scripts/test-windows-static-relink.sh",
    "repository/userland/tools/px4_ts_windows.cpp",
    "repository/userland/tools/px4_windows_args.h",
    "repository/userland/tests/windows_platform_tests.cpp",
    "repository/userland/tests/windows_worker_tests_main.cpp",
    "repository/userland/include/px4/windows_tuner_nonce.h",
}
# Snapshots before the host-runtime move still own the Windows IPC/nonce
# implementation in the product tree.
WINDOWS_PRODUCT_RUNTIME_REQUIRED = {
    "repository/userland/src/windows/windows_ipc.cpp",
    "repository/userland/src/windows/windows_tuner_nonce.cpp",
}
HOST_RUNTIME_ADAPTER_PREFIX = (
    "repository/third_party/dtv-userland-core/adapters/host_runtime/"
)
HOST_TS_ADAPTER_PREFIX = (
    "repository/third_party/dtv-userland-core/adapters/host_ts/"
)
HOST_FACADE_ADAPTER_PREFIX = (
    "repository/third_party/dtv-userland-core/adapters/host_facade/"
)
HOST_DAEMON_ADAPTER_PREFIX = (
    "repository/third_party/dtv-userland-core/adapters/host_daemon/"
)
HOST_TS_ADAPTER_REQUIRED = {
    HOST_TS_ADAPTER_PREFIX + "COPYING",
    HOST_TS_ADAPTER_PREFIX + "README.md",
    HOST_TS_ADAPTER_PREFIX + "control_client.cpp",
    HOST_TS_ADAPTER_PREFIX + "include/dtv/host_ts/control_client.h",
    HOST_TS_ADAPTER_PREFIX + "include/dtv/host_ts/px4_ts_core.h",
    HOST_TS_ADAPTER_PREFIX + "include/dtv/host_ts/px4_ts_posix.h",
    HOST_TS_ADAPTER_PREFIX + "include/dtv/host_ts/px4_ts_windows_output.h",
    HOST_TS_ADAPTER_PREFIX + "include/px4/control_client.h",
    HOST_TS_ADAPTER_PREFIX + "px4_ts_core.cpp",
    HOST_TS_ADAPTER_PREFIX + "posix/px4_ts_posix.cpp",
    HOST_TS_ADAPTER_PREFIX + "windows/px4_ts_windows_output.cpp",
    "repository/CMakeLists.txt",
}
WINDOWS_HOST_RUNTIME_REQUIRED = {
    HOST_RUNTIME_ADAPTER_PREFIX + "COPYING",
    HOST_RUNTIME_ADAPTER_PREFIX + "README.md",
    HOST_RUNTIME_ADAPTER_PREFIX + "include/px4/posix_ipc_contract.h",
    HOST_RUNTIME_ADAPTER_PREFIX + "include/px4/posix_tuner_nonce_internal.h",
    HOST_RUNTIME_ADAPTER_PREFIX + "include/windows/windows_security.h",
    HOST_RUNTIME_ADAPTER_PREFIX + "posix_ipc.cpp",
    HOST_RUNTIME_ADAPTER_PREFIX + "posix_tuner_nonce.cpp",
    HOST_RUNTIME_ADAPTER_PREFIX + "windows/windows_ipc.cpp",
    HOST_RUNTIME_ADAPTER_PREFIX + "windows/windows_tuner_nonce.cpp",
    "repository/cmake/DtvUserlandCoreDependency.cmake",
    "repository/userland/include/px4/posix_tuner_nonce.h",
    "repository/userland/include/px4/windows_tuner_nonce.h",
}
HOST_FACADE_ADAPTER_REQUIRED = {
    HOST_FACADE_ADAPTER_PREFIX + "COPYING",
    HOST_FACADE_ADAPTER_PREFIX + "README.md",
    HOST_FACADE_ADAPTER_PREFIX + "PROVENANCE.json",
    HOST_FACADE_ADAPTER_PREFIX + "include/dtv/userland/host_facade/px4d_signals.h",
    HOST_FACADE_ADAPTER_PREFIX + "include/dtv/userland/host_facade/px4d_windows_stdin.h",
    HOST_FACADE_ADAPTER_PREFIX + "include/dtv/userland/host_facade/identity.h",
    HOST_FACADE_ADAPTER_PREFIX + "include/px4/card_atr_bytes.h",
    HOST_FACADE_ADAPTER_PREFIX + "include/px4/error.h",
    HOST_FACADE_ADAPTER_PREFIX + "include/px4/ipc.h",
    HOST_FACADE_ADAPTER_PREFIX + "include/px4/platform_sleep.h",
    HOST_FACADE_ADAPTER_PREFIX + "include/px4/posix_ipc.h",
    HOST_FACADE_ADAPTER_PREFIX + "include/px4/transport.h",
    HOST_FACADE_ADAPTER_PREFIX + "src/ipc.cpp",
    HOST_FACADE_ADAPTER_PREFIX + "src/identity.cpp",
    HOST_FACADE_ADAPTER_PREFIX + "src/px4d_signals.cpp",
    HOST_FACADE_ADAPTER_PREFIX + "src/px4d_windows_stdin.cpp",
    HOST_FACADE_ADAPTER_PREFIX + "src/windows_sleep.cpp",
    "repository/CMakeLists.txt",
}
HOST_DAEMON_ADAPTER_REQUIRED = {
    HOST_DAEMON_ADAPTER_PREFIX + "COPYING",
    HOST_DAEMON_ADAPTER_PREFIX + "README.md",
    HOST_DAEMON_ADAPTER_PREFIX + "PROVENANCE.json",
    HOST_DAEMON_ADAPTER_PREFIX + "include/dtv/userland/host_daemon/card_service.h",
    HOST_DAEMON_ADAPTER_PREFIX + "include/dtv/userland/host_daemon/control_server_test_access.h",
    HOST_DAEMON_ADAPTER_PREFIX + "include/dtv/userland/host_daemon/control_workers.h",
    HOST_DAEMON_ADAPTER_PREFIX + "include/px4/card_service.h",
    HOST_DAEMON_ADAPTER_PREFIX + "include/px4/control_execution_layout.h",
    HOST_DAEMON_ADAPTER_PREFIX + "include/px4/control_server.h",
    HOST_DAEMON_ADAPTER_PREFIX + "include/px4/enclosure_context.h",
    HOST_DAEMON_ADAPTER_PREFIX + "include/px4/tuner_service.h",
    HOST_DAEMON_ADAPTER_PREFIX + "src/card_service.cpp",
    HOST_DAEMON_ADAPTER_PREFIX + "src/control_server.cpp",
    HOST_DAEMON_ADAPTER_PREFIX + "src/control_workers.cpp",
    HOST_DAEMON_ADAPTER_PREFIX + "src/enclosure_context.cpp",
    HOST_DAEMON_ADAPTER_PREFIX + "src/tuner_service.cpp",
    HOST_DAEMON_ADAPTER_PREFIX + "src/windows_pthread_compat.h",
    "repository/CMakeLists.txt",
}
# The shareable WinSCard proxy/client lives once in the optional GPL adapter of
# the pinned common core.  The product keeps only the build wiring, the host
# facade sources needed by the client-only support target, and the tests.
WINSCARD_ADAPTER_PREFIX = (
    "repository/third_party/dtv-userland-core/adapters/windows_winscard/"
)
WINDOWS_WINSCARD_SOURCE_REQUIRED = {
    "repository/CMakeLists.txt",
    "repository/" + WINDOWS_WINSCARD_DOCUMENT,
    "repository/cmake/DtvUserlandCoreDependency.cmake",
    "repository/userland/src/ipc.cpp",
    "repository/userland/include/px4/control_client.h",
    "repository/userland/include/px4/ipc.h",
    "repository/userland/include/px4/posix_ipc.h",
    "repository/userland/tests/windows_winscard_client_tests.cpp",
    "repository/userland/tests/windows_winscard_proxy_tests.cpp",
    "repository/userland/tests/windows_winscard_import_tests.cpp",
}
WINDOWS_WINSCARD_ADAPTER_REQUIRED = {
    WINSCARD_ADAPTER_PREFIX + "CMakeLists.txt",
    WINSCARD_ADAPTER_PREFIX + "README.md",
    WINSCARD_ADAPTER_PREFIX + "COPYING",
    WINSCARD_ADAPTER_PREFIX + "winscard.def",
    WINSCARD_ADAPTER_PREFIX + "windows/winscard_card_client.h",
    WINSCARD_ADAPTER_PREFIX + "windows/winscard_card_client.cpp",
    WINSCARD_ADAPTER_PREFIX + "windows/winscard_native.h",
    WINSCARD_ADAPTER_PREFIX + "windows/winscard_proxy.cpp",
    WINSCARD_ADAPTER_PREFIX + "windows/winscard_product.h.in",
}
# Snapshots that predate the shared foundation still carry the product-owned
# WinSCard tree.  They are audited exactly as before; the move must not make a
# historical corresponding-source archive unverifiable.
WINDOWS_WINSCARD_LEGACY_MARKERS = {
    "repository/scripts/px4-winscard.def",
    "repository/userland/src/windows/winscard_proxy.cpp",
    "repository/userland/src/windows/winscard_card_client.cpp",
}
WINDOWS_WINSCARD_LEGACY_SOURCE_REQUIRED = {
    "repository/CMakeLists.txt",
    "repository/" + WINDOWS_WINSCARD_DOCUMENT,
    "repository/scripts/px4-winscard.def",
    "repository/userland/src/windows/winscard_card_client.h",
    "repository/userland/src/windows/winscard_card_client.cpp",
    "repository/userland/src/windows/winscard_native.h",
    "repository/userland/src/windows/winscard_proxy.cpp",
    "repository/userland/tests/windows_winscard_client_tests.cpp",
    "repository/userland/tests/windows_winscard_proxy_tests.cpp",
    "repository/userland/tests/windows_winscard_import_tests.cpp",
}


class AuditError(Exception):
    pass


def fail(message: str) -> None:
    raise AuditError(message)


def run(command: list[str], *, input_text: str | None = None) -> str:
    try:
        completed = subprocess.run(
            command,
            check=True,
            text=True,
            input=input_text,
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
        )
    except (OSError, subprocess.CalledProcessError) as error:
        output = getattr(error, "stdout", "") or ""
        fail(f"command failed ({' '.join(command)}): {output.strip()}")
    return completed.stdout


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def validate_version(version: str) -> None:
    if not VERSION_RE.fullmatch(version):
        fail(f"version must be strict N.N.N, got {version!r}")


def validate_platform(platform: str) -> None:
    if platform not in PLATFORMS:
        fail(f"unsupported platform: {platform}")


def _pe_u16(data: bytes, offset: int) -> int:
    if offset + 2 > len(data):
        fail("truncated PE image")
    return struct.unpack_from("<H", data, offset)[0]


def _pe_u32(data: bytes, offset: int) -> int:
    if offset + 4 > len(data):
        fail("truncated PE image")
    return struct.unpack_from("<I", data, offset)[0]


def parse_pe(data: bytes) -> dict:
    """Parse the PE headers needed for the Windows release contract.

    The parser is deliberately self-contained so the Windows audit does not
    depend on a host llvm-readobj/objdump and can run on any CI runner.
    """
    if len(data) < 0x40 or data[:2] != b"MZ":
        fail("not a PE image: missing MZ header")
    pe_offset = _pe_u32(data, 0x3C)
    if pe_offset + 24 > len(data) or data[pe_offset:pe_offset + 4] != b"PE\0\0":
        fail("not a PE image: missing PE signature")
    machine = _pe_u16(data, pe_offset + 4)
    section_count = _pe_u16(data, pe_offset + 6)
    timestamp = _pe_u32(data, pe_offset + 8)
    optional_size = _pe_u16(data, pe_offset + 20)
    characteristics = _pe_u16(data, pe_offset + 22)
    optional_offset = pe_offset + 24
    if optional_size < 0x70 or optional_offset + optional_size > len(data):
        fail("truncated PE optional header")
    magic = _pe_u16(data, optional_offset)
    dll_characteristics = _pe_u16(data, optional_offset + 70)
    number_of_directories = _pe_u32(data, optional_offset + 108) if magic == PE32_PLUS_MAGIC else 0
    if magic == PE32_PLUS_MAGIC and 112 + number_of_directories * 8 > optional_size:
        fail("PE data directories exceed the optional header")
    sections = []
    section_offset = optional_offset + optional_size
    for index in range(section_count):
        base = section_offset + index * 40
        if base + 40 > len(data):
            fail("truncated PE section table")
        sections.append((
            _pe_u32(data, base + 12), _pe_u32(data, base + 8),
            _pe_u32(data, base + 16), _pe_u32(data, base + 20),
            _pe_u32(data, base + 36),
        ))

    def rva_to_offset(rva: int) -> int | None:
        for virtual_address, _, raw_size, raw_pointer, _ in sections:
            if virtual_address <= rva < virtual_address + raw_size:
                offset = raw_pointer + (rva - virtual_address)
                return offset if offset < len(data) else None
        return None

    imports = []
    exports = []
    export_details = {}
    directories = optional_offset + 112
    if magic == PE32_PLUS_MAGIC and number_of_directories >= 1:
        export_rva = _pe_u32(data, directories)
        if export_rva:
            export_size = _pe_u32(data, directories + 4)
            export_details = _pe_exports(data, export_rva, export_size, rva_to_offset, sections)
            exports = list(export_details)
    if magic == PE32_PLUS_MAGIC and number_of_directories >= 2:
        import_rva = _pe_u32(data, directories + 8)
        if import_rva:
            imports = _pe_imports(data, import_rva, rva_to_offset)
    # Release binaries do not use delayed imports. Reject that second import
    # surface instead of silently treating it as an empty dependency closure.
    if magic == PE32_PLUS_MAGIC and number_of_directories > 13:
        if _pe_u32(data, directories + 13 * 8):
            fail("PE delayed imports are not permitted")
    return {
        "machine": machine, "magic": magic, "timestamp": timestamp,
        "characteristics": characteristics, "dll_characteristics": dll_characteristics,
        "imports": imports, "exports": exports, "export_details": export_details,
    }


def _pe_cstring(data: bytes, offset: int) -> str:
    end = data.find(b"\0", offset)
    if end < 0:
        fail("unterminated PE string")
    try:
        return data[offset:end].decode("ascii")
    except UnicodeDecodeError:
        fail("non-ASCII PE import/export name")


def _pe_imports(data: bytes, import_rva: int, rva_to_offset) -> list[str]:
    offset = rva_to_offset(import_rva)
    if offset is None:
        fail("PE import directory RVA is not mappable")
    names: list[str] = []
    seen: set[str] = set()
    while True:
        if offset + 20 > len(data):
            fail("truncated PE import directory")
        name_rva = _pe_u32(data, offset + 12)
        if name_rva == 0:
            break
        name_offset = rva_to_offset(name_rva)
        if name_offset is None:
            fail("PE import name RVA is not mappable")
        name = _pe_cstring(data, name_offset)
        if name not in seen:
            seen.add(name)
            names.append(name)
        offset += 20
    return names


def _pe_exports(data: bytes, export_rva: int, export_size: int,
                rva_to_offset, sections) -> dict[str, dict]:
    offset = rva_to_offset(export_rva)
    if offset is None or offset + 40 > len(data):
        fail("PE export directory is missing or truncated")
    number_of_names = _pe_u32(data, offset + 24)
    names_rva = _pe_u32(data, offset + 32)
    number_of_functions = _pe_u32(data, offset + 20)
    if number_of_functions != number_of_names:
        fail("PE export table contains unnamed or aliased exports")
    functions_offset = rva_to_offset(_pe_u32(data, offset + 28))
    ordinals_offset = rva_to_offset(_pe_u32(data, offset + 36))
    if number_of_names == 0 or names_rva == 0:
        return {}
    if functions_offset is None or ordinals_offset is None:
        fail("PE export function/ordinal table RVA is not mappable")
    names_offset = rva_to_offset(names_rva)
    if names_offset is None:
        fail("PE export name table RVA is not mappable")
    names = {}
    ordinals = set()
    for index in range(number_of_names):
        entry = names_offset + index * 4
        if entry + 4 > len(data):
            fail("truncated PE export name table")
        name_offset = rva_to_offset(_pe_u32(data, entry))
        if name_offset is None:
            fail("PE export name RVA is not mappable")
        name = _pe_cstring(data, name_offset)
        if name in names:
            fail("duplicate PE export name")
        ordinal = _pe_u16(data, ordinals_offset + index * 2)
        if ordinal >= number_of_functions:
            fail("PE export ordinal is outside the function table")
        if ordinal in ordinals:
            fail("PE export ordinal is aliased")
        ordinals.add(ordinal)
        symbol_rva = _pe_u32(data, functions_offset + ordinal * 4)
        symbol_offset = rva_to_offset(symbol_rva)
        if not symbol_rva or symbol_offset is None:
            fail("PE export target RVA is not mappable")
        section = next((item for item in sections
                        if item[0] <= symbol_rva < item[0] + item[2]), None)
        assert section is not None
        available = min(8, section[0] + section[2] - symbol_rva, len(data) - symbol_offset)
        names[name] = {
            "forwarded": export_rva <= symbol_rva < export_rva + export_size,
            "executable": bool(section[4] & 0x20000000),
            "readable": bool(section[4] & 0x40000000),
            "data": data[symbol_offset:symbol_offset + available].hex(),
        }
    return names


def audit_windows_pe(path: Path, logical_name: str, *, is_dll: bool,
                     source_root: Path | None) -> dict:
    if path.is_symlink() or not path.is_file():
        fail(f"missing Windows artifact: {path}")
    data = path.read_bytes()
    pe = parse_pe(data)
    if pe["machine"] != IMAGE_FILE_MACHINE_AMD64:
        fail(f"{logical_name} is not an AMD64 PE image")
    if pe["magic"] != PE32_PLUS_MAGIC:
        fail(f"{logical_name} is not a PE32+ image")
    if pe["timestamp"] != 0:
        fail(f"{logical_name} does not have a zero reproducible PE timestamp")
    if bool(pe["characteristics"] & IMAGE_FILE_DLL) != is_dll:
        fail(f"{logical_name} PE DLL/EXE characteristic mismatch")
    hardened = IMAGE_DLLCHARACTERISTICS_DYNAMIC_BASE | IMAGE_DLLCHARACTERISTICS_NX_COMPAT
    if pe["dll_characteristics"] & hardened != hardened:
        fail(f"{logical_name} lacks ASLR/NX hardening")
    imports = sorted({name.lower() for name in pe["imports"]})
    for dll in imports:
        if dll == WINDOWS_LIBUSB_DLL:
            fail(f"{logical_name} imports {WINDOWS_LIBUSB_DLL}; libusb is statically linked")
        if dll in WINDOWS_SYSTEM_IMPORTS or dll.startswith(WINDOWS_UCRT_IMPORT_PREFIX):
            continue
        fail(f"{logical_name} imports unexpected DLL: {dll}")
    if logical_name == "px4d.exe":
        if WINDOWS_LIBUSB_MARKER not in data:
            fail(f"{logical_name} does not contain the static libusb version URL")
    elif WINDOWS_LIBUSB_MARKER in data:
        fail(f"{logical_name} contains the libusb version URL without linking libusb")
    if is_dll:
        exports = sorted(set(pe["exports"]))
        if logical_name != WINDOWS_WINSCARD_ARTIFACT:
            fail(f"unexpected Windows DLL: {logical_name}")
        if set(exports) != WINDOWS_WINSCARD_EXPORTS:
            fail(f"{logical_name} WinSCard export allowlist mismatch")
        for symbol in WINDOWS_WINSCARD_FUNCTIONS:
            entry = pe["export_details"][symbol]
            if entry["forwarded"] or not entry["executable"]:
                fail(f"{logical_name} WinSCard function is not local code: {symbol}")
        for symbol, protocol in WINDOWS_WINSCARD_PCI.items():
            entry = pe["export_details"][symbol]
            expected = struct.pack("<II", protocol, 8).hex()
            if (entry["forwarded"] or entry["executable"] or not entry["readable"] or
                    entry["data"] != expected):
                fail(f"{logical_name} PCI export is not the expected structure data: {symbol}")
    exports = sorted(set(pe["exports"])) if is_dll else []
    if source_root is not None:
        marker = str(source_root).encode()
        if marker and marker in data:
            fail(f"{logical_name} leaks the source/build path")
    record = {
        "artifact": logical_name, "format": "PE32+", "machine": "AMD64", "magic": "0x20B",
        "imports": imports, "exports": exports, "sha256": hashlib.sha256(data).hexdigest(),
    }
    if is_dll:
        record["pci_data"] = {name: {"protocol": protocol, "length": 8}
                              for name, protocol in WINDOWS_WINSCARD_PCI.items()}
    return record


def safe_member(name: str) -> PurePosixPath:
    if "\\" in name or name.startswith("/"):
        fail(f"unsafe archive member path: {name!r}")
    path = PurePosixPath(name)
    if not name or path == PurePosixPath(".") or ".." in path.parts:
        fail(f"unsafe archive member path: {name!r}")
    return path


def archive_members(archive: Path, *, forbidden: re.Pattern | None = FORBIDDEN) -> dict[str, tarfile.TarInfo]:
    if not archive.is_file():
        fail(f"archive not found: {archive}")
    members: dict[str, tarfile.TarInfo] = {}
    try:
        with tarfile.open(archive, "r:*") as stream:
            for member in stream.getmembers():
                safe_member(member.name)
                if member.name in members:
                    fail(f"duplicate archive member: {member.name}")
                if not member.isfile():
                    fail(f"archive member is not a regular file: {member.name}")
                if forbidden is not None and forbidden.search(member.name):
                    fail(f"forbidden archive member: {member.name}")
                members[member.name] = member
    except (OSError, tarfile.TarError) as error:
        fail(f"cannot read archive {archive}: {error}")
    return members


def read_archive_file(archive: Path, name: str) -> bytes:
    with tarfile.open(archive, "r:*") as stream:
        member = stream.getmember(name)
        handle = stream.extractfile(member)
        if handle is None:
            fail(f"cannot read archive member: {name}")
        return handle.read()


def verify_checksums(archive: Path, members: dict[str, tarfile.TarInfo]) -> None:
    if "SHA256SUMS" not in members:
        fail("SHA256SUMS is missing")
    try:
        text = read_archive_file(archive, "SHA256SUMS").decode("ascii")
    except (UnicodeDecodeError, OSError, tarfile.TarError) as error:
        fail(f"invalid SHA256SUMS: {error}")
    listed: dict[str, str] = {}
    for line in text.splitlines():
        if not line.strip():
            continue
        match = re.fullmatch(r"([0-9a-f]{64})  (.+)", line)
        if not match:
            fail(f"invalid SHA256SUMS line: {line!r}")
        digest, name = match.groups()
        safe_member(name)
        if name in listed:
            fail(f"duplicate checksum entry: {name}")
        listed[name] = digest
    required = set(members) - {"SHA256SUMS"}
    if listed.keys() != required:
        fail("SHA256SUMS does not list exactly every other archive member")
    with tarfile.open(archive, "r:*") as stream:
        for name, expected in listed.items():
            handle = stream.extractfile(stream.getmember(name))
            if handle is None or hashlib.sha256(handle.read()).hexdigest() != expected:
                fail(f"checksum mismatch: {name}")


def zip_members(archive: Path) -> dict[str, zipfile.ZipInfo]:
    """Validate the Windows zip envelope and return its members by name."""
    if not archive.is_file():
        fail(f"archive not found: {archive}")
    members: dict[str, zipfile.ZipInfo] = {}
    try:
        with zipfile.ZipFile(archive) as stream:
            for info in stream.infolist():
                name = info.filename
                safe_member(name)
                pure = PurePosixPath(name)
                if ":" in pure.parts[0]:
                    fail(f"unsafe archive member path: {name!r}")
                if name in members:
                    fail(f"duplicate archive member: {name}")
                if info.is_dir():
                    fail(f"archive member is not a regular file: {name}")
                mode = (info.external_attr >> 16) & 0o170000
                if mode == stat.S_IFLNK or (mode and mode not in (stat.S_IFREG,)):
                    fail(f"archive member is not a regular file: {name}")
                if not name.isascii():
                    fail(f"archive member name is not ASCII: {name!r}")
                if info.compress_type not in (zipfile.ZIP_STORED, zipfile.ZIP_DEFLATED):
                    fail(f"unsupported archive compression for {name}")
                members[name] = info
    except (OSError, zipfile.BadZipFile, UnicodeDecodeError) as error:
        fail(f"cannot read archive {archive}: {error}")
    return members


def read_zip_file(archive: Path, name: str) -> bytes:
    try:
        with zipfile.ZipFile(archive) as stream:
            return stream.read(name)
    except (KeyError, OSError, zipfile.BadZipFile) as error:
        fail(f"cannot read archive member {name}: {error}")


def verify_windows_checksums(archive: Path, members: dict[str, zipfile.ZipInfo]) -> None:
    if "SHA256SUMS" not in members:
        fail("SHA256SUMS is missing")
    try:
        text = read_zip_file(archive, "SHA256SUMS").decode("ascii")
    except UnicodeDecodeError as error:
        fail(f"invalid SHA256SUMS: {error}")
    listed: dict[str, str] = {}
    for line in text.splitlines():
        if not line.strip():
            continue
        match = re.fullmatch(r"([0-9a-f]{64})  (.+)", line)
        if not match:
            fail(f"invalid SHA256SUMS line: {line!r}")
        digest, name = match.groups()
        safe_member(name)
        if name in listed:
            fail(f"duplicate checksum entry: {name}")
        listed[name] = digest
    if listed.keys() != set(members) - {"SHA256SUMS"}:
        fail("SHA256SUMS does not list exactly every other archive member")
    for name, expected in listed.items():
        if hashlib.sha256(read_zip_file(archive, name)).hexdigest() != expected:
            fail(f"checksum mismatch: {name}")


def verify_windows_manifest(archive: Path, members: dict[str, zipfile.ZipInfo], version: str) -> dict:
    try:
        payload = read_zip_file(archive, "manifest.json").decode("utf-8")
        manifest = json.loads(payload, object_pairs_hook=reject_duplicate_json_keys,
                              parse_constant=reject_json_constant)
    except (UnicodeDecodeError, OSError, zipfile.BadZipFile, ValueError) as error:
        fail(f"invalid manifest.json: {error}")
    if not isinstance(manifest, dict):
        fail("invalid manifest.json: top-level value must be an object")
    if manifest.get("schema") != 1 or manifest.get("platform") != WINDOWS_PLATFORM:
        fail("manifest schema/platform mismatch")
    if manifest.get("architecture") != "x86_64":
        fail("manifest architecture must be x86_64 for the Windows archive")
    if manifest.get("embedded_libusb") != {"version": "1.0.30", "linkage": "static"}:
        fail("manifest libusb linkage metadata mismatch")
    if manifest.get("winscard") != {"artifact": WINDOWS_WINSCARD_ARTIFACT,
                                    "architecture": "x86_64", "deployment": "application-local"}:
        fail("manifest WinSCard metadata mismatch")
    if "libusb_dll" in manifest:
        fail("manifest names a packaged libusb DLL")
    if not isinstance(manifest.get("source_ref"), str) or not manifest["source_ref"]:
        fail("manifest source_ref metadata is missing")
    validate_version(str(manifest.get("version", "")))
    if manifest.get("version") != version:
        fail("manifest version does not match the archive name")
    if sorted(manifest.get("programs", [])) != sorted(PROGRAMS):
        fail("manifest does not describe exactly the three production programs")
    inventory = manifest.get("files")
    if not isinstance(inventory, dict):
        fail("manifest files inventory is missing")
    expected_names = set(members) - {"manifest.json", "SHA256SUMS"}
    if set(inventory) != expected_names:
        fail("manifest files inventory does not match archived payload")
    for name in sorted(expected_names):
        record = inventory[name]
        if (not isinstance(record, dict) or set(record) != {"sha256", "size"} or
                not isinstance(record["sha256"], str) or
                not re.fullmatch(r"[0-9a-f]{64}", record["sha256"]) or
                not isinstance(record["size"], int) or isinstance(record["size"], bool) or
                record["size"] < 0):
            fail(f"invalid manifest file record: {name}")
        data = read_zip_file(archive, name)
        if record["size"] != len(data) or record["sha256"] != hashlib.sha256(data).hexdigest():
            fail(f"manifest file record mismatch: {name}")
    return manifest


def audit_windows_archive(args: argparse.Namespace) -> dict:
    archive = args.archive.resolve()
    members = zip_members(archive)
    if set(members) != WINDOWS_ARCHIVE_MEMBERS:
        fail("Windows archive member allowlist mismatch; unexpected="
             f"{sorted(set(members) - WINDOWS_ARCHIVE_MEMBERS)}, "
             f"missing={sorted(WINDOWS_ARCHIVE_MEMBERS - set(members))}")
    if members["libusb/COPYING"].external_attr >> 16 & 0o777 != 0o644:
        fail("Windows archive libusb/COPYING must have mode 644")
    if hashlib.sha256(read_zip_file(archive, "libusb/COPYING")).hexdigest() != LIBUSB_COPYING_SHA256:
        fail("Windows archive libusb/COPYING does not match the verified libusb license")
    for name, expected in WINDOWS_TOOLCHAIN_LICENSE_SHA256.items():
        if members[name].external_attr >> 16 & 0o777 != 0o644:
            fail(f"Windows archive toolchain license must have mode 644: {name}")
        if hashlib.sha256(read_zip_file(archive, name)).hexdigest() != expected:
            fail(f"Windows archive toolchain license does not match the pinned text: {name}")
    try:
        notice = read_zip_file(archive, "DEPENDENCY-NOTICE.txt").decode("utf-8")
    except UnicodeDecodeError as error:
        fail(f"invalid Windows dependency notice: {error}")
    fields = notice_fields(notice)
    match = re.search(r"px4-userland-([0-9]+\.[0-9]+\.[0-9]+)-windows-x86_64\.zip", archive.name)
    if not match:
        fail(f"Windows archive name does not match the platform/version pattern: {archive.name}")
    version = match.group(1)
    required_fields = {
        "dependency.libusb.version": "1.0.30",
        "dependency.libusb.linkage": "static",
        "dependency.libusb.license": "LGPL-2.1-or-later",
        "dependency.toolchain": "llvm-mingw-20250910-ucrt-x86_64",
        "dependency.toolchain.licenses": WINDOWS_TOOLCHAIN_LICENSE_NOTICE,
        "dependency.winscard.artifact": WINDOWS_WINSCARD_ARTIFACT,
        "dependency.winscard.architecture": "x86_64",
        "corresponding-source-archive": f"px4-userland-{version}-source.tar.gz",
    }
    for key, value in required_fields.items():
        if fields.get(key) != value:
            fail(f"Windows dependency notice field mismatch: {key}={value}")
    if "dependency.libusb.dll" in fields:
        fail("Windows dependency notice still names a libusb DLL")
    source_root = args.repo_root.resolve() if getattr(args, "repo_root", None) else None
    records = {}
    with tempfile.TemporaryDirectory(prefix="px4-windows-audit-") as temporary:
        for name in (*WINDOWS_PROGRAM_ARTIFACTS, WINDOWS_WINSCARD_ARTIFACT):
            path = Path(temporary) / name
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_bytes(read_zip_file(archive, name))
            records[name] = audit_windows_pe(path, name, is_dll=name == WINDOWS_WINSCARD_ARTIFACT,
                                             source_root=source_root)
    for name in (*WINDOWS_PROGRAM_ARTIFACTS, WINDOWS_WINSCARD_ARTIFACT):
        if members[name].external_attr >> 16 & 0o777 not in (0o755, 0o644):
            fail(f"Windows archive program has an unexpected mode: {name}")
    try:
        evidence_payload = read_zip_file(archive, "evidence/binary-audit.json").decode("utf-8")
        evidence = json.loads(evidence_payload, object_pairs_hook=reject_duplicate_json_keys,
                              parse_constant=reject_json_constant)
    except (UnicodeDecodeError, ValueError) as error:
        fail(f"invalid Windows binary evidence: {error}")
    if not isinstance(evidence, dict) or set(evidence) != {"platform", "programs", "extra"}:
        fail("Windows binary evidence schema has unknown or missing fields")
    if evidence.get("platform") != WINDOWS_PLATFORM:
        fail("Windows binary evidence platform mismatch")
    programs = evidence.get("programs")
    if not isinstance(programs, list) or len(programs) != len(PROGRAMS):
        fail("Windows binary evidence must contain exactly three program records")
    program_names = []
    for record in programs:
        if not isinstance(record, dict) or record.get("format") != "PE32+":
            fail("invalid Windows binary evidence record")
        if record.get("artifact") not in WINDOWS_PROGRAM_ARTIFACTS:
            fail("invalid Windows binary evidence artifact name")
        program_names.append(record["artifact"])
    if set(program_names) != set(WINDOWS_PROGRAM_ARTIFACTS) or len(set(program_names)) != len(PROGRAMS):
        fail("Windows binary evidence must contain each executable exactly once")
    extra = evidence.get("extra")
    if (not isinstance(extra, list) or len(extra) != 1 or not isinstance(extra[0], dict) or
            extra[0].get("artifact") != WINDOWS_WINSCARD_ARTIFACT):
        fail("Windows binary evidence must contain exactly the WinSCard DLL")
    for record in programs + extra:
        name = record["artifact"]
        if record != records[name]:
            fail(f"Windows binary evidence record mismatch: {name}")
    manifest = verify_windows_manifest(archive, members, version)
    verify_windows_checksums(archive, members)
    return {"archive": str(archive), "platform": WINDOWS_PLATFORM,
            "members": sorted(members), "manifest": manifest}


def synthetic_pe(*, imports: tuple[str, ...] = (), exports: tuple[str, ...] = (),
                 data_exports: dict[str, bytes] | None = None,
                 machine: int = IMAGE_FILE_MACHINE_AMD64, magic: int = PE32_PLUS_MAGIC,
                 timestamp: int = 0, dll: bool = False,
                 dll_characteristics: int = (IMAGE_DLLCHARACTERISTICS_DYNAMIC_BASE |
                                             IMAGE_DLLCHARACTERISTICS_NX_COMPAT),
                 poison: bytes = b"") -> bytes:
    """Build a minimal PE32+ image for offline audit self-tests only."""
    section_rva = 0x1000
    content = bytearray()

    def align4() -> None:
        while len(content) % 4:
            content.append(0)

    descriptors = len(content)
    content.extend(b"\0" * (20 * (len(imports) + 1)))
    import_name_rvas = []
    for name in imports:
        import_name_rvas.append(section_rva + len(content))
        content.extend(name.encode("ascii") + b"\0")
    for index, rva in enumerate(import_name_rvas):
        struct.pack_into("<I", content, descriptors + index * 20 + 12, rva)
    import_rva = section_rva + descriptors if imports else 0
    export_rva = 0
    data_exports = data_exports or {}
    all_exports = tuple(exports) + tuple(data_exports)
    code_rva = 0x2000
    if all_exports:
        align4()
        export_dir = len(content)
        content.extend(b"\0" * 40)
        functions_table = len(content)
        content.extend(b"\0" * (4 * len(all_exports)))
        names_table = len(content)
        content.extend(b"\0" * (4 * len(all_exports)))
        ordinals_table = len(content)
        content.extend(b"\0" * (2 * len(all_exports)))
        export_name_rvas = []
        for name in all_exports:
            export_name_rvas.append(section_rva + len(content))
            content.extend(name.encode("ascii") + b"\0")
        struct.pack_into("<I", content, export_dir + 20, len(all_exports))
        struct.pack_into("<I", content, export_dir + 24, len(all_exports))
        struct.pack_into("<I", content, export_dir + 28, section_rva + functions_table)
        struct.pack_into("<I", content, export_dir + 32, section_rva + names_table)
        struct.pack_into("<I", content, export_dir + 36, section_rva + ordinals_table)
        for index, rva in enumerate(export_name_rvas):
            struct.pack_into("<I", content, names_table + index * 4, rva)
            struct.pack_into("<H", content, ordinals_table + index * 2, index)
            name = all_exports[index]
            if name in data_exports:
                align4()
                target = section_rva + len(content)
                content.extend(data_exports[name])
            else:
                target = code_rva + index
            struct.pack_into("<I", content, functions_table + index * 4, target)
        export_rva = section_rva + export_dir
    content.extend(poison)
    section_size = len(content)

    optional_size = 0xF0
    pe_offset = 0x40
    optional_offset = pe_offset + 24
    section_header_offset = optional_offset + optional_size
    raw_pointer = 0x200
    code_pointer = raw_pointer + ((section_size + 0x1FF) & ~0x1FF)
    image = bytearray(code_pointer + 0x200)
    image[0:2] = b"MZ"
    struct.pack_into("<I", image, 0x3C, pe_offset)
    image[pe_offset:pe_offset + 4] = b"PE\0\0"
    characteristics = 0x22 | (IMAGE_FILE_DLL if dll else 0)
    struct.pack_into("<HHIIIHH", image, pe_offset + 4, machine, 2, timestamp, 0, 0,
                     optional_size, characteristics)
    struct.pack_into("<H", image, optional_offset, magic)
    struct.pack_into("<I", image, optional_offset + 16, section_rva)
    struct.pack_into("<I", image, optional_offset + 20, section_rva)
    struct.pack_into("<Q", image, optional_offset + 24, 0x140000000)
    struct.pack_into("<II", image, optional_offset + 32, 0x1000, 0x200)
    struct.pack_into("<II", image, optional_offset + 56, section_rva + ((section_size + 0xFFF) & ~0xFFF), raw_pointer)
    struct.pack_into("<H", image, optional_offset + 68, 3)
    struct.pack_into("<H", image, optional_offset + 70, dll_characteristics)
    struct.pack_into("<I", image, optional_offset + 108, 16)
    directories = optional_offset + 112
    if export_rva:
        struct.pack_into("<II", image, directories, export_rva, 40)
    if import_rva:
        struct.pack_into("<II", image, directories + 8, import_rva, 20 * (len(imports) + 1))
    image[section_header_offset:section_header_offset + 8] = b".rdata\0\0"
    struct.pack_into("<IIII", image, section_header_offset + 8, section_size, section_rva,
                     section_size, raw_pointer)
    struct.pack_into("<I", image, section_header_offset + 36, 0x40000040)
    code_header = section_header_offset + 40
    image[code_header:code_header + 8] = b".text\0\0\0"
    struct.pack_into("<IIII", image, code_header + 8, 0x200, code_rva, 0x200, code_pointer)
    struct.pack_into("<I", image, code_header + 36, 0x60000020)
    image[raw_pointer:raw_pointer + section_size] = content
    image[code_pointer:code_pointer + 0x200] = b"\xc3" * 0x200
    return bytes(image)


def notice_fields(text: str) -> dict[str, str]:
    fields = {}
    for line in text.splitlines():
        if "=" not in line:
            continue
        key, value = line.split("=", 1)
        if re.fullmatch(r"[a-z0-9.-]+", key):
            fields[key] = value
    return fields


def reject_duplicate_json_keys(pairs: list[tuple[str, object]]) -> dict[str, object]:
    result: dict[str, object] = {}
    for key, value in pairs:
        if key in result:
            raise ValueError(f"duplicate JSON object key: {key}")
        result[key] = value
    return result


def reject_json_constant(value: str) -> None:
    raise ValueError(f"invalid JSON constant: {value}")


def read_json_archive_file(archive: Path, name: str) -> dict:
    try:
        payload = read_archive_file(archive, name).decode("utf-8")
        value = json.loads(
            payload,
            object_pairs_hook=reject_duplicate_json_keys,
            parse_constant=reject_json_constant,
        )
    except (KeyError, UnicodeDecodeError, OSError, tarfile.TarError, ValueError) as error:
        fail(f"invalid {name}: {error}")
    if not isinstance(value, dict):
        fail(f"invalid {name}: top-level value must be an object")
    return value


def validate_string_list(value: object, field: str) -> list[str]:
    if (not isinstance(value, list) or
            any(not isinstance(item, str) for item in value) or
            len(set(value)) != len(value)):
        fail(f"invalid binary evidence {field}")
    return value


def validate_binary_evidence_record(record: object, platform: str) -> dict:
    if not isinstance(record, dict):
        fail("binary evidence artifact record must be an object")
    if platform.startswith("linux-"):
        fields = {"artifact", "format", "interpreter", "needed", "sha256", "libc", "glibc_floor"}
        if set(record) != fields or record.get("format") != "ELF":
            fail("invalid Linux binary evidence schema")
        if not isinstance(record.get("interpreter"), (str, type(None))):
            fail("invalid Linux binary evidence interpreter")
        validate_string_list(record.get("needed"), "needed")
        if record.get("libc") not in ("glibc", "musl"):
            fail("invalid Linux binary evidence libc")
        if record.get("glibc_floor") not in (None, "2.31"):
            fail("invalid Linux binary evidence glibc floor")
    elif platform == "darwin-arm64":
        fields = {"artifact", "format", "install_id", "dependencies", "dwarf_sections", "nlocalsym", "local_metadata", "sha256"}
        if set(record) != fields or record.get("format") != "Mach-O":
            fail("invalid macOS binary evidence schema")
        if not isinstance(record.get("install_id"), (str, type(None))):
            fail("invalid macOS binary evidence install_id")
        validate_string_list(record.get("dependencies"), "dependencies")
        validate_string_list(record.get("dwarf_sections"), "dwarf_sections")
        if record["dwarf_sections"]:
            fail("macOS binary evidence records DWARF sections")
        if (isinstance(record.get("nlocalsym"), bool) or
                not isinstance(record.get("nlocalsym"), int) or
                record["nlocalsym"] not in (0, 1)):
            fail("invalid macOS binary evidence local symbol count")
        local_metadata = validate_string_list(record.get("local_metadata"), "local_metadata")
        if len(local_metadata) != record["nlocalsym"] or any(
                not re.fullmatch(r"radr://[0-9]+", item) for item in local_metadata):
            fail("invalid macOS local symbol metadata")
    else:
        fields = {"artifact", "format", "interpreter", "needed", "sha256"}
        if set(record) != fields or record.get("format") != "Android ELF":
            fail("invalid Android binary evidence schema")
        if not isinstance(record.get("interpreter"), str):
            fail("invalid Android binary evidence interpreter")
        validate_string_list(record.get("needed"), "needed")
    if (not isinstance(record.get("artifact"), str) or
            not isinstance(record.get("sha256"), str) or
            not re.fullmatch(r"[0-9a-f]{64}", record["sha256"])):
        fail("invalid binary evidence artifact or sha256")
    return record


def verify_binary_evidence(archive: Path, members: dict[str, tarfile.TarInfo],
                           platform: str) -> dict:
    evidence = read_json_archive_file(archive, "evidence/binary-audit.json")
    allowed_fields = {"platform", "programs", "extra"}
    if platform.startswith("android"):
        allowed_fields.add("android")
    if set(evidence) != allowed_fields:
        fail("binary evidence schema has unknown or missing fields")
    if evidence.get("platform") != platform:
        fail("binary evidence platform mismatch")

    programs = evidence.get("programs")
    if not isinstance(programs, list) or len(programs) != len(PROGRAMS):
        fail("binary evidence programs must contain exactly three records")
    program_records = [validate_binary_evidence_record(record, platform) for record in programs]
    program_names = [record["artifact"] for record in program_records]
    if set(program_names) != set(PROGRAMS) or len(set(program_names)) != len(PROGRAMS):
        fail("binary evidence programs must contain px4d, px4-ts, and px4ctl exactly once")

    expected_extra = {
        "linux-glibc-x86_64": "ifd/px4-userland-ifd.so",
        "linux-glibc-aarch64": "ifd/px4-userland-ifd.so",
        "linux-musl-x86_64": "ifd/px4-userland-ifd.so",
        "linux-musl-aarch64": "ifd/px4-userland-ifd.so",
        "darwin-arm64": DARWIN_IFD_ARTIFACT,
    }.get(platform)
    extra = evidence.get("extra")
    if not isinstance(extra, list):
        fail("binary evidence extra must be a list")
    if expected_extra is None:
        if extra:
            fail("Android binary evidence must not contain extra artifacts")
        records = program_records
    else:
        if len(extra) != 1:
            fail("native binary evidence must contain exactly one extra artifact")
        extra_record = validate_binary_evidence_record(extra[0], platform)
        if extra_record["artifact"] != expected_extra:
            fail("binary evidence extra artifact does not match platform")
        records = program_records + [extra_record]

    expected_libc = {
        "linux-glibc-x86_64": "glibc", "linux-glibc-aarch64": "glibc",
        "linux-musl-x86_64": "musl", "linux-musl-aarch64": "musl",
    }.get(platform)
    expected_interpreter = {
        "android-aarch64": "/system/bin/linker64",
        "android-armv7a": "/system/bin/linker",
        "android-x86_64": "/system/bin/linker64",
    }.get(platform)
    for record in records:
        artifact = record["artifact"]
        if artifact not in members:
            fail(f"binary evidence artifact is not archived: {artifact}")
        if platform.startswith("linux-"):
            expected_record_libc = expected_libc if artifact not in PROGRAMS else "musl"
            if record["interpreter"] is not None or record["libc"] != expected_record_libc:
                fail(f"Linux binary evidence static/libc mismatch: {artifact}")
            if artifact not in PROGRAMS and expected_libc == "glibc" and record["glibc_floor"] != "2.31":
                fail(f"glibc Linux binary evidence floor mismatch: {artifact}")
        elif platform.startswith("android") and record["interpreter"] != expected_interpreter:
            fail(f"Android binary evidence interpreter mismatch: {artifact}")
        actual = hashlib.sha256(read_archive_file(archive, artifact)).hexdigest()
        if record["sha256"] != actual:
            fail(f"binary evidence checksum mismatch: {artifact}")

    if platform.startswith("android"):
        android = evidence.get("android")
        if (not isinstance(android, dict) or set(android) != {"ndk_revision", "inventory_members"} or
                not isinstance(android.get("ndk_revision"), str)):
            fail("invalid Android binary evidence metadata schema")
        inventory_members = validate_string_list(android.get("inventory_members"), "inventory_members")
        if tuple(inventory_members) != ANDROID_INVENTORY_MEMBERS:
            fail("Android binary evidence inventory_members do not match the allowlist")
        if any(name not in members for name in inventory_members):
            fail("Android binary evidence inventory member is not archived")
    return evidence


def verify_manifest(archive: Path, members: dict[str, tarfile.TarInfo], platform: str) -> dict:
    try:
        manifest = json.loads(read_archive_file(archive, "manifest.json"))
    except (KeyError, ValueError, UnicodeDecodeError, OSError, tarfile.TarError) as error:
        fail(f"invalid manifest.json: {error}")
    if manifest.get("schema") != 1 or manifest.get("platform") != platform:
        fail("manifest schema/platform mismatch")
    expected_libc = ("glibc" if platform.startswith("linux-glibc-") else
                     "musl" if platform.startswith("linux-musl-") else
                     "android" if platform.startswith("android-") else "darwin")
    if manifest.get("libc") != expected_libc or manifest.get("architecture") != platform.rsplit("-", 1)[-1]:
        fail("manifest libc/architecture metadata mismatch")
    if not isinstance(manifest.get("source_ref"), str) or not manifest["source_ref"]:
        fail("manifest source_ref metadata is missing")
    if manifest.get("embedded_libusb") != {"version": "1.0.30", "linkage": "static"}:
        fail("manifest embedded libusb metadata mismatch")
    validate_version(str(manifest.get("version", "")))
    if sorted(manifest.get("programs", [])) != sorted(PROGRAMS):
        fail("manifest does not describe exactly the three production programs")
    inventory = manifest.get("files")
    if not isinstance(inventory, dict):
        fail("manifest files inventory is missing")
    expected_names = set(members) - {"manifest.json", "SHA256SUMS"}
    if set(inventory) != expected_names:
        fail("manifest files inventory does not match archived payload")
    with tarfile.open(archive, "r:*") as stream:
        for name in sorted(expected_names):
            record = inventory[name]
            if (not isinstance(record, dict) or set(record) != {"sha256", "size"} or
                    not isinstance(record["sha256"], str) or not re.fullmatch(r"[0-9a-f]{64}", record["sha256"]) or
                    not isinstance(record["size"], int) or isinstance(record["size"], bool) or record["size"] < 0):
                fail(f"invalid manifest file record: {name}")
            payload = stream.extractfile(stream.getmember(name))
            if payload is None:
                fail(f"cannot read manifest file: {name}")
            data = payload.read()
            if record["size"] != len(data) or record["sha256"] != hashlib.sha256(data).hexdigest():
                fail(f"manifest file record mismatch: {name}")
    return manifest


def parse_needed(dynamic: str) -> set[str]:
    return set(re.findall(r"Shared library: \[([^]]+)\]", dynamic))


def validate_reader_template(data: bytes) -> None:
    try:
        text = data.decode("utf-8")
    except UnicodeDecodeError:
        fail("reader configuration is not UTF-8")
    placeholders = set(re.findall(r"@[A-Z][A-Z0-9_]*@", text))
    if placeholders != READER_PLACEHOLDERS:
        fail(f"reader configuration placeholder mismatch: {sorted(placeholders)}")
    if text.count("@PX4_ACCESS@") != 1 or "access=@PX4_ACCESS@" not in text:
        fail("reader configuration must expose exactly one PX4 access placeholder")


def readelf_path() -> str:
    for candidate in ("readelf", "llvm-readelf"):
        found = next((part for part in os.get_exec_path() if Path(part, candidate).is_file()), None)
        if found:
            return str(Path(found, candidate))
    fail("readelf or llvm-readelf is required")


def audit_elf_sections(path: Path, readelf: str, *, reject_build_id: bool) -> None:
    sections = run([readelf, "-SW", str(path)])
    names = re.findall(r"^\s*\[\s*\d+\]\s+(\S+)", sections, re.MULTILINE)
    if reject_build_id and ".note.gnu.build-id" in names:
        fail(f"GNU Build ID section is forbidden in release ELF: {path}")
    forbidden = [name for name in names if name.startswith((".debug", ".zdebug")) or
                 name in (".symtab", ".symtab_shndx")]
    if forbidden:
        fail(f"debug or regular symbol sections are forbidden: {path}: {forbidden}")


def audit_linux(path: Path, logical_name: str, *, platform: str, shared: bool, require_libusb: bool,
                reject_build_id: bool, reject_pcsc: bool = False) -> dict:
    target = LINUX_TARGETS[platform]
    readelf = readelf_path()
    audit_elf_sections(path, readelf, reject_build_id=reject_build_id)
    header = run([readelf, "-h", str(path)])
    program = run([readelf, "-lW", str(path)])
    dynamic = run([readelf, "-d", str(path)])
    if "ELF" not in header:
        fail(f"not an ELF file: {path}")
    machine = re.search(r"^\s*Machine:\s*(.+)$", header, re.MULTILINE)
    if not machine or machine.group(1).strip() != target["machine"]:
        fail(f"wrong Linux ELF machine for {platform}: {path}")
    interpreter = re.search(r"Requesting program interpreter: ([^]]+)", program)
    if not shared and interpreter:
        fail(f"static Linux executable has PT_INTERP: {path}")
    if shared and interpreter:
        fail(f"Linux IFD shared object has PT_INTERP: {path}")
    needed = parse_needed(dynamic)
    if not shared and needed:
        fail(f"static Linux executable has DT_NEEDED: {path}: {sorted(needed)}")
    if shared:
        if target["needed"] is None:
            if needed != {target["libc"]}:
                fail(f"musl IFD must only require matching libc: {path}: {sorted(needed)}")
        elif target["libc"] not in needed or not needed <= target["needed"]:
            fail(f"glibc IFD has unexpected host ABI dependency: {path}: {sorted(needed)}")
    elif require_libusb:
        # Static libusb is intentionally present in the executable, so it must
        # not appear as a dynamic dependency.
        if "libusb-1.0.so.0" in needed:
            fail(f"static executable has shared libusb dependency: {path}")
    if reject_pcsc and any("pcsc" in library.lower() for library in needed):
        fail(f"unexpected direct PC/SC client dependency: {path}")
    if "RPATH" in dynamic or "RUNPATH" in dynamic:
        fail(f"RPATH/RUNPATH is forbidden: {path}")
    versions = run([readelf, "--version-info", str(path)])
    glibc_versions = [tuple(int(part) for part in value.split("."))
                      for value in re.findall(r"GLIBC_([0-9]+(?:\.[0-9]+)+)", versions)]
    if shared and target["floor"] and glibc_versions and max(glibc_versions) > tuple(int(p) for p in target["floor"].split(".")):
        fail(f"glibc IFD exceeds floor {target['floor']}: {path}")
    return {"artifact": logical_name, "format": "ELF",
            "interpreter": None, "needed": sorted(needed),
            "libc": ("musl" if not shared else
                     "glibc" if target["libc"] == "libc.so.6" else "musl"),
            "glibc_floor": target["floor"] if shared else None}


def audit_macho_load_commands(path: Path) -> tuple[list[str], int]:
    load_commands = run(["otool", "-l", str(path)])
    if re.search(r"^\s*segname\s+__DWARF\s*$", load_commands, re.MULTILINE):
        fail(f"DWARF segment is forbidden: {path}")
    dwarf_sections = re.findall(r"^\s*sectname\s+__(?:debug|zdebug)[^\s]*\s*$",
                                load_commands, re.MULTILINE)
    if dwarf_sections:
        fail(f"DWARF sections are forbidden: {path}: {dwarf_sections}")
    dysymtab_commands = re.findall(r"^\s*cmd\s+LC_DYSYMTAB\s*$", load_commands, re.MULTILINE)
    if len(dysymtab_commands) != 1:
        fail(f"Mach-O must contain exactly one LC_DYSYMTAB: {path}: {len(dysymtab_commands)}")
    dysymtab = re.search(r"^\s*cmd\s+LC_DYSYMTAB\s*$.*?(?=^\s*cmd\s+\S|\Z)",
                         load_commands, re.MULTILINE | re.DOTALL)
    if not dysymtab:
        fail(f"LC_DYSYMTAB is missing: {path}")
    local_symbols = re.search(r"^\s*nlocalsym\s+(\d+)\s*$", dysymtab.group(0), re.MULTILINE)
    if not local_symbols:
        fail(f"LC_DYSYMTAB nlocalsym is missing: {path}")
    nlocalsym = int(local_symbols.group(1))
    if nlocalsym > 1:
        fail(f"too many local Mach-O symbols: {path}: nlocalsym={nlocalsym}")
    return dwarf_sections, nlocalsym


def audit_macho_local_metadata(path: Path) -> list[str]:
    output = run([nm_path(), "-ap", str(path)])
    metadata = []
    for line in output.splitlines():
        fields = line.split()
        if not fields:
            continue
        radr_fields = [field for field in fields if field.startswith("radr://")]
        if radr_fields:
            if (len(fields) < 2 or fields[-2] != "OPT" or
                    not re.fullmatch(r"radr://[0-9]+", fields[-1]) or
                    len(radr_fields) != 1 or fields[1] != "-"):
                fail(f"unexpected Mach-O local metadata: {path}: {line}")
            metadata.append(fields[-1])
            continue
        symbol_type = fields[1] if len(fields) > 1 and len(fields[0]) > 1 else fields[0]
        if symbol_type == "-" or symbol_type.islower():
            fail(f"unexpected local Mach-O symbol: {path}: {line}")
    if len(metadata) != 1:
        fail(f"expected exactly one radr:// N_OPT metadata symbol: {path}: {metadata}")
    return metadata


def nm_path() -> str:
    for candidate in ("nm", "llvm-nm"):
        found = next((part for part in os.get_exec_path() if Path(part, candidate).is_file()), None)
        if found:
            return str(Path(found, candidate))
    fail("nm or llvm-nm is required for the macOS IFD audit")


def audit_macho_ifd_exports(path: Path) -> None:
    output = run([nm_path(), "-gU", str(path)])
    symbols = {line.split()[-1].lstrip("_") for line in output.splitlines() if line.split()}
    expected = set(IFD_EXPORTS)
    if symbols != expected:
        fail(f"macOS IFD export set mismatch: {path}: {sorted(symbols)}")


DARWIN_SYSTEM_PREFIXES = ("/usr/lib/", "/System/Library/")


def audit_darwin(path: Path, logical_name: str, *, reject_pcsc: bool = False) -> dict:
    dwarf_sections, nlocalsym = audit_macho_load_commands(path)
    local_metadata = audit_macho_local_metadata(path) if nlocalsym == 1 else []
    if logical_name == DARWIN_IFD_ARTIFACT:
        audit_macho_ifd_exports(path)
    output = run(["otool", "-L", str(path)])
    own_id = None
    if logical_name.endswith(".dylib"):
        install_output = run(["otool", "-D", str(path)])
        install_lines = [line.strip() for line in install_output.splitlines() if line.strip()]
        own_id = install_lines[1] if len(install_lines) > 1 else None
    lines = [line.strip() for line in output.splitlines()[1:] if line.strip()]
    first_name = lines[0].split(" (", 1)[0] if lines else None
    if own_id is None and logical_name.endswith(".dylib") and first_name:
        if PurePosixPath(first_name).name == PurePosixPath(logical_name).name:
            own_id = first_name
    if own_id and lines and first_name == own_id:
        lines = lines[1:]
    dependencies = [line.split(" (", 1)[0] for line in lines]
    logical_dependencies = sorted({PurePosixPath(dependency).name for dependency in dependencies})
    # libusb is statically linked into px4d; no macOS artifact may load it.
    if any("libusb-1.0" in dependency for dependency in logical_dependencies):
        fail(f"unexpected direct dynamic libusb dependency: {path}")
    if reject_pcsc and any("pcsc" in dependency.lower() for dependency in logical_dependencies):
        fail(f"unexpected direct PC/SC client dependency: {path}")
    if any(dependency.startswith("@loader_path/") or dependency.startswith("@rpath/")
           for dependency in dependencies):
        fail(f"bundled/rpath dependency is forbidden: {path}")
    host_dependencies = [dependency for dependency in dependencies
                         if not dependency.startswith(DARWIN_SYSTEM_PREFIXES)]
    if host_dependencies:
        fail(f"non-system macOS dependency is forbidden: {path}: {host_dependencies}")
    stable_install_id = None if own_id is None else (own_id if own_id.startswith("@") else PurePosixPath(own_id).name)
    return {"artifact": logical_name, "format": "Mach-O", "install_id": stable_install_id,
            "dependencies": logical_dependencies, "dwarf_sections": dwarf_sections,
            "nlocalsym": nlocalsym, "local_metadata": local_metadata}


def audit_archive_elf_sections(archive: Path, members: dict[str, tarfile.TarInfo],
                               platform: str) -> None:
    # Darwin DWARF inspection is performed by audit_binaries on macOS. The
    # resulting dwarf_sections/local-symbol fields are checked against each
    # archived binary's SHA-256 below, so this cross-platform archive audit
    # must not attempt to run the Darwin-only otool.
    if not platform.startswith(("linux-", "android-")):
        return
    # Only px4d has the Build ID reproducibility contract. The other two
    # production programs and the Linux IFD intentionally keep their baseline
    # metadata, including any GNU Build ID.
    binary_names = [(program, program == "px4d") for program in PROGRAMS]
    if platform.startswith("linux-"):
        binary_names.append(("ifd/px4-userland-ifd.so", False))
    with tempfile.TemporaryDirectory(prefix="px4-archive-binary-audit-") as temporary:
        root = Path(temporary)
        for name, reject_build_id in binary_names:
            if name not in members:
                fail(f"binary audit member is missing: {name}")
            path = root / PurePosixPath(name)
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_bytes(read_archive_file(archive, name))
            audit_elf_sections(path, readelf_path(), reject_build_id=reject_build_id)


def audit_termux_launcher(payload: bytes, mode: int) -> None:
    if mode != 0o755:
        fail(f"Termux launcher must have mode 0755, got {mode:o}")
    try:
        text = payload.decode("utf-8")
    except UnicodeDecodeError:
        fail("Termux launcher is not UTF-8 text")
    if not text.startswith("#!/data/data/com.termux/files/usr/bin/sh\n"):
        fail("Termux launcher must use the Termux shell shebang")
    if "# SPDX-License-Identifier: GPL-2.0-only" not in text.splitlines()[:3]:
        fail("Termux launcher is missing its SPDX license identifier")
    if re.search(r"(^|[^A-Za-z0-9_])eval([^A-Za-z0-9_]|$)", text):
        fail("Termux launcher must not use shell eval")
    for required in ("command -v termux-usb", "command -v setsid",
                     "setsid termux-usb"):
        if required not in text:
            fail(f"Termux launcher is missing required operation: {required}")
    run(["sh", "-n"], input_text=text)


def audit_android(path: Path, logical_name: str, expected_interpreter: str,
                  expected_arch: str, root: Path, *, reject_build_id: bool) -> dict:
    verifier = root / "scripts" / "verify-android-elf.sh"
    if not verifier.is_file():
        fail(f"missing existing Android ELF verifier: {verifier}")
    run([str(verifier), str(path), expected_interpreter, expected_arch])
    readelf = readelf_path()
    audit_elf_sections(path, readelf, reject_build_id=reject_build_id)
    dynamic = run([readelf, "-d", str(path)])
    return {"artifact": logical_name, "format": "Android ELF",
            "interpreter": expected_interpreter, "needed": sorted(parse_needed(dynamic))}


def audit_binaries(args: argparse.Namespace) -> dict:
    validate_platform(args.platform)
    build = args.build_dir.resolve()
    if not build.is_dir():
        fail(f"build directory not found: {build}")
    result: dict = {"platform": args.platform, "programs": [], "extra": []}
    for program in PROGRAMS:
        if args.platform == WINDOWS_PLATFORM:
            path = build / f"{program}.exe"
        else:
            path = build / f"{program}{('-' + args.binary_suffix) if args.binary_suffix else ''}"
        if not path.is_file() or path.is_symlink():
            fail(f"missing production program: {path}")
        if args.platform == WINDOWS_PLATFORM:
            evidence = audit_windows_pe(path, f"{program}.exe", is_dll=False,
                                        source_root=args.repo_root.resolve())
        elif args.platform.startswith("android"):
            expected, architecture = {
                "android-aarch64": ("/system/bin/linker64", "aarch64"),
                "android-armv7a": ("/system/bin/linker", "armv7a"),
                "android-x86_64": ("/system/bin/linker64", "x86_64"),
            }[args.platform]
            evidence = audit_android(path, program, expected, architecture, args.repo_root.resolve(),
                                     reject_build_id=program == "px4d")
        elif args.platform.startswith("linux-"):
            evidence = audit_linux(path, program, platform=args.platform, shared=False,
                                   require_libusb=program == "px4d", reject_build_id=program == "px4d")
        else:
            evidence = audit_darwin(path, program)
        evidence["sha256"] = sha256(path)
        result["programs"].append(evidence)

    if args.platform == WINDOWS_PLATFORM:
        result["extra"].append(audit_windows_pe(
            build / WINDOWS_WINSCARD_ARTIFACT, WINDOWS_WINSCARD_ARTIFACT,
            is_dll=True, source_root=args.repo_root.resolve()))
    elif args.platform.startswith("linux-"):
        if not args.ifd_library:
            fail("--ifd-library is required for Linux binary audit")
        path = args.ifd_library.resolve()
        if not path.is_file() or path.is_symlink():
            fail(f"missing Linux IFD library: {path}")
        evidence = audit_linux(path, "ifd/px4-userland-ifd.so", platform=args.platform, shared=True,
                               require_libusb=False, reject_build_id=False, reject_pcsc=True)
        evidence["sha256"] = sha256(path)
        result["extra"].append(evidence)
    elif args.platform == "darwin-arm64":
        if not args.ifd_bundle:
            fail("--ifd-bundle is required for macOS binary audit")
        path = args.ifd_bundle.resolve() / "Contents" / "MacOS" / "libpx4-userland-ifd.dylib"
        if not path.is_file() or path.is_symlink():
            fail(f"missing macOS IFD bundle executable: {path}")
        evidence = audit_darwin(
            path, "ifd/px4-userland-ifd.bundle/Contents/MacOS/libpx4-userland-ifd.dylib",
            reject_pcsc=True)
        evidence["sha256"] = sha256(path)
        result["extra"].append(evidence)

    if args.platform.startswith("android"):
        if not args.link_map_dir or not args.ndk_root:
            fail("Android binary audit requires explicit --link-map-dir and --ndk-root")
        link_maps = args.link_map_dir.resolve()
        ndk = args.ndk_root.resolve()
        properties = ndk / "source.properties"
        notice = ndk / "NOTICE"
        toolchain_notice = ndk / "NOTICE.toolchain"
        for required in (properties, notice, toolchain_notice):
            if not required.is_file():
                fail(f"required NDK file is absent: {required}")
        revision_line = next((line for line in properties.read_text().splitlines()
                              if line.startswith("Pkg.Revision = ")), "")
        revision = revision_line.split("=", 1)[1].strip() if revision_line else ""
        if not revision.startswith("27."):
            fail(f"NDK r27 is required, got {revision or 'unknown'}")
        inventory_dir = args.inventory_dir.resolve() if args.inventory_dir else None
        if inventory_dir:
            inventory_dir.mkdir(parents=True, exist_ok=True)
        inventories = []
        inventory_tool = args.repo_root.resolve() / "scripts" / "android-link-inventory.py"
        for program in PROGRAMS:
            link_map = link_maps / f"{program}.map"
            if not link_map.is_file() or link_map.is_symlink():
                fail(f"missing explicit Android link map: {link_map}")
            if inventory_dir is None:
                fail("--inventory-dir is required for Android binary audit")
            inventory = inventory_dir / f"{program}-static-archives.tsv"
            command = [sys.executable, str(inventory_tool), "--map", str(link_map), "--output", str(inventory)]
            if program == "px4d":
                command.append("--require-libusb")
            run(command)
            text = inventory.read_text(encoding="utf-8")
            if "ndk-runtime\tlibc++_static.a\t" not in text:
                fail(f"NDK libc++ member inventory is missing for {program}")
            if program == "px4d" and "libusb\tlibusb-1.0.a\t" not in text:
                fail("static libusb member inventory is missing for px4d")
            inventories.append(f"evidence/inventory/{program}-static-archives.tsv")
        result["android"] = {"ndk_revision": revision, "inventory_members": inventories}
    return result


def verify_linux_mdev_modes(modes: dict[str, int], platform: str) -> None:
    if not platform.startswith("linux-"):
        return
    expected_modes = {
        "mdev/px4-userland-mdev.conf": 0o644,
        "mdev/px4-userland-mdev.sh": 0o755,
        "mdev/px4-userland-mdev.start": 0o755,
    }
    for name, expected_mode in expected_modes.items():
        actual_mode = modes.get(name)
        if actual_mode != expected_mode:
            fail(f"Linux mdev file has mode {actual_mode!r}, expected {expected_mode:o}: {name}")


def verify_linux_runtime_licenses(payloads: dict[str, bytes], modes: dict[str, int]) -> None:
    for name, expected_hash in LINUX_RUNTIME_LICENSE_SHA256.items():
        if name not in payloads or hashlib.sha256(payloads[name]).hexdigest() != expected_hash:
            fail(f"Linux runtime license is missing or altered: {name}")
        if modes.get(name) != 0o644:
            fail(f"Linux runtime license mode mismatch: {name}")


def verify_linux_toolchain_properties(properties: dict[str, str], name: str, platform: str) -> None:
    for key in ("compiler", "compiler_sha256", "compiler_version", "compiler_target",
                "libc", "libc_version", "runtime", "linkage"):
        if not properties.get(key) or properties[key] == "unknown":
            fail(f"Linux toolchain metadata is missing an actual value: {name}:{key}")
    if not properties["compiler"].startswith("/"):
        fail(f"Linux toolchain compiler path is not absolute: {name}")
    if properties["runtime"] != "libgcc+libstdc++" or \
            not re.fullmatch(r"[0-9a-f]{64}", properties["compiler_sha256"]):
        fail(f"Linux toolchain metadata does not bind the selected driver and static runtimes: {name}")
    expected_target = "aarch64" if platform.endswith("aarch64") else "x86_64"
    if expected_target not in properties["compiler_target"]:
        fail(f"Linux compiler target does not match archive platform: {name}")
    runtime_inputs = (
        ("libc_archive", "libc_archive_sha256", "libc_archive_owner"),
        ("libgcc_archive", "libgcc_archive_sha256", "libgcc_archive_owner"),
        ("libstdcxx_archive", "libstdcxx_archive_sha256", "libstdcxx_archive_owner"),
    ) if "static" in name else (
        ("libc_input", "libc_input_sha256", "libc_input_owner"),
        ("libgcc_archive", "libgcc_archive_sha256", "libgcc_archive_owner"),
        ("libstdcxx_archive", "libstdcxx_archive_sha256", "libstdcxx_archive_owner"),
    )
    for path_key, hash_key, owner_key in runtime_inputs:
        if not properties.get(path_key, "").startswith("/") or not properties.get(owner_key):
            fail(f"Linux runtime input provenance is incomplete: {name}:{path_key}")
        if not re.fullmatch(r"[0-9a-f]{64}", properties.get(hash_key, "")):
            fail(f"Linux runtime input hash is invalid: {name}:{hash_key}")
    if "ifd" in name and any(not properties.get(key) for key in (
            "libgcc_package_version", "libstdcxx_package_version", "libc_input_package_version")):
        fail(f"Linux IFD input package versions are incomplete: {name}")
    if name.endswith("toolchain-static.properties"):
        if not properties["libc_version"].startswith("musl-1.2.5-r") or \
                not properties["libc_archive_owner"].startswith("musl-dev-1.2.5-r"):
            fail("Linux executable musl input does not match the distributed copyright")
    elif properties["libc"] == "musl":
        if not properties["libc_version"].startswith("musl-1.2.5-r") or \
                not properties["libc_input_owner"].startswith("musl-1.2.5-r") or \
                properties["libc_input_package_version"] != properties["libc_input_owner"]:
            fail("Linux musl IFD input does not match the distributed copyright")
    elif properties["libc_version"] != "glibc 2.31" or not properties.get("libc_input_package_version"):
        fail("Linux glibc IFD metadata does not match the supported baseline")


def audit_binary_archive(args: argparse.Namespace) -> dict:
    validate_platform(args.platform)
    if args.platform == WINDOWS_PLATFORM:
        return audit_windows_archive(args)
    members = archive_members(args.archive.resolve())
    expected = set(COMMON) | set(PROGRAMS)
    if args.platform.startswith("linux-"):
        expected |= {"ifd/px4-userland-ifd.so", "reader.conf.d/px4-userland.conf",
                     "evidence/toolchain-static.properties", "evidence/toolchain-ifd.properties"} | LINUX_MDEV
        expected |= LINUX_RUNTIME_LICENSE_MEMBERS
    elif args.platform == "darwin-arm64":
        expected |= {
            "ifd/px4-userland-ifd.bundle/Contents/Info.plist",
            "ifd/px4-userland-ifd.bundle/Contents/MacOS/libpx4-userland-ifd.dylib",
            "reader.conf.d/px4-userland.conf",
        }
    else:
        expected |= {"ndk/NOTICE", "ndk/NOTICE.toolchain", "ndk/source.properties"}
        expected.add(TERMUX_LAUNCHER)
        for program in PROGRAMS:
            expected.add(f"evidence/inventory/{program}-static-archives.tsv")
    if set(members) != expected:
        fail(f"archive member allowlist mismatch; unexpected={sorted(set(members)-expected)}, missing={sorted(expected-set(members))}")
    if members["libusb/COPYING"].mode != 0o644:
        fail(f"binary archive libusb/COPYING must have mode 644, got {members['libusb/COPYING'].mode:o}")
    if hashlib.sha256(read_archive_file(args.archive.resolve(), "libusb/COPYING")).hexdigest() != LIBUSB_COPYING_SHA256:
        fail("binary archive libusb/COPYING does not match the verified libusb 1.0.30 license")
    verify_linux_mdev_modes({name: member.mode for name, member in members.items()}, args.platform)
    if args.platform.startswith("linux-"):
        verify_linux_runtime_licenses(
            {name: read_archive_file(args.archive.resolve(), name) for name in LINUX_RUNTIME_LICENSE_MEMBERS},
            {name: member.mode for name, member in members.items()})
        for name in ("evidence/toolchain-static.properties", "evidence/toolchain-ifd.properties"):
            properties = {}
            for line in read_archive_file(args.archive.resolve(), name).decode("utf-8").splitlines():
                if "=" not in line:
                    fail(f"invalid Linux toolchain metadata: {name}")
                key, value = line.split("=", 1)
                if key in properties:
                    fail(f"duplicate Linux toolchain metadata key: {name}:{key}")
                properties[key] = value
            verify_linux_toolchain_properties(properties, name, args.platform)
    if args.platform.startswith("android"):
        audit_termux_launcher(read_archive_file(args.archive.resolve(), TERMUX_LAUNCHER),
                              members[TERMUX_LAUNCHER].mode)
    audit_archive_elf_sections(args.archive.resolve(), members, args.platform)
    manifest = verify_manifest(args.archive.resolve(), members, args.platform)
    expected_name = f"px4-userland-{manifest['version']}-{args.platform}.tar.gz"
    if args.archive.name != expected_name:
        fail(f"archive name does not match platform/version: {args.archive.name}")
    if args.platform.startswith("linux-") or args.platform == "darwin-arm64":
        validate_reader_template(read_archive_file(args.archive.resolve(), "reader.conf.d/px4-userland.conf"))
    notice = read_archive_file(args.archive.resolve(), "DEPENDENCY-NOTICE.txt").decode("utf-8")
    evidence = verify_binary_evidence(args.archive.resolve(), members, args.platform)
    if args.platform.startswith("android"):
        properties = read_archive_file(args.archive.resolve(), "ndk/source.properties").decode("utf-8")
        revision = next((line.split("=", 1)[1].strip() for line in properties.splitlines()
                         if line.startswith("Pkg.Revision = ")), "")
        if not revision.startswith("27."):
            fail(f"Android archive does not retain an NDK r27/r27d revision: {revision or 'unknown'}")
        fields = notice_fields(notice)
        required_fields = {
            "dependency.libusb.version": "1.0.30",
            "dependency.libusb.linkage": "static",
            "dependency.libusb.license": "LGPL-2.1-or-later",
            "dependency.ndk.revision": revision,
            "dependency.ndk.materials": "ndk/source.properties,ndk/NOTICE,ndk/NOTICE.toolchain",
            "corresponding-source-archive": "present",
        }
        for key, value in required_fields.items():
            if fields.get(key) != value:
                fail(f"Android dependency notice field mismatch: {key}={value}")
        android_evidence = evidence["android"]
        if android_evidence["ndk_revision"] != revision:
            fail("Android binary evidence NDK revision does not match source.properties")
        if fields.get("dependency.ndk.revision") != android_evidence["ndk_revision"]:
            fail("Android binary evidence NDK revision does not match dependency notice")
    else:
        fields = notice_fields(notice)
        if (fields.get("dependency.libusb.linkage") != "static" or
                fields.get("dependency.libusb.version") != "1.0.30" or
                fields.get("dependency.libusb.license") != "LGPL-2.1-or-later"):
            fail("native dependency notice does not describe static pinned libusb")
        if args.platform.startswith("linux-") and \
                fields.get("dependency.libc") != ("glibc" if args.platform.startswith("linux-glibc") else "musl"):
            fail("Linux dependency notice libc mismatch")
        if args.platform.startswith("linux-") and (
                fields.get("runtime-license-members") != ",".join(sorted(LINUX_RUNTIME_LICENSE_MEMBERS)) or
                fields.get("runtime-build-metadata") !=
                "evidence/toolchain-static.properties,evidence/toolchain-ifd.properties"):
            fail("Linux dependency notice does not identify runtime license and build metadata members")
        if fields.get("corresponding-source-archive") != f"px4-userland-{manifest['version']}-source.tar.gz":
            fail("native dependency notice must point to the separately published source archive")
    verify_checksums(args.archive.resolve(), members)
    return {"archive": str(args.archive.resolve()), "platform": args.platform, "members": sorted(members), "manifest": manifest}


def audit_source_archive(args: argparse.Namespace) -> dict:
    members = archive_members(args.archive.resolve(), forbidden=None)
    for name in members:
        if not name.startswith(("repository/", "third_party/libusb-1.0.30/")):
            continue
        if SOURCE_FORBIDDEN.search(name):
            fail(f"forbidden source archive member: {name}")
        if SOURCE_WINDOWS_TOKEN.search(name) and not WINDOWS_SOURCE_ALLOWED.fullmatch(name):
            fail(f"forbidden source archive member: {name}")
    # The shared foundation dependency is declared by the selected product
    # snapshot's lock file.  Historical snapshots that predate the dependency
    # are audited without it; a snapshot that declares it must ship the exact
    # pinned common source and metadata.
    common_lock = "repository/cmake/dtv-userland-core.revision"
    common_prefix = "repository/third_party/dtv-userland-core/"
    required = {
        "BUILD-RELINK.md",
        "source-manifest.json",
        "SHA256SUMS",
        "DEPENDENCY-NOTICE.txt",
        "LICENSE",
        "README.md",
        "THIRD_PARTY_NOTICES.md",
        "third_party/libusb-1.0.30.tar.bz2",
        "repository/LICENSE",
        "repository/README.md",
        "repository/THIRD_PARTY_NOTICES.md",
        "repository/scripts/build-linux-static.sh",
        "repository/scripts/build-linux-ifd.sh",
        "repository/scripts/build-macos-static.sh",
        "repository/scripts/test-static-relink.sh",
        "repository/packaging/REBUILD.md.in",
        "repository/distribution/licenses/gcc-COPYING3",
        "repository/distribution/licenses/gcc-COPYING.RUNTIME",
        "repository/distribution/licenses/musl-1.2.5-COPYRIGHT",
    }
    if common_lock in members:
        # The declared dependency must ship the build closure of the pinned
        # foundation project (its CMake entry points, public headers, and
        # sources), not just a license and provenance stub.
        required |= {common_lock, common_prefix + "LICENSE", common_prefix + "PROVENANCE.json"}
        required |= {
            common_prefix + "CMakeLists.txt",
            common_prefix + "cmake/dtv_userland_coreConfig.cmake.in",
            common_prefix + "include/dtv/userland/error.h",
            common_prefix + "include/dtv/userland/logging.h",
            common_prefix + "include/dtv/userland/transport.h",
            common_prefix + "include/dtv/userland/mock_transport.h",
            common_prefix + "include/dtv/userland/wire.h",
            common_prefix + "src/error.cpp",
            common_prefix + "src/logging.cpp",
            common_prefix + "src/mock_transport.cpp",
            common_prefix + "src/wire.cpp",
        }
        # A product that links the receiver engine needs its sources as well.
        if "repository/CMakeLists.txt" in members and "dtv::receiver" in read_archive_file(
                args.archive.resolve(), "repository/CMakeLists.txt").decode("utf-8"):
            required |= {
                common_prefix + "include/dtv/userland/detail/receiver_engine.h",
                common_prefix + "src/receiver_engine.cpp",
            }
        if "repository/CMakeLists.txt" in members and "dtv::card" in read_archive_file(
                args.archive.resolve(), "repository/CMakeLists.txt").decode("utf-8"):
            required |= {
                common_prefix + "include/dtv/userland/detail/card_ownership_engine.h",
                common_prefix + "include/dtv/userland/detail/card_ownership_engine.tpp",
            }
    winscard_document = "repository/" + WINDOWS_WINSCARD_DOCUMENT
    windows_product = any(
        name.startswith("repository/userland/src/windows/") for name in members)
    product_runtime = any(
        name in members for name in WINDOWS_PRODUCT_RUNTIME_REQUIRED)
    host_runtime_adapter = any(
        name.startswith(HOST_RUNTIME_ADAPTER_PREFIX) for name in members)
    host_ts_adapter = any(
        name.startswith(HOST_TS_ADAPTER_PREFIX) for name in members)
    host_facade_adapter = any(
        name.startswith(HOST_FACADE_ADAPTER_PREFIX) for name in members)
    host_daemon_adapter = any(
        name.startswith(HOST_DAEMON_ADAPTER_PREFIX) for name in members)
    product_cmake = (read_archive_file(args.archive.resolve(), "repository/CMakeLists.txt")
                     .decode("utf-8") if "repository/CMakeLists.txt" in members else "")
    host_ts_product = "dtv::host_ts" in product_cmake
    host_daemon_product = "dtv::host_daemon" in product_cmake
    winscard_product = winscard_document in members or any(
        name.startswith("repository/userland/tests/windows_winscard")
        for name in members)
    winscard_adapter = any(
        name.startswith(WINSCARD_ADAPTER_PREFIX) for name in members)
    winscard_legacy = any(
        name in members for name in WINDOWS_WINSCARD_LEGACY_MARKERS)
    if windows_product:
        # A committed Windows tree must ship the complete platform, test, and
        # build script coverage; a partial tree is rejected.
        required |= WINDOWS_SOURCE_REQUIRED
    # The product layout decides, not the lock: a pre-move snapshot still owns
    # these two sources.  Otherwise the new shared runtime closure is mandatory
    # even when the whole adapter directory is absent, so a vanished dependency
    # cannot silently shrink the requirement.
    if product_runtime:
        required |= WINDOWS_PRODUCT_RUNTIME_REQUIRED
    if host_runtime_adapter or (windows_product and not product_runtime):
        required |= WINDOWS_HOST_RUNTIME_REQUIRED
    if host_ts_adapter or host_ts_product:
        required |= HOST_TS_ADAPTER_REQUIRED
    if host_facade_adapter or host_daemon_adapter or host_daemon_product:
        required |= HOST_FACADE_ADAPTER_REQUIRED
    if host_daemon_adapter or host_daemon_product:
        required |= HOST_DAEMON_ADAPTER_REQUIRED
    # A product that advertises WinSCard through its document/tests must ship
    # either the complete legacy product closure or the complete shared
    # adapter/support closure.  The adapter prefix alone also triggers both.
    if winscard_product or winscard_adapter or winscard_legacy:
        if winscard_legacy:
            required |= WINDOWS_WINSCARD_LEGACY_SOURCE_REQUIRED
        if winscard_adapter or not winscard_legacy:
            required |= WINDOWS_WINSCARD_SOURCE_REQUIRED
            required |= WINDOWS_WINSCARD_ADAPTER_REQUIRED
    if not required.issubset(members):
        fail(f"source archive is missing: {sorted(required-set(members))}")
    for name, member in (
            ("repository/distribution/licenses/gcc-COPYING3", "toolchain/gcc-COPYING3"),
            ("repository/distribution/licenses/gcc-COPYING.RUNTIME", "toolchain/gcc-COPYING.RUNTIME"),
            ("repository/distribution/licenses/musl-1.2.5-COPYRIGHT", "toolchain/musl-1.2.5-COPYRIGHT")):
        if hashlib.sha256(read_archive_file(args.archive.resolve(), name)).hexdigest() != LINUX_RUNTIME_LICENSE_SHA256[member]:
            fail(f"source archive runtime license is altered: {name}")
    for name in members:
        if name not in required and not name.startswith("repository/") \
                and not name.startswith("third_party/libusb-1.0.30/"):
            fail(f"unexpected source archive member: {name}")
    manifest = json.loads(read_archive_file(args.archive.resolve(), "source-manifest.json"))
    if manifest.get("schema") != 1 or manifest.get("libusb_version") != "1.0.30":
        fail("invalid source manifest")
    version = str(manifest.get("version", ""))
    validate_version(version)
    if args.archive.name != f"px4-userland-{version}-source.tar.gz":
        fail(f"source archive name does not match manifest version: {args.archive.name}")
    inventory = manifest.get("files")
    if not isinstance(inventory, dict):
        fail("source manifest files inventory is missing")
    expected_names = set(members) - {"source-manifest.json", "SHA256SUMS"}
    if set(inventory) != expected_names:
        fail("source manifest files inventory does not match archived payload")
    with tarfile.open(args.archive.resolve(), "r:*") as stream:
        for name in sorted(expected_names):
            record = inventory[name]
            if (not isinstance(record, dict) or set(record) != {"sha256", "size"} or
                    not isinstance(record["sha256"], str) or not re.fullmatch(r"[0-9a-f]{64}", record["sha256"]) or
                    not isinstance(record["size"], int) or isinstance(record["size"], bool) or record["size"] < 0):
                fail(f"invalid source manifest file record: {name}")
            payload = stream.extractfile(stream.getmember(name))
            if payload is None:
                fail(f"cannot read source manifest file: {name}")
            data = payload.read()
            if record["size"] != len(data) or record["sha256"] != hashlib.sha256(data).hexdigest():
                fail(f"source manifest file record mismatch: {name}")
    if manifest.get("libusb_archive_sha256") != LIBUSB_SHA256:
        fail("source manifest does not record the pinned libusb checksum")
    common_commit = manifest.get("dtv_userland_core_commit")
    if common_lock in members:
        lock_revision = read_archive_file(args.archive.resolve(), common_lock).decode("utf-8").strip()
        if not re.fullmatch(r"[0-9a-f]{40}", lock_revision) or lock_revision == "0" * 40:
            fail("product snapshot does not pin a verified dtv-userland-core commit")
        if not isinstance(common_commit, str) or common_commit != lock_revision:
            fail("source manifest dtv-userland-core commit does not match the product lock")
        if manifest.get("dtv_userland_core_license") != "MIT":
            fail("source manifest does not record the dtv-userland-core license")
        common_license = read_archive_file(
            args.archive.resolve(), common_prefix + "LICENSE").decode("utf-8")
        if "MIT License" not in common_license:
            fail("bundled dtv-userland-core LICENSE is not MIT")
        common_provenance = read_archive_file(
            args.archive.resolve(), common_prefix + "PROVENANCE.json").decode("utf-8")
        if not common_provenance.strip():
            fail("bundled dtv-userland-core PROVENANCE.json is empty")
    else:
        if common_commit is not None or manifest.get("dtv_userland_core_license") is not None:
            fail("source manifest declares dtv-userland-core without a product lock")
        if any(name.startswith(common_prefix) for name in members):
            fail("source archive bundles dtv-userland-core without a product lock")
    if "COPYING" not in "\n".join(members):
        fail("libusb COPYING is missing from source archive")
    embedded = read_archive_file(args.archive.resolve(), "third_party/libusb-1.0.30.tar.bz2")
    if hashlib.sha256(embedded).hexdigest() != LIBUSB_SHA256:
        fail("embedded libusb source archive checksum mismatch")
    try:
        with tarfile.open(fileobj=io.BytesIO(embedded), mode="r:bz2") as stream:
            names = []
            for member in stream.getmembers():
                safe_member(member.name)
                if member.name != "libusb-1.0.30" and not member.name.startswith("libusb-1.0.30/"):
                    fail(f"unexpected embedded libusb member: {member.name}")
                if member.isdir():
                    continue
                if member.issym() or member.islnk() or not member.isfile():
                    fail(f"embedded libusb member is not regular: {member.name}")
                names.append(member.name)
            if "libusb-1.0.30/COPYING" not in names:
                fail("embedded libusb source has no COPYING")
    except (OSError, tarfile.TarError) as error:
        fail(f"cannot inspect embedded libusb source: {error}")
    source_notice = read_archive_file(args.archive.resolve(), "DEPENDENCY-NOTICE.txt").decode("utf-8")
    source_fields = notice_fields(source_notice)
    if source_fields.get("dependency.libusb.version") != "1.0.30" or \
            source_fields.get("corresponding-source-archive") != "present":
        fail("source dependency notice is missing stable dependency fields")
    if common_lock in members:
        if source_fields.get("dependency.dtv-userland-core.license") != "MIT" or \
                source_fields.get("dependency.dtv-userland-core.commit") != common_commit:
            fail("source dependency notice is missing the dtv-userland-core fields")
    verify_checksums(args.archive.resolve(), members)
    return {"archive": str(args.archive.resolve()), "kind": "source", "members": sorted(members), "manifest": manifest}


def self_test() -> int:
    with tempfile.TemporaryDirectory(prefix="px4-audit-test-") as temporary:
        root = Path(temporary)
        safe = root / "safe.tar.gz"
        with tarfile.open(safe, "w:gz") as stream:
            info = tarfile.TarInfo("README.md")
            payload = b"ok\n"
            info.size = len(payload)
            stream.addfile(info, io.BytesIO(payload))
        members = archive_members(safe)
        if "README.md" not in members:
            fail("self-test could not read safe archive")
        bad = root / "bad.tar.gz"
        with tarfile.open(bad, "w:gz") as stream:
            info = tarfile.TarInfo("../escape")
            info.size = 1
            stream.addfile(info, io.BytesIO(b"x"))
        try:
            archive_members(bad)
        except AuditError:
            pass
        else:
            fail("self-test accepted traversal")

        platform = "linux-musl-x86_64"
        archive_name = f"px4-userland-0.1.0-{platform}.tar.gz"
        binary_payloads = {
            program: f"synthetic {program}\n".encode("ascii") for program in PROGRAMS
        }
        binary_payloads["ifd/px4-userland-ifd.so"] = b"synthetic ifd\n"
        base_files = {
            "LICENSE": b"license\n",
            "README.md": b"readme\n",
            "THIRD_PARTY_NOTICES.md": b"notices\n",
            "libusb/COPYING": (Path(__file__).resolve().parents[1] / "packaging/libusb/COPYING").read_bytes(),
            "DEPENDENCY-NOTICE.txt": (
                b"dependency.libusb.version=1.0.30\n"
                b"dependency.libusb.linkage=static\n"
                b"dependency.libusb.license=LGPL-2.1-or-later\n"
                b"dependency.libc=musl\n"
                b"dependency.executables=static-musl\n"
                b"runtime-license-members=toolchain/gcc-COPYING.RUNTIME,toolchain/gcc-COPYING3,toolchain/musl-1.2.5-COPYRIGHT\n"
                b"runtime-build-metadata=evidence/toolchain-static.properties,evidence/toolchain-ifd.properties\n"
                b"corresponding-source-archive=px4-userland-0.1.0-source.tar.gz\n"
            ),
            "evidence/toolchain-static.properties": (
                b"compiler=/usr/bin/g++\n"
                b"compiler_sha256=" + b"c" * 64 + b"\n"
                b"compiler_version=14.2.0\n"
                b"compiler_target=x86_64-alpine-linux-musl\n"
                b"libc=musl\n"
                b"libc_version=musl-1.2.5-r0\n"
                b"runtime=libgcc+libstdc++\n"
                b"linkage=static\n"
                b"libc_archive=/usr/lib/libc.a\n"
                b"libc_archive_sha256=" + b"a" * 64 + b"\n"
                b"libc_archive_owner=musl-dev-1.2.5-r0\n"
                b"libgcc_archive=/usr/lib/gcc/x86_64-alpine-linux-musl/14.2.0/libgcc.a\n"
                b"libgcc_archive_sha256=" + b"b" * 64 + b"\n"
                b"libgcc_archive_owner=gcc-14.2.0-r0\n"
                b"libstdcxx_archive=/usr/lib/gcc/x86_64-alpine-linux-musl/14.2.0/libstdc++.a\n"
                b"libstdcxx_archive_sha256=" + b"d" * 64 + b"\n"
                b"libstdcxx_archive_owner=libstdc++-14.2.0-r0\n"
            ),
            "evidence/toolchain-ifd.properties": (
                b"compiler=/usr/bin/g++\n"
                b"compiler_sha256=" + b"c" * 64 + b"\n"
                b"compiler_version=14.2.0\n"
                b"compiler_target=x86_64-alpine-linux-musl\n"
                b"libc=musl\n"
                b"libc_version=musl-1.2.5-r0\n"
                b"runtime=libgcc+libstdc++\n"
                b"linkage=shared-ifd-static-gcc-runtime\n"
                b"libc_input=/lib/ld-musl-x86_64.so.1\n"
                b"libc_input_sha256=" + b"a" * 64 + b"\n"
                b"libc_input_owner=musl-1.2.5-r12\n"
                b"libc_input_package_version=musl-1.2.5-r12\n"
                b"libgcc_archive=/usr/lib/gcc/x86_64-alpine-linux-musl/14.2.0/libgcc.a\n"
                b"libgcc_archive_sha256=" + b"b" * 64 + b"\n"
                b"libgcc_archive_owner=gcc-14.2.0-r0\n"
                b"libgcc_package_version=gcc-14.2.0-r0\n"
                b"libstdcxx_archive=/usr/lib/gcc/x86_64-alpine-linux-musl/14.2.0/libstdc++.a\n"
                b"libstdcxx_archive_sha256=" + b"d" * 64 + b"\n"
                b"libstdcxx_archive_owner=libstdc++-14.2.0-r0\n"
                b"libstdcxx_package_version=libstdc++-14.2.0-r0\n"
            ),
            **{
                f"toolchain/{name}": (
                    Path(__file__).resolve().parents[1] / "distribution/licenses" / name
                ).read_bytes()
                for name in ("gcc-COPYING3", "gcc-COPYING.RUNTIME", "musl-1.2.5-COPYRIGHT")
            },
            "reader.conf.d/px4-userland.conf": (
                b"DEVICENAME px4-userland:runtime=@PX4_RUNTIME_DIR@:"
                b"device=@PX4_BASE_SERIAL@:access=@PX4_ACCESS@\n"
                b"LIBPATH @PX4_IFD_LIBRARY@\n"
            ),
            "mdev/px4-userland-mdev.conf": b"mdev rule\n",
            "mdev/px4-userland-mdev.sh": b"#!/bin/sh\n",
            "mdev/px4-userland-mdev.start": b"#!/bin/sh\n",
            **binary_payloads,
        }

        def write_test_archive(path: Path, evidence: dict, mode_overrides: dict[str, int] | None = None,
                               file_overrides: dict[str, bytes | None] | None = None) -> None:
            files = dict(base_files)
            if file_overrides:
                for name, payload in file_overrides.items():
                    if payload is None:
                        files.pop(name, None)
                    else:
                        files[name] = payload
            files["evidence/binary-audit.json"] = (
                json.dumps(evidence, indent=2, sort_keys=True) + "\n"
            ).encode("utf-8")
            manifest = {
                "schema": 1,
                "version": "0.1.0",
                "platform": platform,
                "libc": "musl",
                "architecture": "x86_64",
                "embedded_libusb": {"version": "1.0.30", "linkage": "static"},
                "source_ref": "self-test",
                "programs": list(PROGRAMS),
                "production_only": True,
                "files": {
                    name: {"size": len(payload), "sha256": hashlib.sha256(payload).hexdigest()}
                    for name, payload in sorted(files.items())
                },
            }
            files["manifest.json"] = (
                json.dumps(manifest, indent=2, sort_keys=True) + "\n"
            ).encode("utf-8")
            files["SHA256SUMS"] = (
                "\n".join(
                    f"{hashlib.sha256(files[name]).hexdigest()}  {name}"
                    for name in sorted(files)
                ) + "\n"
            ).encode("ascii")
            with tarfile.open(path, "w:gz") as stream:
                for name, payload in sorted(files.items()):
                    info = tarfile.TarInfo(name)
                    info.size = len(payload)
                    default_mode = 0o755 if name in {
                        "mdev/px4-userland-mdev.sh", "mdev/px4-userland-mdev.start"
                    } else 0o644
                    info.mode = (mode_overrides or {}).get(name, default_mode)
                    stream.addfile(info, io.BytesIO(payload))

        def audit_test_archive(path: Path, section_mode: str = "stripped") -> None:
            testdata = Path(__file__).resolve().parent / "testdata"
            previous_path = os.environ.get("PATH")
            previous_sections = os.environ.get("PX4_TEST_SECTIONS")
            os.environ["PATH"] = f"{testdata}{os.pathsep}{previous_path or ''}"
            os.environ["PX4_TEST_SECTIONS"] = section_mode
            try:
                audit_binary_archive(argparse.Namespace(archive=path, platform=platform))
            finally:
                if previous_path is None:
                    os.environ.pop("PATH", None)
                else:
                    os.environ["PATH"] = previous_path
                if previous_sections is None:
                    os.environ.pop("PX4_TEST_SECTIONS", None)
                else:
                    os.environ["PX4_TEST_SECTIONS"] = previous_sections

        def evidence_for(files: dict[str, bytes]) -> dict:
            def record(name: str) -> dict:
                return {
                    "artifact": name,
                    "format": "ELF",
                    "interpreter": None,
                    "needed": [],
                    "libc": "musl",
                    "glibc_floor": None,
                    "sha256": hashlib.sha256(files[name]).hexdigest(),
                }

            return {
                "platform": platform,
                "programs": [record(program) for program in PROGRAMS],
                "extra": [record("ifd/px4-userland-ifd.so")],
            }

        valid = root / archive_name
        valid_evidence = evidence_for(base_files)
        write_test_archive(valid, valid_evidence)
        audit_test_archive(valid)
        audit_test_archive(valid, "dynsym-unwind")
        audit_test_archive(valid, "build-id-non-px4d")

        expected_mdev_modes = {
            "mdev/px4-userland-mdev.conf": 0o644,
            "mdev/px4-userland-mdev.sh": 0o755,
            "mdev/px4-userland-mdev.start": 0o755,
        }
        for name, expected_mode in expected_mdev_modes.items():
            invalid_mode = 0o600 if name.endswith(".conf") else 0o744
            invalid_archive = root / f"invalid-{name.rsplit('/', 1)[-1]}.tar.gz"
            write_test_archive(invalid_archive, valid_evidence, {name: invalid_mode})
            try:
                audit_test_archive(invalid_archive)
            except AuditError:
                pass
            else:
                fail(f"invalid mdev mode archive self-test did not fail: {name}")

        invalid_license_mode_archive = root / "invalid-libusb-copying-mode.tar.gz"
        write_test_archive(invalid_license_mode_archive, valid_evidence,
                           {"libusb/COPYING": 0o600})
        try:
            audit_test_archive(invalid_license_mode_archive)
        except AuditError:
            pass
        else:
            fail("invalid libusb/COPYING mode archive self-test did not fail")

        for section_mode in ("debug", "zdebug", "symtab", "build-id-px4d"):
            try:
                audit_test_archive(valid, section_mode)
            except AuditError:
                pass
            else:
                fail(f"self-test accepted {section_mode} sections")

        invalid_readers = {
            "missing-placeholder": base_files["reader.conf.d/px4-userland.conf"].replace(
                b":access=@PX4_ACCESS@", b":access=user"
            ),
            "extra-placeholder": base_files["reader.conf.d/px4-userland.conf"] + b"# @PX4_EXTRA@\n",
        }
        for description, reader in invalid_readers.items():
            try:
                validate_reader_template(reader)
            except AuditError:
                pass
            else:
                fail(f"self-test accepted reader template with {description}")

        def expect_archive_rejected(path: Path, description: str) -> None:
            try:
                audit_test_archive(path)
            except AuditError:
                return
            fail(f"self-test accepted {description}")

        def expect_archive_error(path: Path, expected: str, description: str) -> None:
            try:
                audit_test_archive(path)
            except AuditError as error:
                if expected not in str(error):
                    fail(f"self-test rejected {description} for the wrong reason: {error}")
                return
            fail(f"self-test accepted {description}")

        tampered_evidence = dict(valid_evidence)
        tampered_evidence["platform"] = "darwin-arm64"
        tampered_evidence_archive = root / "tampered-evidence" / archive_name
        tampered_evidence_archive.parent.mkdir()
        write_test_archive(tampered_evidence_archive, tampered_evidence)
        expect_archive_rejected(tampered_evidence_archive, "tampered evidence")

        tampered_hash = json.loads(json.dumps(valid_evidence))
        tampered_hash["programs"][0]["sha256"] = "0" * 64
        tampered_hash_archive = root / "tampered-hash" / archive_name
        tampered_hash_archive.parent.mkdir()
        write_test_archive(tampered_hash_archive, tampered_hash)
        expect_archive_rejected(tampered_hash_archive, "tampered evidence hash")

        duplicate_program = json.loads(json.dumps(valid_evidence))
        duplicate_program["programs"][1]["artifact"] = "px4d"
        duplicate_program_archive = root / "duplicate-program" / archive_name
        duplicate_program_archive.parent.mkdir()
        write_test_archive(duplicate_program_archive, duplicate_program)
        expect_archive_rejected(duplicate_program_archive, "duplicate program artifact")

        missing_license_archive = root / "missing-license" / archive_name
        missing_license_archive.parent.mkdir()
        write_test_archive(missing_license_archive, valid_evidence,
                           file_overrides={"libusb/COPYING": None})
        expect_archive_rejected(missing_license_archive, "archive missing libusb/COPYING")

        missing_runtime_license_archive = root / "missing-runtime-license" / archive_name
        missing_runtime_license_archive.parent.mkdir()
        write_test_archive(missing_runtime_license_archive, valid_evidence,
                           file_overrides={"toolchain/gcc-COPYING.RUNTIME": None})
        expect_archive_error(missing_runtime_license_archive, "archive member allowlist mismatch",
                             "missing Linux runtime license")

        tampered_license_archive = root / "tampered-license" / archive_name
        tampered_license_archive.parent.mkdir()
        write_test_archive(tampered_license_archive, valid_evidence,
                           file_overrides={"libusb/COPYING": base_files["libusb/COPYING"] + b"tampered\n"})
        expect_archive_rejected(tampered_license_archive, "tampered libusb/COPYING")
        tampered_runtime_license = root / "tampered-runtime-license" / archive_name
        tampered_runtime_license.parent.mkdir()
        write_test_archive(tampered_runtime_license, valid_evidence, file_overrides={
            "toolchain/gcc-COPYING.RUNTIME": base_files["toolchain/gcc-COPYING.RUNTIME"] + b"tampered\n"
        })
        expect_archive_error(tampered_runtime_license, "Linux runtime license is missing or altered",
                             "tampered Linux runtime license")

        malformed_metadata = root / "malformed-metadata" / archive_name
        malformed_metadata.parent.mkdir()
        write_test_archive(malformed_metadata, valid_evidence, file_overrides={
            "evidence/toolchain-static.properties": base_files["evidence/toolchain-static.properties"].replace(
                b"libgcc_archive_sha256=" + b"b" * 64,
                b"libgcc_archive_sha256=synthetic-invalid-hash"
            )
        })
        expect_archive_error(malformed_metadata,
                             "Linux runtime input hash is invalid: evidence/toolchain-static.properties:libgcc_archive_sha256",
                             "malformed Linux runtime metadata")
    print("artifact audit self-test: traversal, evidence, missing license, and license tampering rejected")
    return 0


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--self-test", action="store_true")
    parser.add_argument("--repo-root", type=Path, default=Path(__file__).resolve().parents[1])
    parser.add_argument("--platform", choices=sorted(PLATFORMS))
    parser.add_argument("--build-dir", type=Path)
    parser.add_argument("--binary-suffix", default="")
    parser.add_argument("--archive", type=Path)
    parser.add_argument("--source-archive", action="store_true")
    parser.add_argument("--ifd-library", type=Path)
    parser.add_argument("--ifd-bundle", type=Path)
    parser.add_argument("--link-map-dir", type=Path)
    parser.add_argument("--ndk-root", type=Path)
    parser.add_argument("--inventory-dir", type=Path)
    parser.add_argument("--evidence-output", type=Path)
    args = parser.parse_args()
    if args.self_test:
        return self_test()
    if args.source_archive and not args.archive:
        parser.error("--source-archive requires --archive")
    if args.source_archive:
        result = audit_source_archive(args)
    elif args.archive:
        result = audit_binary_archive(args)
        if args.build_dir:
            result = {"archive": result, "binaries": audit_binaries(args)}
    elif args.build_dir and args.platform:
        result = {"binaries": audit_binaries(args)}
    else:
        parser.error("provide --archive, or --platform and --build-dir")
    if args.evidence_output:
        args.evidence_output.parent.mkdir(parents=True, exist_ok=True)
        args.evidence_output.write_text(json.dumps(result, indent=2, sort_keys=True) + "\n", encoding="utf-8")
    print(f"audited {args.archive}" if args.archive else "audited binaries")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (AuditError, OSError, ValueError, KeyError, json.JSONDecodeError) as error:
        print(f"artifact audit: {error}", file=sys.stderr)
        raise SystemExit(1)
