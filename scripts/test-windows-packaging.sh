#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-only
# Offline Windows packaging/audit tests: synthetic PE/zip envelope
# rejection, deterministic zip, and corresponding-source Windows coverage.
set -eu
script_dir=$(CDPATH='' cd -- "$(dirname -- "$0")" && pwd)
root=$(CDPATH='' cd -- "$script_dir/.." && pwd)
test_root=$(mktemp -d /tmp/px4-windows-src.XXXXXX)
trap 'find "$test_root" -depth -delete' EXIT

shellcheck "$script_dir/build-windows.sh" "$script_dir/test-windows-static-relink.sh"
python3 -m py_compile "$script_dir/audit-artifact.py" "$script_dir/package-artifact.py" "$script_dir/package-source.py"
python3 "$script_dir/audit-artifact.py" --self-test >/dev/null
python3 "$script_dir/package-artifact.py" --self-test >/dev/null
python3 - "$root" <<'PY'
import importlib.util
import os
from pathlib import Path
import sys

root = Path(sys.argv[1])
# The export map is owned once by the shared Core adapter.  Use the same
# explicit local source override as the build; a corresponding-source archive
# carries it under third_party/dtv-userland-core instead.
override = os.environ.get("DTV_USERLAND_CORE_SOURCE_DIR")
core_root = Path(override) if override else root / "third_party" / "dtv-userland-core"
export_map = core_root / "adapters" / "windows_winscard" / "winscard.def"
if not export_map.is_file():
    raise SystemExit(f"shared WinSCard export map not found: {export_map}")
spec = importlib.util.spec_from_file_location("audit", root / "scripts/audit-artifact.py")
audit = importlib.util.module_from_spec(spec)
spec.loader.exec_module(audit)
exports = {}
for line in export_map.read_text().splitlines():
    parts = line.strip().split()
    if not parts or parts[0] in ("LIBRARY", "EXPORTS"):
        continue
    if parts[0] in exports or len(parts) > 2 or (len(parts) == 2 and parts[1] != "DATA"):
        raise SystemExit("WinSCard export map must contain unique named code/DATA exports without ordinals")
    exports[parts[0]] = parts[1:]
expected = {name: [] for name in audit.WINDOWS_WINSCARD_FUNCTIONS}
expected.update({name: ["DATA"] for name in audit.WINDOWS_WINSCARD_PCI})
if exports != expected:
    raise SystemExit("WinSCard export map and package audit contract differ")
print("WinSCard export map matches the named code/DATA audit contract")
PY

libusb_archive=${PX4_LIBUSB_1_0_30_ARCHIVE:-}
if [ -z "$libusb_archive" ]; then
    printf '%s\n' 'PX4_LIBUSB_1_0_30_ARCHIVE is required to exercise the Windows source archive'
    exit 0
fi
[ -f "$libusb_archive" ] || {
    printf '%s\n' "PX4_LIBUSB_1_0_30_ARCHIVE is not a file: $libusb_archive" >&2
    exit 1
}

python3 - "$script_dir" "$root" "$test_root" "$libusb_archive" <<'PY'
from __future__ import annotations

import subprocess
import sys
from pathlib import Path

script_dir = Path(sys.argv[1])
root = Path(sys.argv[2])
test_root = Path(sys.argv[3])
libusb_archive = Path(sys.argv[4])
version = (root / "VERSION").read_text(encoding="ascii").strip()

