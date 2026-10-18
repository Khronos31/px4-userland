#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-only
"""Create the exact corresponding-source archive."""

from __future__ import annotations

import argparse
import gzip
import hashlib
import importlib.util
import io
import json
from pathlib import Path, PurePosixPath
import re
import subprocess
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
VERSION_RE = _audit.VERSION_RE
audit_source_archive = _audit.audit_source_archive
fail = _audit.fail


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def write_file(path: Path, data: bytes, mode: int = 0o644) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_bytes(data)
    path.chmod(mode)


def safe_name(name: str) -> None:
    if "\\" in name or name.startswith("/") or ".." in PurePosixPath(name).parts:
        fail(f"unsafe source member: {name}")


def git_output(root: Path, *arguments: str) -> str:
    try:
        return subprocess.check_output(
            ["git", "-C", str(root), *arguments], text=True, stderr=subprocess.STDOUT
        ).strip()
    except (OSError, subprocess.CalledProcessError) as error:
        fail(f"git query failed: {error}")


def ref_version(root: Path, ref: str) -> str:
    try:
        value = subprocess.check_output(
            ["git", "-C", str(root), "show", f"{ref}:VERSION"], stderr=subprocess.STDOUT
        ).decode("ascii")
    except (OSError, subprocess.CalledProcessError, UnicodeDecodeError) as error:
        fail(f"cannot read VERSION from source snapshot {ref!r}: {error}")
    if not value.endswith("\n") or value.count("\n") != 1:
        fail(f"VERSION in source snapshot {ref!r} must be exactly N.N.N followed by one newline")
    value = value[:-1]
    if not VERSION_RE.fullmatch(value):
        fail(f"VERSION in source snapshot {ref!r} must be exactly N.N.N followed by one newline")
    return value


def add_git_snapshot(root: Path, ref: str, stage: Path) -> tuple[str, str]:
    commit = git_output(root, "rev-parse", f"{ref}^{{commit}}")
    tree = git_output(root, "rev-parse", f"{ref}^{{tree}}")
    try:
        archive = subprocess.check_output(["git", "-C", str(root), "archive", "--format=tar", ref])
    except (OSError, subprocess.CalledProcessError) as error:
        fail(f"git archive failed: {error}")
    extracted: dict[str, tuple[tarfile.TarInfo, bytes | None]] = {}
    with tarfile.open(fileobj=io.BytesIO(archive), mode="r:") as stream:
        for member in stream.getmembers():
            name = member.name
            safe_name(name)
            if member.isdir():
                continue
            if member.issym() or member.islnk():
                fail(f"git archive contains a symlink or hardlink: {name}")
            elif member.isfile():
                handle = stream.extractfile(member)
                if handle is None:
                    fail(f"cannot read git archive member: {name}")
                extracted[name] = (member, handle.read())
            else:
                fail(f"unsupported git archive member: {name}")
    for name in sorted(extracted):
        member, data = extracted[name]
        if data is None:
            fail(f"git archive member has no regular data: {name}")
        destination = stage / "repository" / name
        write_file(destination, data, member.mode & 0o7777)
    return commit, tree


def extract_tar(archive: bytes, destination_root: Path) -> None:
    with tarfile.open(fileobj=io.BytesIO(archive), mode="r:") as stream:
        extracted: dict[str, tuple[tarfile.TarInfo, bytes]] = {}
        for member in stream.getmembers():
            name = member.name
            safe_name(name)
            if member.isdir():
                continue
            if member.issym() or member.islnk():
                fail(f"git archive contains a symlink or hardlink: {name}")
            elif member.isfile():
                handle = stream.extractfile(member)
                if handle is None:
                    fail(f"cannot read git archive member: {name}")
                extracted[name] = (member, handle.read())
            else:
                fail(f"unsupported git archive member: {name}")
        for name in sorted(extracted):
            member, data = extracted[name]
            write_file(destination_root / name, data, member.mode & 0o7777)


def read_common_core_revision(product_snapshot: Path) -> str | None:
    lock_path = product_snapshot / "cmake" / "dtv-userland-core.revision"
    if not lock_path.is_file():
        return None
    revision = lock_path.read_text(encoding="utf-8").strip()
    if not re.fullmatch(r"[0-9a-f]{40}", revision):
        fail("cmake/dtv-userland-core.revision must contain a full 40-hex commit")
    if revision == "0" * 40:
        fail("cmake/dtv-userland-core.revision is not yet pinned to a verified commit")
    return revision


