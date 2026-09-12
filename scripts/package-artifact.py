#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-only
"""Package one already-built px4-userland platform."""

from __future__ import annotations

import argparse
import gzip
import hashlib
import importlib.util
import json
import os
from pathlib import Path, PurePosixPath
import shutil
import sys
import tarfile
import tempfile

_audit_spec = importlib.util.spec_from_file_location(
    "px4_audit_artifact", Path(__file__).with_name("audit-artifact.py")
)
if _audit_spec is None or _audit_spec.loader is None:
    raise ImportError("cannot load audit-artifact.py")
_audit = importlib.util.module_from_spec(_audit_spec)
_audit_spec.loader.exec_module(_audit)
AuditError = _audit.AuditError
LIBUSB_SHA256 = _audit.LIBUSB_SHA256
PLATFORMS = _audit.PLATFORMS
PROGRAMS = _audit.PROGRAMS
TERMUX_LAUNCHER = _audit.TERMUX_LAUNCHER
VERSION_RE = _audit.VERSION_RE
audit_binaries = _audit.audit_binaries
audit_binary_archive = _audit.audit_binary_archive
fail = _audit.fail
run = _audit.run


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def copy_regular(source: Path, destination: Path) -> None:
    if source.is_symlink() or not source.is_file():
        fail(f"input must be an ordinary file: {source}")
    destination.parent.mkdir(parents=True, exist_ok=True)
    shutil.copyfile(source, destination)
    shutil.copymode(source, destination)


def copy_tree(source: Path, destination: Path) -> None:
    if source.is_symlink() or not source.is_dir():
        fail(f"input must be an ordinary directory: {source}")
    for path in source.rglob("*"):
        relative = path.relative_to(source)
        if path.is_symlink():
            fail(f"symlink in package input: {path}")
        target = destination / relative
        if path.is_dir():
            target.mkdir(parents=True, exist_ok=True)
        elif path.is_file():
            copy_regular(path, target)
        else:
            fail(f"unsupported package input: {path}")


def write_text(path: Path, text: str) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open("w", encoding="utf-8", newline="\n") as stream:
        stream.write(text)
    path.chmod(0o644)


def validate_libusb_member(member: tarfile.TarInfo) -> bool:
    """Validate a member and return False for the source root directory."""
    if "\\" in member.name or member.name.startswith("/"):
        fail(f"unsafe libusb source member: {member.name}")
    member_path = PurePosixPath(member.name)
    if ".." in member_path.parts:
        fail(f"unsafe libusb source member: {member.name}")
    if member_path != PurePosixPath("libusb-1.0.30") and not member.name.startswith("libusb-1.0.30/"):
        fail(f"unexpected libusb source member: {member.name}")
    if member.isdir():
        return False
    if member.issym() or member.islnk() or not member.isfile():
        fail(f"libusb source archive contains a non-regular member: {member.name}")
    return True


def source_version(root: Path) -> str:
    version_file = root / "VERSION"
    if not version_file.is_file() or version_file.is_symlink():
        fail(f"missing VERSION file: {version_file}")
    data = version_file.read_bytes()
    if not data.endswith(b"\n") or data.count(b"\n") != 1:
        fail("VERSION must contain exactly N.N.N followed by one newline")
    try:
        text = data[:-1].decode("ascii")
    except UnicodeDecodeError:
        fail("VERSION must contain ASCII N.N.N text")
    if not VERSION_RE.fullmatch(text):
        fail("VERSION must contain exactly N.N.N followed by one newline")
    return text


def deterministic_tar(source: Path, destination: Path) -> None:
    with destination.open("wb") as raw:
        with gzip.GzipFile(fileobj=raw, mode="wb", filename="", mtime=0) as compressed:
            with tarfile.open(fileobj=compressed, mode="w|", format=tarfile.PAX_FORMAT) as stream:
                for path in sorted(source.rglob("*")):
                    if path.is_symlink():
                        fail(f"symlink reached archive writer: {path}")
                    if not path.is_file():
                        continue
                    relative = path.relative_to(source).as_posix()
                    info = tarfile.TarInfo(relative)
                    info.size = path.stat().st_size
                    info.mode = path.stat().st_mode & 0o7777
                    info.mtime = 0
                    info.uid = info.gid = 0
                    info.uname = info.gname = ""
                    with path.open("rb") as handle:
                        stream.addfile(info, handle)