REQUIRED = {
    "LICENSE": "license\n",
    "README.md": "readme\n",
    "THIRD_PARTY_NOTICES.md": "notices\n",
    "scripts/build-linux-static.sh": "#!/bin/sh\n",
    "scripts/build-linux-ifd.sh": "#!/bin/sh\n",
    "scripts/build-macos-static.sh": "#!/bin/sh\n",
    "scripts/test-static-relink.sh": "#!/bin/sh\n",
}
WINDOWS = {
    "CMakeLists.txt": "# build\n",
    "cmake/DtvUserlandCoreDependency.cmake": "# core dependency\n",
    "docs/platforms/windows-winscard.md": "WinSCard placement and limits\n",
    "scripts/build-windows.sh": "#!/bin/sh\n",
    "scripts/test-windows-static-relink.sh": "#!/bin/sh\n",
    "userland/src/ipc.cpp": "// ipc facade\n",
    "userland/src/control_client.cpp": "// control client\n",
    "userland/src/windows/windows_sleep.cpp": "// sleep\n",
    "userland/include/px4/control_client.h": "// control client header\n",
    "userland/include/px4/ipc.h": "// ipc header\n",
    "userland/include/px4/posix_ipc.h": "// posix ipc header\n",
    "userland/include/px4/posix_tuner_nonce.h": "// nonce header\n",
    "userland/tests/windows_winscard_client_tests.cpp": "// client tests\n",
    "userland/tests/windows_winscard_proxy_tests.cpp": "// proxy tests\n",
    "userland/tests/windows_winscard_import_tests.cpp": "// import tests\n",
    "userland/tools/px4_ts_windows.cpp": "// ts\n",
    "userland/tools/px4_windows_args.h": "// args\n",
    "userland/tests/windows_platform_tests.cpp": "// tests\n",
    "userland/tests/windows_worker_tests_main.cpp": "// main\n",
    "userland/include/px4/windows_tuner_nonce.h": "// header\n",
}
# The single GPL WinSCard source lives in the pinned shared core, never in the
# product tree.  The fixture supplies it as its own synthetic Git dependency.
HOST_RUNTIME = {
    "COPYING": "GPL-2.0-only\n",
    "README.md": "host runtime\n",
    "include/px4/posix_ipc_contract.h": "// ipc contract\n",
    "include/px4/posix_tuner_nonce_internal.h": "// nonce seam\n",
    "include/windows/windows_security.h": "// security\n",
    "posix_ipc.cpp": "// posix ipc\n",
    "posix_tuner_nonce.cpp": "// posix nonce\n",
    "windows/windows_ipc.cpp": "// windows ipc\n",
    "windows/windows_tuner_nonce.cpp": "// windows nonce\n",
}
WINSCARD_ADAPTER = {
    "CMakeLists.txt": "# adapter\n",
    "README.md": "adapter\n",
    "COPYING": "GPL-2.0-only\n",
    "winscard.def": "EXPORTS\n",
    "windows/winscard_card_client.h": "// card client header\n",
    "windows/winscard_card_client.cpp": "// card client\n",
    "windows/winscard_native.h": "// native API\n",
    "windows/winscard_proxy.cpp": "// WinSCard proxy\n",
    "windows/winscard_product.h.in": "// product header template\n",
}
CORE_BASE = {
    "LICENSE": "MIT License\n\nCopyright (c) 2026 Khronos31\n",
    "PROVENANCE.json": '{"schema": 1}\n',
    "CMakeLists.txt": "project(dtv_userland_core)\n",
    "cmake/dtv_userland_coreConfig.cmake.in": "# config\n",
}
for _name in ("error", "logging", "transport", "mock_transport", "wire"):
    CORE_BASE[f"include/dtv/userland/{_name}.h"] = f"// {_name}\n"
for _name in ("error", "logging", "mock_transport", "wire"):
    CORE_BASE[f"src/{_name}.cpp"] = f"// {_name}\n"


def commit_repo(repo: Path, message: str) -> str:
    subprocess.run(["git", "init", "-q", str(repo)], check=True)
    subprocess.run(["git", "-C", str(repo), "config", "user.email", "self@test"], check=True)
    subprocess.run(["git", "-C", str(repo), "config", "user.name", "self"], check=True)
    subprocess.run(["git", "-C", str(repo), "add", "-A"], check=True)
    subprocess.run(["git", "-C", str(repo), "commit", "-q", "-m", message], check=True)
    return subprocess.check_output(
        ["git", "-C", str(repo), "rev-parse", "HEAD"], text=True).strip()


def make_core(name: str, omit: str | None = None,
              omit_runtime: str | None = None) -> tuple[Path, str]:
    core = test_root / name
    files = dict(CORE_BASE)
    for relative, text in WINSCARD_ADAPTER.items():
        if relative != omit:
            files[f"adapters/windows_winscard/{relative}"] = text
    for relative, text in HOST_RUNTIME.items():
        if relative != omit_runtime:
            files[f"adapters/host_runtime/{relative}"] = text
    for relative, text in files.items():
        path = core / relative
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(text, encoding="utf-8")
    return core, commit_repo(core, "core fixture")