def resolve_common_core(repository: str, revision: str, explicit_root: Path | None,
                        work_dir: Path) -> Path:
    # The pinned commit must come from a real Git checkout: an explicit root or
    # a fetch of the pinned repository.  A corresponding-source archive's
    # bundled repository/third_party/dtv-userland-core tree is for offline CMake
    # builds only and is not treated as the pinned Git tree.
    if explicit_root is not None:
        return explicit_root
    target = work_dir / "dtv-userland-core"
    try:
        subprocess.run(
            ["git", "clone", "--quiet", "--no-checkout", repository, str(target)],
            check=True, stderr=subprocess.STDOUT)
        subprocess.run(
            ["git", "-C", str(target), "checkout", "--quiet", revision],
            check=True, stderr=subprocess.STDOUT)
    except (OSError, subprocess.CalledProcessError) as error:
        fail(f"cannot resolve pinned common core source at {revision}: {error}")
    return target


def add_common_core(product_snapshot: Path, stage: Path, explicit_root: Path | None,
                    repository: str, work_dir: Path) -> str | None:
    revision = read_common_core_revision(product_snapshot)
    if revision is None:
        # Historical snapshots predate the shared foundation dependency.
        return None
    common_root = resolve_common_core(repository, revision, explicit_root, work_dir)
    if not (common_root / ".git").exists():
        fail(f"common core source root is not a git checkout: {common_root}")
    commit = git_output(common_root, "rev-parse", f"{revision}^{{commit}}")
    if commit != revision:
        fail(f"common core revision mismatch: lock {revision} resolved to {commit}")
    try:
        archive = subprocess.check_output(
            ["git", "-C", str(common_root), "archive", "--format=tar", revision]
        )
    except (OSError, subprocess.CalledProcessError) as error:
        fail(f"common core git archive failed: {error}")
    destination_root = stage / "repository" / "third_party" / "dtv-userland-core"
    extract_tar(archive, destination_root)
    if not (destination_root / "LICENSE").is_file():
        fail("common core source archive has no LICENSE")
    if not (destination_root / "PROVENANCE.json").is_file():
        fail("common core source archive has no PROVENANCE.json")
    return commit


def _git_commit(root: Path, message: str) -> str:
    run = ["git", "-C", str(root), "-c", "user.email=test@example.invalid",
           "-c", "user.name=source-test"]
    subprocess.run(run + ["init", "-q"], check=True)
    subprocess.run(run + ["add", "-A"], check=True)
    subprocess.run(run + ["commit", "-q", "-m", message], check=True)
    return git_output(root, "rev-parse", "HEAD")


def self_test() -> int:
    with tempfile.TemporaryDirectory(prefix="px4-common-source-test-") as temporary:
        base = Path(temporary)
        common = base / "common"
        write_file(common / "LICENSE", b"MIT License\n\nCopyright (c) 2026 Khronos31\n")
        write_file(common / "PROVENANCE.json", b'{"schema": 1}\n')
        write_file(common / "include/dtv/userland/error.h", b"// SPDX-License-Identifier: MIT\n")
        write_file(common / "CMakeLists.txt", b"project(dtv_userland_core)\n")
        commit = _git_commit(common, "common foundation")

        product = base / "product"
        lock = product / "cmake" / "dtv-userland-core.revision"
        write_file(lock, (commit + "\n").encode())
        stage = base / "stage"
        stage.mkdir()

        exported = add_common_core(product, stage, common, "unused", base / "work")
        if exported != commit:
            fail("self-test exported the wrong common commit")
        root = stage / "repository" / "third_party" / "dtv-userland-core"
        for name in ("LICENSE", "PROVENANCE.json", "include/dtv/userland/error.h"):
            if not (root / name).is_file():
                fail(f"self-test common export is missing {name}")

        write_file(lock, ("0" * 40 + "\n").encode())
        try:
            add_common_core(product, stage, common, "unused", base / "work")
        except AuditError:
            pass
        else:
            fail("self-test accepted an unpinned common revision")

        write_file(lock, ("f" * 40 + "\n").encode())
        try:
            add_common_core(product, stage, common, "unused", base / "work")
        except AuditError:
            pass
        else:
            fail("self-test accepted a missing common commit")

        write_file(lock, (commit + "\n").encode())
        try:
            add_common_core(product, stage, base / "absent", "unused", base / "work")
        except AuditError:
            pass
        else:
            fail("self-test accepted a non-git common source root")

        lock.unlink()
        if add_common_core(product, stage, common, "unused", base / "work") is not None:
            fail("self-test exported a dependency for a historical snapshot without a lock")
    print("package-source self-test: PASS")
    return 0