def write_checksums(stage: Path) -> None:
    rows = []
    for path in sorted(stage.rglob("*")):
        if path.is_file() and path.name != "SHA256SUMS":
            rows.append(f"{sha256(path)}  {path.relative_to(stage).as_posix()}")
    write_text(stage / "SHA256SUMS", "\n".join(rows) + "\n")


def darwin_strip_tool() -> str:
    tool = shutil.which("strip")
    if tool is None:
        fail("Apple strip is required for macOS packaging")
    return tool


def strip_darwin_stage(stage: Path) -> None:
    binaries = [stage / program for program in PROGRAMS]
    binaries.append(stage / "ifd" / "px4-userland-ifd.bundle" / "Contents" / "MacOS" /
                    "libpx4-userland-ifd.dylib")
    tool = darwin_strip_tool()
    for binary in binaries:
        if not binary.is_file() or binary.is_symlink():
            fail(f"missing macOS staged binary: {binary}")
        # Apple strip can retain one radr:// UUID metadata entry after -S -x;
        # the native audit below accepts only that exact N_OPT form.
        run([tool, "-S", "-x", str(binary)])


def verify_darwin_stage_runtime(stage: Path) -> None:
    for program in PROGRAMS:
        run([str(stage / program), "--help"])


def verify_pinned_libusb(archive: Path, stage: Path) -> None:
    if archive.name != "libusb-1.0.30.tar.bz2" or sha256(archive) != LIBUSB_SHA256:
        fail("Android requires the exact verified libusb-1.0.30 source archive")
    try:
        with tarfile.open(archive, "r:bz2") as stream:
            names = []
            for member in stream.getmembers():
                if not validate_libusb_member(member):
                    continue
                names.append(member.name)
            if "libusb-1.0.30/COPYING" not in names:
                fail("verified libusb archive has no COPYING")
            copying = stream.extractfile(stream.getmember("libusb-1.0.30/COPYING"))
            if copying is None:
                fail("cannot extract libusb COPYING")
            (stage / "libusb").mkdir()
            write_text(stage / "libusb" / "COPYING", copying.read().decode("utf-8"))
    except (OSError, tarfile.TarError, UnicodeDecodeError) as error:
        fail(f"cannot inspect verified libusb archive: {error}")


def render_dependency_notice(repo_root: Path, version: str, platform: str,
                             ndk_revision: str | None = None) -> str:
    template = repo_root / "packaging" / "DEPENDENCY-NOTICE.txt.in"
    if not template.is_file():
        fail(f"missing dependency notice template: {template}")
    if platform.startswith("linux-"):
        libc = "glibc" if platform.startswith("linux-glibc-") else "musl"
        dependency_text = (
            "dependency.libusb.version=1.0.30\n"
            "dependency.libusb.linkage=static\n"
            "dependency.libusb.license=LGPL-2.1-or-later\n"
            f"dependency.libc={libc}\n"
            "dependency.executables=static-musl\n"
            f"corresponding-source-archive=px4-userland-{version}-source.tar.gz\n\n"
            "Linux production executables are fully static musl ELFs. The PC/SC IFD Handler is a separate "
            f"{libc} shared object loaded by the host pcscd. The libusb source and relink recipe are not in this "
            f"binary archive; obtain px4-userland-{version}-source.tar.gz from the same candidate handoff.\n"
        )
    elif platform.startswith("android"):
        if not ndk_revision:
            fail("Android dependency notice requires an exact NDK revision")
        dependency_text = (
            "dependency.libusb.version=1.0.30\n"
            "dependency.libusb.linkage=static\n"
            "dependency.libusb.license=LGPL-2.1-or-later\n"
            f"dependency.ndk.revision={ndk_revision}\n"
            "dependency.ndk.materials=ndk/source.properties,ndk/NOTICE,ndk/NOTICE.toolchain\n"
            "corresponding-source-archive=present\n\n"
            "This Android archive statically includes libusb 1.0.30 and portions of the NDK C++ runtime. "
            "See libusb/COPYING, the NDK notice materials, THIRD_PARTY_NOTICES.md, evidence/, and the "
            "corresponding-source archive.\n"
        )
    else:
        dependency_text = (
            "dependency.libusb.linkage=dynamic\n"
            "dependency.libusb.provider=host\n"
            "This native archive uses host-provided dynamic libusb and system PC/SC dependencies; "
            "no dependency is bundled. See THIRD_PARTY_NOTICES.md.\n"
        )
    notice = template.read_text(encoding="utf-8")
    notice = notice.replace("@VERSION@", version).replace("@PLATFORM@", platform)
    return notice.replace("@DEPENDENCY_TEXT@", dependency_text)