def make_repo(name: str, extra: dict[str, str], core_commit: str) -> Path:
    repo = test_root / name
    (repo / "packaging").mkdir(parents=True)
    for relative, text in {**REQUIRED, **extra}.items():
        path = repo / relative
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(text, encoding="utf-8")
    (repo / "VERSION").write_text(version + "\n", encoding="ascii")
    (repo / "cmake").mkdir(parents=True, exist_ok=True)
    (repo / "cmake" / "dtv-userland-core.revision").write_text(
        core_commit + "\n", encoding="ascii")
    for template in ("REBUILD.md.in", "DEPENDENCY-NOTICE.txt.in"):
        (repo / "packaging" / template).write_text(
            (root / "packaging" / template).read_text(encoding="utf-8"), encoding="utf-8")
    # The runtime license members are part of the existing required closure.
    (repo / "distribution" / "licenses").mkdir(parents=True, exist_ok=True)
    for license_name in ("gcc-COPYING3", "gcc-COPYING.RUNTIME", "musl-1.2.5-COPYRIGHT"):
        (repo / "distribution" / "licenses" / license_name).write_bytes(
            (root / "distribution" / "licenses" / license_name).read_bytes())
    commit_repo(repo, "fixture")
    return repo


def package(repo: Path, output: Path, core: Path) -> subprocess.CompletedProcess:
    return subprocess.run(
        [sys.executable, str(script_dir / "package-source.py"), "--version", version,
         "--source-root", str(repo), "--source-ref", "HEAD",
         "--common-core-source-root", str(core),
         "--libusb-source-archive", str(libusb_archive), "--output-dir", str(output)],
        stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True,
    )


valid_core, valid_core_commit = make_core("core")
valid_repo = make_repo("valid", WINDOWS, valid_core_commit)
valid_out = test_root / "valid-out"
result = package(valid_repo, valid_out, valid_core)
if result.returncode != 0:
    raise SystemExit(f"valid Windows source archive was rejected: {result.stdout}")
archive = valid_out / f"px4-userland-{version}-source.tar.gz"
audit = subprocess.run(
    [sys.executable, str(script_dir / "audit-artifact.py"), "--source-archive", "--archive", str(archive)],
    stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True,
)
if audit.returncode != 0:
    raise SystemExit(f"valid Windows source archive failed audit: {audit.stdout}")
print("valid Windows corresponding-source archive accepted")

for index, missing in enumerate(name for name in WINDOWS if "winscard" in name.lower()):
    incomplete = dict(WINDOWS)
    incomplete.pop(missing)
    repo = make_repo(f"incomplete-winscard-{index}", incomplete, valid_core_commit)
    if package(repo, test_root / f"incomplete-winscard-out-{index}", valid_core).returncode == 0:
        raise SystemExit(f"WinSCard source archive without {missing} was accepted")
print("incomplete WinSCard implementation and import-test source rejected")

for index, missing in enumerate(WINSCARD_ADAPTER):
    core, core_commit = make_core(f"core-broken-{index}", omit=missing)
    repo = make_repo(f"incomplete-adapter-{index}", WINDOWS, core_commit)
    if package(repo, test_root / f"incomplete-adapter-out-{index}", core).returncode == 0:
        raise SystemExit(f"WinSCard source archive without shared adapter {missing} was accepted")
print("incomplete shared WinSCard adapter, export map, and generated-header source rejected")

for index, missing in enumerate(HOST_RUNTIME):
    core, core_commit = make_core(f"core-runtime-broken-{index}", omit_runtime=missing)
    repo = make_repo(f"incomplete-runtime-{index}", WINDOWS, core_commit)
    if package(repo, test_root / f"incomplete-runtime-out-{index}", core).returncode == 0:
        raise SystemExit(f"Windows source archive without shared runtime {missing} was accepted")
print("incomplete shared Windows runtime source, security helper, and nonce seam rejected")

partial = {"userland/src/windows/windows_sleep.cpp": "// sleep\n"}
partial_repo = make_repo("partial", partial, valid_core_commit)
partial_out = test_root / "partial-out"
if package(partial_repo, partial_out, valid_core).returncode == 0:
    raise SystemExit("partial Windows source tree was accepted")
print("partial Windows source coverage rejected")

legacy_repo = make_repo("legacy", {"drivers/windows/legacy.c": "int x;\n"}, valid_core_commit)
legacy_out = test_root / "legacy-out"
if package(legacy_repo, legacy_out, valid_core).returncode == 0:
    raise SystemExit("legacy Windows driver tree was accepted")
print("legacy Windows driver tree rejected")

win32_repo = make_repo("win32", {"win32/host.c": "int x;\n"}, valid_core_commit)
win32_out = test_root / "win32-out"
if package(win32_repo, win32_out, valid_core).returncode == 0:
    raise SystemExit("legacy win32 tree was accepted")
print("legacy win32 tree rejected")
PY

printf '%s\n' 'Windows packaging/audit self-tests: PASS'