def add_libusb(archive: Path, stage: Path) -> None:
    if archive.name != "libusb-1.0.30.tar.bz2" or sha256(archive) != LIBUSB_SHA256:
        fail("source archive requires the exact verified libusb 1.0.30 archive")
    destination = stage / "third_party"
    write_file(destination / "libusb-1.0.30.tar.bz2", archive.read_bytes())
    with tarfile.open(archive, "r:bz2") as stream:
        for member in stream.getmembers():
            safe_name(member.name)
            if member.name != "libusb-1.0.30" and not member.name.startswith("libusb-1.0.30/"):
                fail(f"invalid libusb source member: {member.name}")
            if member.isdir():
                continue
            if member.issym() or member.islnk() or not member.isfile():
                fail(f"invalid libusb source member: {member.name}")
            handle = stream.extractfile(member)
            if handle is None:
                fail(f"cannot extract libusb source member: {member.name}")
            write_file(destination / member.name, handle.read(), member.mode & 0o7777)
    if not (destination / "libusb-1.0.30" / "COPYING").is_file():
        fail("exact libusb source has no COPYING")


def write_checksums(stage: Path) -> None:
    rows = []
    for path in sorted(stage.rglob("*")):
        if path.is_file() and path.name != "SHA256SUMS":
            rows.append(f"{sha256(path)}  {path.relative_to(stage).as_posix()}")
    write_file(stage / "SHA256SUMS", ("\n".join(rows) + "\n").encode())


def deterministic_tar(source: Path, destination: Path) -> None:
    with destination.open("wb") as raw:
        with gzip.GzipFile(fileobj=raw, mode="wb", filename="", mtime=0) as compressed:
            with tarfile.open(fileobj=compressed, mode="w|", format=tarfile.PAX_FORMAT) as stream:
                for path in sorted(source.rglob("*")):
                    if path.is_symlink():
                        fail(f"symlink reached source archive writer: {path}")
                    if not path.is_file():
                        continue
                    info = tarfile.TarInfo(path.relative_to(source).as_posix())
                    info.size = path.stat().st_size
                    info.mode = path.stat().st_mode & 0o7777
                    info.mtime = 0
                    info.uid = info.gid = 0
                    info.uname = info.gname = ""
                    with path.open("rb") as handle:
                        stream.addfile(info, handle)