def self_test_android_notice_and_archive(repo_root: Path) -> None:
    """Exercise the generated Android notice and the final archive audit together."""
    version = source_version(repo_root)
    platform = "android-aarch64"
    revision = "27.0.12077973"
    notice = render_dependency_notice(repo_root, version, platform, revision)
    required = {
        "dependency.libusb.version": "1.0.30",
        "dependency.libusb.linkage": "static",
        "dependency.libusb.license": "LGPL-2.1-or-later",
        "dependency.ndk.revision": revision,
        "dependency.ndk.materials": "ndk/source.properties,ndk/NOTICE,ndk/NOTICE.toolchain",
        "corresponding-source-archive": "present",
    }
    fields = _audit.notice_fields(notice)
    if any(fields.get(key) != value for key, value in required.items()):
        fail("Android dependency notice self-test fields do not match the audit contract")

    with tempfile.TemporaryDirectory(prefix="px4-android-audit-test-") as temporary:
        stage = Path(temporary) / "stage"
        stage.mkdir()
        for name in ("LICENSE", "README.md", "THIRD_PARTY_NOTICES.md"):
            copy_regular(repo_root / name, stage / name)
        copy_regular(repo_root / "packaging" / "termux" / TERMUX_LAUNCHER,
                     stage / TERMUX_LAUNCHER)
        (stage / TERMUX_LAUNCHER).chmod(0o755)
        for program in PROGRAMS:
            write_text(stage / program, "synthetic Android ELF\n")
            (stage / program).chmod(0o755)
        write_text(stage / "DEPENDENCY-NOTICE.txt", notice)
        write_text(stage / "libusb" / "COPYING", "LGPL-2.1-or-later\n")
        write_text(stage / "ndk" / "NOTICE", "NDK notice\n")
        write_text(stage / "ndk" / "NOTICE.toolchain", "toolchain notice\n")
        write_text(stage / "ndk" / "source.properties", f"Pkg.Revision = {revision}\n")
        program_evidence = []
        for program in PROGRAMS:
            program_evidence.append({
                "artifact": program,
                "format": "Android ELF",
                "interpreter": "/system/bin/linker64",
                "needed": [],
                "sha256": sha256(stage / program),
            })
        evidence = {
            "platform": platform,
            "programs": program_evidence,
            "extra": [],
            "android": {"ndk_revision": revision, "inventory_members": [
                f"evidence/inventory/{program}-static-archives.tsv" for program in PROGRAMS
            ]},
        }
        write_text(stage / "evidence" / "binary-audit.json",
                   json.dumps(evidence, indent=2, sort_keys=True) + "\n")
        for program in PROGRAMS:
            inventory = "category\tarchive\tmember\nndk-runtime\tlibc++_static.a\tmember\n"
            if program == "px4d":
                inventory += "libusb\tlibusb-1.0.a\tmember\n"
            write_text(stage / "evidence" / "inventory" / f"{program}-static-archives.tsv", inventory)
        manifest = {
            "schema": 1,
            "version": version,
            "platform": platform,
            "libc": "android",
            "architecture": "aarch64",
            "embedded_libusb": {"version": "1.0.30", "linkage": "static"},
            "source_ref": "self-test",
            "programs": list(PROGRAMS),
            "production_only": True,
            "files": {},
        }
        for path in sorted(stage.rglob("*")):
            if path.is_file():
                manifest["files"][path.relative_to(stage).as_posix()] = {
                    "size": path.stat().st_size,
                    "sha256": sha256(path),
                }
        write_text(stage / "manifest.json", json.dumps(manifest, indent=2, sort_keys=True) + "\n")
        write_checksums(stage)
        archive = Path(temporary) / f"px4-userland-{version}-{platform}.tar.gz"
        deterministic_tar(stage, archive)
        testdata = repo_root / "scripts" / "testdata"
        previous_path = os.environ.get("PATH")
        previous_sections = os.environ.get("PX4_TEST_SECTIONS")
        os.environ["PATH"] = f"{testdata}{os.pathsep}{previous_path or ''}"
        try:
            os.environ["PX4_TEST_SECTIONS"] = "stripped"
            audit_binary_archive(argparse.Namespace(archive=archive, platform=platform))
            os.environ["PX4_TEST_SECTIONS"] = "dynsym-unwind"
            audit_binary_archive(argparse.Namespace(archive=archive, platform=platform))
            for mode in ("debug", "zdebug", "symtab", "build-id-px4d"):
                os.environ["PX4_TEST_SECTIONS"] = mode
                try:
                    audit_binary_archive(argparse.Namespace(archive=archive, platform=platform))
                except AuditError:
                    pass
                else:
                    fail(f"Android archive self-test accepted {mode} sections")
            os.environ["PX4_TEST_SECTIONS"] = "build-id-non-px4d"
            audit_binary_archive(argparse.Namespace(archive=archive, platform=platform))
        finally:
            if previous_path is None:
                os.environ.pop("PATH", None)
            else:
                os.environ["PATH"] = previous_path
            if previous_sections is None:
                os.environ.pop("PX4_TEST_SECTIONS", None)
            else:
                os.environ["PX4_TEST_SECTIONS"] = previous_sections
        with tarfile.open(archive, "r:gz") as stream:
            for member in stream.getmembers():
                if member.name.endswith(".map") or member.name.startswith("evidence/maps/"):
                    fail("Android archive self-test found a raw linker map")
                handle = stream.extractfile(member)
                if handle is None:
                    fail(f"Android archive self-test cannot read {member.name}")
                payload = handle.read()
                for marker in (str(repo_root.resolve()).encode(),
                               str(Path(temporary).resolve()).encode()):
                    if marker in payload:
                        fail(f"Android archive self-test found an input path: {member.name}")


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--self-test", action="store_true")
    parser.add_argument("--platform", choices=sorted(PLATFORMS))
    parser.add_argument("--version")
    parser.add_argument("--build-dir", type=Path)
    parser.add_argument("--static-build-dir", type=Path,
                        help="musl static production executables for Linux archives")
    parser.add_argument("--binary-suffix", default="", help="explicit suffix used by build outputs, e.g. arm64-v8a")
    parser.add_argument("--output-dir", type=Path)
    parser.add_argument("--reader-template", type=Path)
    parser.add_argument("--ifd-library", type=Path)
    parser.add_argument("--ifd-bundle", type=Path)
    parser.add_argument("--libusb-source-archive", type=Path)
    parser.add_argument("--ndk-root", type=Path)
    parser.add_argument("--link-map-dir", type=Path)
    parser.add_argument("--repo-root", type=Path, default=Path(__file__).resolve().parents[1])
    parser.add_argument("--source-ref", default=os.environ.get("GITHUB_SHA", "working-tree"))
    args = parser.parse_args()
    if args.self_test:
        if args.libusb_source_archive:
            with tempfile.TemporaryDirectory(prefix="px4-real-libusb-test-") as temporary:
                stage = Path(temporary) / "stage"
                stage.mkdir()
                verify_pinned_libusb(args.libusb_source_archive.resolve(), stage)
                if not (stage / "libusb" / "COPYING").is_file():
                    fail("real libusb material extraction did not produce COPYING")
            print("real pinned libusb material extraction: PASS")
            return 0
        with tempfile.TemporaryDirectory(prefix="px4-libusb-test-") as temporary:
            version_root = Path(temporary) / "version"
            version_root.mkdir()
            for invalid in (b" 0.1.0\n", b"0.1.0\n\n", b"0.1.0"):
                (version_root / "VERSION").write_bytes(invalid)
                try:
                    source_version(version_root)
                except Exception as error:  # Every malformed form must fail closed.
                    if "VERSION" not in str(error):
                        raise
                else:
                    fail("malformed VERSION self-test was accepted")
            archive = Path(temporary) / "libusb-1.0.30.tar.bz2"
            with tarfile.open(archive, "w:bz2") as stream:
                directory = tarfile.TarInfo("libusb-1.0.30")
                directory.type = tarfile.DIRTYPE
                stream.addfile(directory)
                payload = b"license\n"
                file_info = tarfile.TarInfo("libusb-1.0.30/COPYING")
                file_info.size = len(payload)
                stream.addfile(file_info, __import__("io").BytesIO(payload))
            with tarfile.open(archive, "r:bz2") as stream:
                members = stream.getmembers()
                if validate_libusb_member(members[0]) or not validate_libusb_member(members[1]):
                    fail("libusb directory-entry self-test failed")
            try:
                verify_pinned_libusb(archive, Path(temporary) / "stage")
            except Exception as error:  # SHA rejection is the production boundary under test.
                if "exact verified" not in str(error):
                    raise
            else:
                fail("synthetic libusb archive bypassed SHA enforcement")
        self_test_android_notice_and_archive(args.repo_root.resolve())
        print("libusb packaging self-test: directory accepted, synthetic SHA mismatch rejected")
        print("Android dependency notice and archive audit self-test: PASS")
        return 0
    for required in (args.platform, args.version, args.output_dir):
        if required is None:
            parser.error("--platform, --version, and --output-dir are required")
    if args.platform.startswith("linux-"):
        if args.static_build_dir is None:
            parser.error("Linux requires --static-build-dir")
    elif args.build_dir is None:
        parser.error("non-Linux platforms require --build-dir")
    if not VERSION_RE.fullmatch(args.version):
        fail(f"version must be strict N.N.N, got {args.version!r}")
    repository_version = source_version(args.repo_root.resolve())
    if args.version != repository_version:
        fail(f"--version {args.version} does not match VERSION {repository_version}")
    if not all(character.isalnum() or character in "._-" for character in args.binary_suffix):
        fail("--binary-suffix contains unsafe path characters")
    platform_kind = PLATFORMS[args.platform]
    args.output_dir.mkdir(parents=True, exist_ok=True)
    output = args.output_dir.resolve() / f"px4-userland-{args.version}-{args.platform}.tar.gz"
    if output.exists() or output.is_symlink():
        fail(f"refusing to overwrite existing archive: {output}")
    with tempfile.TemporaryDirectory(prefix="px4-package-") as temporary:
        stage = Path(temporary) / "stage"
        stage.mkdir()
        for name in ("LICENSE", "README.md", "THIRD_PARTY_NOTICES.md"):
            copy_regular(args.repo_root / name, stage / name)
        binary_root = (args.static_build_dir if args.platform.startswith("linux-") else args.build_dir).resolve()
        for program in ("px4d", "px4-ts", "px4ctl"):
            source = binary_root / f"{program}{('-' + args.binary_suffix) if args.binary_suffix else ''}"
            copy_regular(source, stage / program)
            (stage / program).chmod(0o755)
        if platform_kind.startswith("linux") or platform_kind == "darwin":
            if not args.reader_template:
                fail("--reader-template is required for native platforms")
            copy_regular(args.reader_template.resolve(), stage / "reader.conf.d" / "px4-userland.conf")
        if args.platform.startswith("linux-"):
            for name in ("px4-userland-mdev.conf", "px4-userland-mdev.sh", "px4-userland-mdev.start"):
                copy_regular(args.repo_root / "packaging/mdev" / name, stage / "mdev" / name)
        revision = None
        if args.platform.startswith("linux-"):
            if not args.ifd_library:
                fail("--ifd-library is required for Linux")
            copy_regular(args.ifd_library.resolve(), stage / "ifd" / "px4-userland-ifd.so")
        elif args.platform == "darwin-arm64":
            if not args.ifd_bundle:
                fail("--ifd-bundle is required for macOS")
            copy_tree(args.ifd_bundle.resolve(), stage / "ifd" / "px4-userland-ifd.bundle")
        else:
            if not args.libusb_source_archive or not args.ndk_root or not args.link_map_dir:
                fail("Android requires --libusb-source-archive, --ndk-root, and --link-map-dir")
            copy_regular(args.repo_root / "packaging" / "termux" / TERMUX_LAUNCHER,
                         stage / TERMUX_LAUNCHER)
            (stage / TERMUX_LAUNCHER).chmod(0o755)
            verify_pinned_libusb(args.libusb_source_archive.resolve(), stage)
            ndk = args.ndk_root.resolve()
            properties = ndk / "source.properties"
            revision = next((line.split("=", 1)[1].strip() for line in properties.read_text().splitlines()
                             if line.startswith("Pkg.Revision = ")), "")
            if not revision.startswith("27."):
                fail(f"Android packaging requires NDK r27, got {revision or 'unknown'}")
            for name in ("NOTICE", "NOTICE.toolchain", "source.properties"):
                copy_regular(ndk / name, stage / "ndk" / name)
        if args.platform == "darwin-arm64":
            # Strip only the staged copies. The build tree remains available
            # for diagnostics and is never used as the release evidence input.
            strip_darwin_stage(stage)
            verify_darwin_stage_runtime(stage)
        write_text(stage / "DEPENDENCY-NOTICE.txt",
                   render_dependency_notice(args.repo_root, args.version, args.platform, revision))
        darwin_stage = stage / "ifd" / "px4-userland-ifd.bundle"
        audit_args = argparse.Namespace(
            platform=args.platform,
            build_dir=stage if args.platform == "darwin-arm64" else binary_root,
            binary_suffix=args.binary_suffix,
            repo_root=args.repo_root,
            ifd_library=args.ifd_library,
            ifd_bundle=darwin_stage if args.platform == "darwin-arm64" else args.ifd_bundle,
            link_map_dir=args.link_map_dir,
            ndk_root=args.ndk_root,
            inventory_dir=stage / "evidence" / "inventory",
        )
        evidence = audit_binaries(audit_args)
        (stage / "evidence").mkdir(exist_ok=True)
        write_text(stage / "evidence" / "binary-audit.json", json.dumps(evidence, indent=2, sort_keys=True) + "\n")
        manifest = {
            "schema": 1,
            "version": args.version,
            "platform": args.platform,
            "libc": ("glibc" if args.platform.startswith("linux-glibc-") else
                      "musl" if args.platform.startswith("linux-musl-") else
                      "android" if args.platform.startswith("android-") else "darwin"),
            "architecture": args.platform.rsplit("-", 1)[-1],
            "embedded_libusb": {"version": "1.0.30", "linkage": "static"}
                if args.platform.startswith("linux-") or args.platform.startswith("android-") else None,
            "source_ref": args.source_ref,
            "programs": ["px4d", "px4-ts", "px4ctl"],
            "production_only": True,
            "files": {},
        }
        for path in sorted(stage.rglob("*")):
            if path.is_file():
                manifest["files"][path.relative_to(stage).as_posix()] = {"size": path.stat().st_size, "sha256": sha256(path)}
        write_text(stage / "manifest.json", json.dumps(manifest, indent=2, sort_keys=True) + "\n")
        write_checksums(stage)
        deterministic_tar(stage, output)
    audit_binary_archive(argparse.Namespace(archive=output, platform=args.platform))
    print(output)
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (AuditError, OSError, ValueError, KeyError, RuntimeError) as error:
        print(f"package-artifact: {error}", file=sys.stderr)
        raise SystemExit(1)
