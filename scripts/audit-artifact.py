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
import subprocess
import sys
import tarfile
import tempfile


PLATFORMS = {
    "linux-glibc-x86_64": "linux-glibc",
    "linux-glibc-aarch64": "linux-glibc",
    "linux-musl-x86_64": "linux-musl",
    "linux-musl-aarch64": "linux-musl",
    "darwin-arm64": "darwin",
    "android-aarch64": "android",
    "android-armv7a": "android",
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
COMMON = {
    "LICENSE",
    "README.md",
    "THIRD_PARTY_NOTICES.md",
    "DEPENDENCY-NOTICE.txt",
    "manifest.json",
    "SHA256SUMS",
    "evidence/binary-audit.json",
}
PROGRAMS = ("px4d", "px4-ts", "px4ctl")
ANDROID_INVENTORY_MEMBERS = tuple(
    f"evidence/inventory/{program}-static-archives.tsv" for program in PROGRAMS
)
FORBIDDEN = re.compile(
    r"(^|/)(?:firmware|windows|win32|vendor|drivers?|dkms|kernel|apk|addon|add-on)(?:/|$)"
    r"|(?:\.apk$|\.ko$|\.sys$|\.inf$|\.dll$|\.exe$|\.bin$)"
    r"|(?:px4-ts-probe|px4-.*-probe)",
    re.IGNORECASE,
)


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


def safe_member(name: str) -> PurePosixPath:
    if "\\" in name or name.startswith("/"):
        fail(f"unsafe archive member path: {name!r}")
    path = PurePosixPath(name)
    if not name or path == PurePosixPath(".") or ".." in path.parts:
        fail(f"unsafe archive member path: {name!r}")
    return path


def archive_members(archive: Path) -> dict[str, tarfile.TarInfo]:
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
                if FORBIDDEN.search(member.name):
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
        fields = {"artifact", "format", "install_id", "dependencies", "sha256"}
        if set(record) != fields or record.get("format") != "Mach-O":
            fail("invalid macOS binary evidence schema")
        if not isinstance(record.get("install_id"), (str, type(None))):
            fail("invalid macOS binary evidence install_id")
        validate_string_list(record.get("dependencies"), "dependencies")
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
        "darwin-arm64": "ifd/px4-userland-ifd.bundle/Contents/MacOS/libpx4-userland-ifd.dylib",
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
    embedded = manifest.get("embedded_libusb")
    if platform.startswith("linux-") or platform.startswith("android-"):
        if embedded != {"version": "1.0.30", "linkage": "static"}:
            fail("manifest embedded libusb metadata mismatch")
    elif embedded is not None:
        fail("unexpected embedded libusb metadata")
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


def readelf_path() -> str:
    for candidate in ("readelf", "llvm-readelf"):
        found = next((part for part in os.get_exec_path() if Path(part, candidate).is_file()), None)
        if found:
            return str(Path(found, candidate))
    fail("readelf or llvm-readelf is required")


def audit_linux(path: Path, logical_name: str, *, platform: str, shared: bool, require_libusb: bool,
                reject_pcsc: bool = False) -> dict:
    target = LINUX_TARGETS[platform]
    readelf = readelf_path()
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


def audit_darwin(path: Path, logical_name: str, *, require_libusb: bool,
                 reject_pcsc: bool = False) -> dict:
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
    if require_libusb and not any("libusb-1.0" in dependency for dependency in logical_dependencies):
        fail(f"dynamic libusb is missing from {path}")
    if not require_libusb and any("libusb-1.0" in dependency for dependency in logical_dependencies):
        fail(f"unexpected direct dynamic libusb dependency: {path}")
    if reject_pcsc and any("pcsc" in dependency.lower() for dependency in logical_dependencies):
        fail(f"unexpected direct PC/SC client dependency: {path}")
    if any(dependency.startswith("@loader_path/") or dependency.startswith("@rpath/")
           for dependency in dependencies):
        fail(f"bundled/rpath dependency is forbidden: {path}")
    stable_install_id = None if own_id is None else (own_id if own_id.startswith("@") else PurePosixPath(own_id).name)
    return {"artifact": logical_name, "format": "Mach-O", "install_id": stable_install_id,
            "dependencies": logical_dependencies}


def audit_android(path: Path, logical_name: str, expected_interpreter: str, root: Path) -> dict:
    verifier = root / "scripts" / "verify-android-elf.sh"
    if not verifier.is_file():
        fail(f"missing existing Android ELF verifier: {verifier}")
    run([str(verifier), str(path), expected_interpreter])
    readelf = readelf_path()
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
        path = build / f"{program}{('-' + args.binary_suffix) if args.binary_suffix else ''}"
        if not path.is_file() or path.is_symlink():
            fail(f"missing production program: {path}")
        if args.platform.startswith("android"):
            expected = "/system/bin/linker64" if args.platform.endswith("aarch64") else "/system/bin/linker"
            evidence = audit_android(path, program, expected, args.repo_root.resolve())
        elif args.platform.startswith("linux-"):
            evidence = audit_linux(path, program, platform=args.platform, shared=False,
                                   require_libusb=program == "px4d")
        else:
            evidence = audit_darwin(path, program, require_libusb=program == "px4d")
        evidence["sha256"] = sha256(path)
        result["programs"].append(evidence)

    if args.platform.startswith("linux-"):
        if not args.ifd_library:
            fail("--ifd-library is required for Linux binary audit")
        path = args.ifd_library.resolve()
        if not path.is_file() or path.is_symlink():
            fail(f"missing Linux IFD library: {path}")
        evidence = audit_linux(path, "ifd/px4-userland-ifd.so", platform=args.platform, shared=True,
                               require_libusb=False, reject_pcsc=True)
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
            require_libusb=False, reject_pcsc=True)
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