def main() -> int:
    if "--self-test" in sys.argv[1:]:
        return self_test()
    parser = argparse.ArgumentParser()
    parser.add_argument("--version", required=True)
    parser.add_argument("--source-root", type=Path, required=True)
    parser.add_argument("--source-ref", required=True)
    parser.add_argument("--libusb-source-archive", type=Path, required=True)
    parser.add_argument("--common-core-source-root", type=Path, default=None,
                        help="Git checkout used to export the pinned dtv-userland-core "
                             "dependency; when omitted the pinned private repository is "
                             "fetched. A bundled snapshot tree is not used as a Git source")
    parser.add_argument("--common-core-repository",
                        default="git@github.com:Khronos31/dtv-userland-core.git",
                        help="repository used to resolve the pinned dtv-userland-core commit")
    parser.add_argument("--output-dir", type=Path, required=True)
    args = parser.parse_args()
    if not VERSION_RE.fullmatch(args.version):
        fail(f"version must be strict N.N.N, got {args.version!r}")
    root = args.source_root.resolve()
    if args.version != ref_version(root, args.source_ref):
        fail(f"--version {args.version} does not match VERSION in source snapshot {args.source_ref}")
    output_dir = args.output_dir.resolve()
    output_dir.mkdir(parents=True, exist_ok=True)
    output = output_dir / f"px4-userland-{args.version}-source.tar.gz"
    if output.exists() or output.is_symlink():
        fail(f"refusing to overwrite existing archive: {output}")
    with tempfile.TemporaryDirectory(prefix="px4-source-") as temporary:
        stage = Path(temporary) / "stage"
        stage.mkdir()
        commit, tree = add_git_snapshot(root, args.source_ref, stage)
        common_commit = add_common_core(
            stage / "repository", stage,
            args.common_core_source_root.resolve() if args.common_core_source_root else None,
            args.common_core_repository, Path(temporary) / "common-src")
        add_libusb(args.libusb_source_archive.resolve(), stage)
        template = stage / "repository" / "packaging" / "REBUILD.md.in"
        if not template.is_file():
            fail(f"missing rebuild template: {template}")
        rebuild = template.read_text(encoding="utf-8")
        rebuild = rebuild.replace("@VERSION@", args.version).replace("@COMMIT@", commit).replace("@TREE@", tree)
        write_file(stage / "BUILD-RELINK.md", rebuild.encode())
        for name in ("LICENSE", "README.md", "THIRD_PARTY_NOTICES.md"):
            write_file(stage / name, (stage / "repository" / name).read_bytes())
        notice_template = stage / "repository" / "packaging" / "DEPENDENCY-NOTICE.txt.in"
        if not notice_template.is_file():
            fail(f"missing dependency notice template: {notice_template}")
        notice = notice_template.read_text(encoding="utf-8")
        notice = notice.replace("@VERSION@", args.version).replace("@PLATFORM@", "corresponding-source")
        dependency_text = (
            "dependency.libusb.version=1.0.30\n"
            "dependency.libusb.license=LGPL-2.1-or-later\n"
            "dependency.libusb.linkage=static\n"
        )
        if common_commit is not None:
            dependency_text += (
                "dependency.dtv-userland-core.license=MIT\n"
                f"dependency.dtv-userland-core.commit={common_commit}\n"
            )
        dependency_text += (
            "corresponding-source-archive=present\n\n"
            "This archive contains the exact repository source snapshot and verified libusb 1.0.30 source "
            "needed to rebuild or relink the Linux, macOS, Android, and Windows binaries. "
            "On each of those targets, px4d statically links libusb 1.0.30. px4-ts, px4ctl, and the "
            "Linux and macOS PC/SC IFD adapters, and the Windows application-local WinSCard DLL do not link libusb. "
            "The Windows rebuild produces x64 binaries, including winscard/WinSCard.dll, and uses "
            "scripts/build-windows.sh; pass a modified copy with --libusb-source-dir. "
            "See BUILD-RELINK.md, THIRD_PARTY_NOTICES.md, and third_party/libusb-1.0.30/COPYING.\n"
        )
        if common_commit is not None:
            dependency_text += (
                "This archive also contains the pinned MIT dtv-userland-core foundation library under "
                "repository/third_party/dtv-userland-core, including its LICENSE and PROVENANCE.json.\n"
            )
        notice = notice.replace("@DEPENDENCY_TEXT@", dependency_text)
        write_file(stage / "DEPENDENCY-NOTICE.txt", notice.encode())
        manifest = {
            "schema": 1,
            "version": args.version,
            "kind": "corresponding-source",
            "repository_commit": commit,
            "repository_tree": tree,
            "libusb_version": "1.0.30",
            "libusb_archive_sha256": LIBUSB_SHA256,
            "forbidden_materials_excluded": [".git", "build outputs", "firmware", "APK/add-on", "vendor drivers"],
            "files": {},
        }
        if common_commit is not None:
            manifest["dtv_userland_core_commit"] = common_commit
            manifest["dtv_userland_core_license"] = "MIT"
        for path in sorted(stage.rglob("*")):
            if path.is_file():
                manifest["files"][path.relative_to(stage).as_posix()] = {"size": path.stat().st_size, "sha256": sha256(path)}
        write_file(stage / "source-manifest.json", (json.dumps(manifest, indent=2, sort_keys=True) + "\n").encode())
        write_checksums(stage)
        deterministic_tar(stage, output)
    audit_source_archive(argparse.Namespace(archive=output))
    print(output)
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (AuditError, OSError, ValueError, KeyError, RuntimeError) as error:
        print(f"package-source: {error}", file=sys.stderr)
        raise SystemExit(1)