def audit_binary_archive(args: argparse.Namespace) -> dict:
    validate_platform(args.platform)
    members = archive_members(args.archive.resolve())
    expected = set(COMMON) | set(PROGRAMS)
    if args.platform.startswith("linux-"):
        expected |= {"ifd/px4-userland-ifd.so", "reader.conf.d/px4-userland.conf"}
    elif args.platform == "darwin-arm64":
        expected |= {
            "ifd/px4-userland-ifd.bundle/Contents/Info.plist",
            "ifd/px4-userland-ifd.bundle/Contents/MacOS/libpx4-userland-ifd.dylib",
            "reader.conf.d/px4-userland.conf",
        }
    else:
        expected |= {"libusb/COPYING", "ndk/NOTICE", "ndk/NOTICE.toolchain", "ndk/source.properties"}
        for program in PROGRAMS:
            expected.add(f"evidence/inventory/{program}-static-archives.tsv")
    if set(members) != expected:
        fail(f"archive member allowlist mismatch; unexpected={sorted(set(members)-expected)}, missing={sorted(expected-set(members))}")
    manifest = verify_manifest(args.archive.resolve(), members, args.platform)
    expected_name = f"px4-userland-{manifest['version']}-{args.platform}.tar.gz"
    if args.archive.name != expected_name:
        fail(f"archive name does not match platform/version: {args.archive.name}")
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
    elif args.platform.startswith("linux-"):
        fields = notice_fields(notice)
        if fields.get("dependency.libusb.linkage") != "static" or fields.get("dependency.libusb.version") != "1.0.30":
            fail("Linux dependency notice does not describe static pinned libusb")
        if fields.get("dependency.libc") != ("glibc" if args.platform.startswith("linux-glibc") else "musl"):
            fail("Linux dependency notice libc mismatch")
        if fields.get("corresponding-source-archive") != f"px4-userland-{manifest['version']}-source.tar.gz":
            fail("Linux dependency notice must point to the separately published source archive")
    else:
        fields = notice_fields(notice)
        if fields.get("dependency.libusb.linkage") != "dynamic" or fields.get("dependency.libusb.provider") != "host":
            fail("native dependency notice does not describe host-provided dynamic libusb")
    verify_checksums(args.archive.resolve(), members)
    return {"archive": str(args.archive.resolve()), "platform": args.platform, "members": sorted(members), "manifest": manifest}


def audit_source_archive(args: argparse.Namespace) -> dict:
    members = archive_members(args.archive.resolve())
    source_forbidden = re.compile(
        r"(^|/)(?:\.git|build(?:-[^/.]+)?|out|dist|firmware|windows|win32|vendor|drivers?|dkms|kernel|apk|addon|add-on)(?:/|$)"
        r"|(?:\.o$|\.a$|\.so(?:\.|$)|\.dylib$|\.apk$|\.ko$|\.sys$|\.inf$|\.dll$|\.exe$|\.bin$)"
        r"|(?:px4-ts-probe|px4-.*-probe)", re.IGNORECASE
    )
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
        "repository/scripts/test-static-relink.sh",
        "repository/packaging/REBUILD.md.in",
    }
    if not required.issubset(members):
        fail(f"source archive is missing: {sorted(required-set(members))}")
    for name in members:
        if name not in required and not name.startswith("repository/") and not name.startswith("third_party/libusb-1.0.30/"):
            fail(f"unexpected source archive member: {name}")
        if name.startswith("repository/") and source_forbidden.search(name):
            fail(f"forbidden source archive member: {name}")
        if name.startswith("third_party/libusb-1.0.30/") and source_forbidden.search(name):
            fail(f"forbidden libusb source member: {name}")
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
            "DEPENDENCY-NOTICE.txt": (
                b"dependency.libusb.version=1.0.30\n"
                b"dependency.libusb.linkage=static\n"
                b"dependency.libc=musl\n"
                b"dependency.executables=static-musl\n"
                b"corresponding-source-archive=px4-userland-0.1.0-source.tar.gz\n"
            ),
            "reader.conf.d/px4-userland.conf": b"reader\n",
            **binary_payloads,
        }

        def write_test_archive(path: Path, evidence: dict) -> None:
            files = dict(base_files)
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
                    stream.addfile(info, io.BytesIO(payload))

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
        audit_binary_archive(argparse.Namespace(archive=valid, platform=platform))

        def expect_archive_rejected(path: Path, description: str) -> None:
            try:
                audit_binary_archive(argparse.Namespace(archive=path, platform=platform))
            except AuditError:
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
    print("artifact audit self-test: safe regular member accepted, traversal and archive evidence tampering rejected")
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
