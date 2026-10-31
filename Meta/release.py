#!/usr/bin/env python3

from __future__ import annotations

import argparse
import base64
import email.utils
import functools
import hashlib
import http.server
import importlib.util
import json
import os
import plistlib
import queue
import re
import shutil
import subprocess
import sys
import tarfile
import tempfile
import threading
import time
import urllib.request

from pathlib import Path
from xml.etree import ElementTree
from xml.sax.saxutils import escape
from xml.sax.saxutils import quoteattr

XENON_SOURCE_DIR = Path(__file__).resolve().parent.parent
LADYBIRD_SOURCE_DIR = XENON_SOURCE_DIR / "ladybird"
BUILD_DIR = XENON_SOURCE_DIR / "Build" / "release"
DISTRIBUTION_DIR = XENON_SOURCE_DIR / "Build" / "distribution"

XENON_REPOSITORY = "ThaRae/Xenon"
RELEASES_REPOSITORY = "ThaRae/xenon-releases"
RELEASES_URL = f"https://github.com/{RELEASES_REPOSITORY}"
FEED_URL = f"https://raw.githubusercontent.com/{RELEASES_REPOSITORY}/main/appcast.xml"
GIT_WITH_GITHUB_CREDENTIALS = ("git", "-c", "credential.helper=", "-c", "credential.helper=!gh auth git-credential")

SIGNING_IDENTITY = "Xenon Release"
UPDATE_KEY_ACCOUNT = "xenon"

APP_NAME = "Xenon.app"
BUNDLE_IDENTIFIER = "org.xenon.Xenon"

DMGBUILD_VERSION = "1.6.7"
INSTALLER_BACKGROUND = XENON_SOURCE_DIR / "UI" / "Icons" / "macos" / "installer-background.png"
INSTALLER_WINDOW_SIZE = (640, 400)
INSTALLER_ICON_SIZE = 128
INSTALLER_ICON_LOCATIONS = {APP_NAME: (170, 190), "Applications": (470, 190)}

QT_PLUGINS = (
    "platforms/libqcocoa.dylib",
    "styles/libqmacstyle.dylib",
    "imageformats/libqgif.dylib",
    "imageformats/libqico.dylib",
    "imageformats/libqjpeg.dylib",
)

TEST_BUNDLE_IDENTIFIER = "org.xenon.Xenon.UpdateTest"
TEST_VERSIONS = ("0.0.1", "0.0.2", "0.0.3")
TEST_TIMEOUT_SECONDS = 180
TEST_STAND_IN_VERSION = "99.0.0"

LINUX_ARCHITECTURE = "x86_64"
LINUX_PREFIX_DIR = BUILD_DIR / "xenon"
LINUX_DIR = XENON_SOURCE_DIR / "UI" / "Linux"
LINUX_ICONS_DIR = XENON_SOURCE_DIR / "UI" / "Icons" / "linux" / "hicolor"

APPIMAGE_TOOLS = {
    "linuxdeploy": (
        "https://github.com/linuxdeploy/linuxdeploy/releases/download/1-alpha-20251107-1/linuxdeploy-x86_64.AppImage",
        "c20cd71e3a4e3b80c3483cef793cda3f4e990aca14014d23c544ca3ce1270b4d",
    ),
    "appimagetool": (
        "https://github.com/AppImage/appimagetool/releases/download/1.9.1/appimagetool-x86_64.AppImage",
        "ed4ce84f0d9caff66f50bcca6ff6f35aae54ce8135408b3fa33abfc3cb384eb0",
    ),
    "runtime": (
        "https://github.com/AppImage/type2-runtime/releases/download/20251108/runtime-x86_64",
        "2fca8b443c92510f1483a883f60061ad09b46b978b2631c807cd873a47ec260d",
    ),
}

LINUX_QT_PLUGINS = (
    "platforms/libqxcb.so",
    "platforms/libqwayland*.so",
    "xcbglintegrations/*.so",
    "wayland-shell-integration/*.so",
    "wayland-decoration-client/*.so",
    "wayland-graphics-integration-client/*.so",
    "platforminputcontexts/*.so",
    "imageformats/*.so",
    "iconengines/*.so",
    "position/libqtposition_geoclue2.so",
    "position/libqtposition_positionpoll.so",
)
REQUIRED_LINUX_QT_PLUGINS = ("platforms/libqxcb.so", "platforms/libqwayland*.so")

APPIMAGE_UPDATE_INFORMATION = "gh-releases-zsync|{}|{}|latest|Xenon-*-{}.AppImage.zsync".format(
    *RELEASES_REPOSITORY.split("/"), LINUX_ARCHITECTURE
)

HTTPS_TEST_URL = "https://example.com/"
HTTPS_TEST_TEXT = "documentation examples"


def load_module(name: str, path: Path):
    spec = importlib.util.spec_from_file_location(name, path)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


bundler = load_module("package_macos_test_bundle", LADYBIRD_SOURCE_DIR / "Meta" / "package-macos-test-bundle.py")


def main():
    parser = argparse.ArgumentParser(description="Package, publish and test Xenon's releases")
    subparsers = parser.add_subparsers(dest="command", required=True)

    package_parser = subparsers.add_parser("package", help="build and package a release into Build/distribution")
    package_parser.add_argument("--no-build", action="store_true", help="package the build as it is")
    package_parser.add_argument("--version", help="the version to release as, rather than VERSION.txt's")
    package_parser.add_argument(
        "--sign", default=SIGNING_IDENTITY, help='the signing identity, or "-" for ad hoc (macOS)'
    )
    package_parser.add_argument(
        "--update-key", type=Path, help="a file with the update key, rather than the keychain's (macOS)"
    )
    package_parser.add_argument("--feed-url", default=FEED_URL, help="where the release looks for updates (macOS)")
    package_parser.add_argument("--notes", type=Path, help="a file with the release notes, in plain text (macOS)")

    publish_parser = subparsers.add_parser("publish", help="publish Build/distribution to ThaRae/xenon-releases")
    publish_parser.add_argument("--yes", action="store_true", help="don't ask before publishing")

    test_parser = subparsers.add_parser(
        "test", help="test that a release updates itself to the next (macOS), or that the AppImage runs (Linux)"
    )
    test_parser.add_argument("--no-build", action="store_true", help="test the build as it is (macOS)")
    test_parser.add_argument(
        "--sign", default="-", help='the signing identity, or "-" for ad hoc (the default) (macOS)'
    )
    test_parser.add_argument("--appimage", type=Path, help="the AppImage to test, rather than package's (Linux)")
    test_parser.add_argument(
        "--installed", type=Path, help="an installed Xenon to test, such as the AUR packages' xenon (Linux)"
    )

    args = parser.parse_args()
    if sys.platform not in ("darwin", "linux"):
        sys.exit("Xenon's releases are packaged on macOS and Linux")
    on_mac = sys.platform == "darwin"

    if args.command == "package" and on_mac:
        package_main(args)
    elif args.command == "package":
        package_linux_main(args)
    elif args.command == "publish":
        publish_main(args)
    elif args.command == "test" and on_mac:
        test_main(args)
    elif args.command == "test":
        test_linux_main(args)


def run(*command, **kwargs) -> str:
    result = subprocess.run([str(part) for part in command], capture_output=True, text=True, **kwargs)
    if result.returncode != 0:
        sys.exit(f"{' '.join(str(part) for part in command)} failed:\n{result.stdout}{result.stderr}")
    return result.stdout


def read_version() -> str:
    version = (XENON_SOURCE_DIR / "VERSION.txt").read_text().strip()
    if not re.fullmatch(r"\d+\.\d+\.\d+", version):
        sys.exit(f"VERSION.txt must be major.minor.patch: {version}")
    return version


def build():
    print("==> Building Xenon", flush=True)
    subprocess.run([sys.executable, XENON_SOURCE_DIR / "Meta" / "xenon.py", "build"], check=True)


def sparkle_tool(name: str) -> Path:
    tools = sorted((BUILD_DIR / "Xenon").glob(f"Sparkle-*/bin/{name}"))
    if not tools:
        sys.exit(f"No Sparkle {name} in {BUILD_DIR / 'Xenon'}. Build Xenon first.")
    return tools[-1]


def find_openssl() -> str:
    for candidate in (
        shutil.which("openssl"),
        "/opt/homebrew/opt/openssl@3/bin/openssl",
        "/usr/local/opt/openssl@3/bin/openssl",
    ):
        if candidate and Path(candidate).exists():
            version = subprocess.run([candidate, "version"], capture_output=True, text=True).stdout
            if version.startswith("OpenSSL") and not version.startswith(("OpenSSL 0.", "OpenSSL 1.")):
                return candidate
    sys.exit("This needs OpenSSL 3 or later: brew install openssl@3")


ED25519_PKCS8_PREFIX = bytes.fromhex("302e020100300506032b657004220420")


class UpdateKey:
    def __init__(self, file: Path | None):
        self.file = file

    def options(self) -> list:
        return ["--ed-key-file", self.file] if self.file else ["--account", UPDATE_KEY_ACCOUNT]

    def public_key(self) -> str:
        if not self.file:
            result = subprocess.run(
                [sparkle_tool("generate_keys"), "--account", UPDATE_KEY_ACCOUNT, "-p"], capture_output=True, text=True
            )
            if result.returncode != 0 or not result.stdout.strip():
                sys.exit(
                    f'No update key in the keychain under "{UPDATE_KEY_ACCOUNT}". Documentation/Updates.md says how to '
                    "make one, or pass --update-key."
                )
            return result.stdout.strip()

        seed = base64.b64decode(self.file.read_text().strip())
        if len(seed) != 32:
            sys.exit(f"{self.file} doesn't hold an Ed25519 seed in base64")
        public = subprocess.run(
            [find_openssl(), "pkey", "-inform", "DER", "-pubout", "-outform", "DER"],
            input=ED25519_PKCS8_PREFIX + seed,
            capture_output=True,
            check=True,
        ).stdout[-32:]
        return base64.b64encode(public).decode()

    def sign(self, path: Path) -> str:
        return run(sparkle_tool("sign_update"), *self.options(), "-p", path).strip()

    @staticmethod
    def generate(path: Path):
        der = subprocess.run(
            [find_openssl(), "genpkey", "-algorithm", "ed25519", "-outform", "DER"], capture_output=True, check=True
        ).stdout
        path.write_text(base64.b64encode(der[-32:]).decode() + "\n")
        return UpdateKey(path)


def deploy_qt_plugins(app: Path, plugins_directory: Path):
    for plugin in QT_PLUGINS:
        destination = app / "Contents" / "PlugIns" / plugin
        destination.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(plugins_directory / plugin, destination)
    (app / "Contents" / "Resources" / "qt.conf").write_text("[Paths]\nPlugins = PlugIns\n")
    bundler.rpath_entries.cache_clear()


def make_bundle(app: Path):
    built_app = BUILD_DIR / "bin" / APP_NAME
    built_executable = built_app / "Contents" / "MacOS" / "Xenon"
    if not built_executable.is_file():
        sys.exit(f"No Xenon at {built_app}. Build it first.")

    search_directories = [Path(entry) for entry in bundler.rpath_entries(built_executable) if entry.startswith("/")]
    search_directories = [directory for directory in search_directories if directory.is_dir()]

    if app.exists():
        shutil.rmtree(app)
    app.parent.mkdir(parents=True, exist_ok=True)
    run("ditto", built_app, app)

    contents = app / "Contents"
    (contents / "lib").unlink()
    shutil.rmtree(contents / "_CodeSignature", ignore_errors=True)
    framework = contents / "Frameworks" / "Sparkle.framework"
    shutil.rmtree(framework / "Versions" / "B" / "XPCServices")
    (framework / "XPCServices").unlink()

    bundler.rpath_entries.cache_clear()
    deploy_qt_plugins(app, bundler.find_qt_plugins_directory(search_directories))
    bundler.fix_rpaths(app)
    copied = bundler.copy_linked_libraries(app, search_directories)
    bundler.fix_rpaths(app)

    for binary in bundler.mach_o_files(contents / "MacOS", contents / "lib", contents / "PlugIns"):
        run("strip", "-x", binary)
    bundler.rpath_entries.cache_clear()
    print(f"    copied {len(copied)} libraries into the bundle, and stripped it")


def configure_bundle(app: Path, version: str, feed_url: str, public_key: str, bundle_identifier: str):
    info_path = app / "Contents" / "Info.plist"
    info = plistlib.loads(info_path.read_bytes())
    info.update(
        {
            "CFBundleIdentifier": bundle_identifier,
            "CFBundleShortVersionString": version,
            "CFBundleVersion": version,
            "SUFeedURL": feed_url,
            "SUPublicEDKey": public_key,
            "SUVerifyUpdateBeforeExtraction": True,
            "SURequireSignedFeed": True,
        }
    )
    info_path.write_bytes(plistlib.dumps(info))


def check_signing_identity(identity: str):
    if identity == "-":
        return
    identities = run("security", "find-identity", "-p", "codesigning")
    if f'"{identity}"' not in identities:
        sys.exit(
            f'No "{identity}" signing identity in the keychain. Documentation/Updates.md says how to make one, or pass '
            "--sign - to sign ad hoc."
        )


def helper_entitlements(executable: Path, team_signed: bool, directory: Path) -> Path:
    release = BUILD_DIR / "Services" / executable.name / f"{executable.name}.release.entitlements"
    entitlements = plistlib.loads(release.read_bytes()) if release.is_file() else {}
    if not team_signed:
        entitlements["com.apple.security.cs.disable-library-validation"] = True
    path = directory / f"{executable.name}.entitlements"
    path.write_bytes(plistlib.dumps(entitlements))
    return path


def sign_bundle(app: Path, identity: str):
    team_signed = identity.startswith("Developer ID Application:")
    timestamp = "--timestamp" if team_signed else "--timestamp=none"

    def codesign(path: Path, *options):
        run("codesign", "--force", "--sign", identity, timestamp, *options, path)

    contents = app / "Contents"
    sparkle = contents / "Frameworks" / "Sparkle.framework"
    codesign(sparkle / "Versions" / "B" / "Autoupdate", "--options", "runtime")
    codesign(sparkle / "Versions" / "B" / "Updater.app", "--options", "runtime", "--preserve-metadata=entitlements")
    codesign(sparkle, "--options", "runtime")

    for binary in bundler.mach_o_files(contents / "lib", contents / "PlugIns"):
        codesign(binary)

    with tempfile.TemporaryDirectory() as entitlements_directory:
        for executable in bundler.mach_o_files(contents / "MacOS"):
            if executable.name == "Xenon":
                continue
            entitlements = helper_entitlements(executable, team_signed, Path(entitlements_directory))
            codesign(executable, "--options", "runtime", "--entitlements", entitlements)

    codesign(app, "--entitlements", LADYBIRD_SOURCE_DIR / "Meta" / "Entitlements.plist")

    bundler.verify(app)
    run("codesign", "--verify", "--deep", "--strict", app)


def dmgbuild_python() -> Path:
    environment = XENON_SOURCE_DIR / "Build" / "tools" / f"dmgbuild-{DMGBUILD_VERSION}"
    python = environment / "bin" / "python"
    if not (environment / "bin" / "dmgbuild").is_file():
        print(f"Installing dmgbuild {DMGBUILD_VERSION} into {environment.relative_to(XENON_SOURCE_DIR)}", flush=True)
        shutil.rmtree(environment, ignore_errors=True)
        run(sys.executable, "-m", "venv", environment)
        run(python, "-m", "pip", "install", "--quiet", "--disable-pip-version-check", f"dmgbuild=={DMGBUILD_VERSION}")
    return python


def make_installer(app: Path, dmg: Path, identity: str):
    settings = {
        "filename": str(dmg),
        "volume_name": "Xenon",
        "format": "ULFO",
        "files": [str(app)],
        "symlinks": {"Applications": "/Applications"},
        "icon": str(app / "Contents" / "Resources" / "app_icon.icns"),
        "background": str(INSTALLER_BACKGROUND),
        "window_rect": ((200, 120), INSTALLER_WINDOW_SIZE),
        "default_view": "icon-view",
        "show_status_bar": False,
        "show_tab_view": False,
        "show_toolbar": False,
        "show_pathbar": False,
        "show_sidebar": False,
        "show_icon_preview": False,
        "icon_size": INSTALLER_ICON_SIZE,
        "text_size": 13,
        "icon_locations": INSTALLER_ICON_LOCATIONS,
    }
    program = (
        "import json, sys, dmgbuild\n"
        "settings = json.load(sys.stdin)\n"
        "dmgbuild.build_dmg(settings.pop('filename'), settings.pop('volume_name'), settings=settings)\n"
    )
    dmg.unlink(missing_ok=True)
    result = subprocess.run(
        [str(dmgbuild_python()), "-c", program], input=json.dumps(settings), capture_output=True, text=True
    )
    if result.returncode != 0 or not dmg.is_file():
        sys.exit(f"dmgbuild failed:\n{result.stdout}{result.stderr}")

    if identity != "-":
        timestamp = "--timestamp" if identity.startswith("Developer ID Application:") else "--timestamp=none"
        run("codesign", "--force", "--sign", identity, timestamp, dmg)
        run("codesign", "--verify", "--strict", dmg)
    run("hdiutil", "verify", "-quiet", dmg)


class MountedImage:
    def __init__(self, dmg: Path):
        self.dmg = dmg

    def __enter__(self) -> Path:
        output = run("hdiutil", "attach", "-nobrowse", "-noautoopen", "-readonly", "-plist", self.dmg)
        entities = plistlib.loads(output.encode())["system-entities"]
        self.mount_point = Path(next(entity["mount-point"] for entity in entities if "mount-point" in entity))
        return self.mount_point

    def __exit__(self, *exception):
        subprocess.run(["hdiutil", "detach", "-force", str(self.mount_point)], capture_output=True)


def check_installer(dmg: Path) -> list[str]:
    problems = []
    with MountedImage(dmg) as volume:
        app = volume / APP_NAME
        applications = volume / "Applications"
        if not app.is_dir():
            problems.append(f"it has no {APP_NAME}")
        elif subprocess.run(["codesign", "--verify", "--deep", "--strict", str(app)], capture_output=True).returncode:
            problems.append(f"its {APP_NAME}'s signature doesn't check out")
        if not applications.is_symlink() or os.readlink(applications) != "/Applications":
            problems.append("it has no link to /Applications")
        if not (volume / ".DS_Store").is_file() or not (volume / ".background.tiff").is_file():
            problems.append("its window has no layout or background")
        if not (volume / ".VolumeIcon.icns").is_file():
            problems.append("its volume has no icon")
    return problems


def make_archive(app: Path, archive: Path):
    compression = "-cJf" if archive.name.endswith(".tar.xz") else "-czf"
    archive.unlink(missing_ok=True)
    run(
        "tar",
        "--no-xattrs",
        compression,
        archive,
        "-C",
        app.parent,
        app.name,
        env=dict(os.environ, COPYFILE_DISABLE="1"),
    )


def write_appcast(path: Path, app: Path, archive: Path, archive_url: str, signature: str, link: str, notes: str | None):
    info = plistlib.loads((app / "Contents" / "Info.plist").read_bytes())
    version = info["CFBundleVersion"]
    architectures = run("lipo", "-archs", app / "Contents" / "MacOS" / "Xenon").split()

    lines = [
        f"            <title>Xenon {escape(info['CFBundleShortVersionString'])}</title>",
        f"            <link>{escape(link)}</link>",
        f"            <pubDate>{email.utils.formatdate(usegmt=True)}</pubDate>",
        f"            <sparkle:version>{escape(version)}</sparkle:version>",
        f"            <sparkle:shortVersionString>{escape(info['CFBundleShortVersionString'])}</sparkle:shortVersionString>",
        f"            <sparkle:minimumSystemVersion>{escape(info['LSMinimumSystemVersion'])}</sparkle:minimumSystemVersion>",
    ]
    if architectures == ["arm64"]:
        lines.append("            <sparkle:hardwareRequirements>arm64</sparkle:hardwareRequirements>")
    if notes:
        lines.append(f'            <description sparkle:descriptionFormat="plain-text">{escape(notes)}</description>')
    lines.append(
        f'            <enclosure url={quoteattr(archive_url)} length="{archive.stat().st_size}" '
        f'type="application/octet-stream" sparkle:edSignature={quoteattr(signature)}/>'
    )

    item = "\n".join(lines)
    path.write_text(f"""<?xml version="1.0" encoding="utf-8"?>
<rss version="2.0" xmlns:sparkle="http://www.andymatuschak.org/xml-namespaces/sparkle">
    <channel>
        <title>Xenon</title>
        <link>{escape(RELEASES_URL)}</link>
        <item>
{item}
        </item>
    </channel>
</rss>
""")


def release_link(version: str) -> str:
    return f"{RELEASES_URL}/releases/tag/v{version}"


def archive_name(version: str) -> str:
    return f"Xenon-{version}.tar.xz"


def installer_name(version: str) -> str:
    return f"Xenon-{version}.dmg"


def appimage_name(version: str) -> str:
    return f"Xenon-{version}-{LINUX_ARCHITECTURE}.AppImage"


def zsync_path(appimage: Path) -> Path:
    return appimage.with_name(f"{appimage.name}.zsync")


def package_main(args):
    version = args.version or read_version()
    check_signing_identity(args.sign)
    if not args.no_build:
        build()

    key = UpdateKey(args.update_key)
    public_key = key.public_key()
    notes = args.notes.read_text().strip() if args.notes else None

    print(f"==> Packaging Xenon {version}", flush=True)
    if DISTRIBUTION_DIR.exists():
        shutil.rmtree(DISTRIBUTION_DIR)
    app = DISTRIBUTION_DIR / APP_NAME
    make_bundle(app)
    configure_bundle(app, version, args.feed_url, public_key, BUNDLE_IDENTIFIER)

    print(f"==> Signing it as {'ad hoc' if args.sign == '-' else args.sign}", flush=True)
    sign_bundle(app, args.sign)

    print("==> Making the installer", flush=True)
    installer = DISTRIBUTION_DIR / installer_name(version)
    make_installer(app, installer, args.sign)
    if problems := check_installer(installer):
        sys.exit(f"The installer isn't right: {'; '.join(problems)}")

    print("==> Archiving it, and signing the archive and the appcast", flush=True)
    archive = DISTRIBUTION_DIR / archive_name(version)
    make_archive(app, archive)
    signature = key.sign(archive)
    archive_url = f"{RELEASES_URL}/releases/download/v{version}/{archive.name}"
    appcast = DISTRIBUTION_DIR / "appcast.xml"
    write_appcast(appcast, app, archive, archive_url, signature, release_link(version), notes)
    key.sign(appcast)
    if notes:
        (DISTRIBUTION_DIR / "notes.txt").write_text(notes + "\n")

    print(f"    {installer} ({installer.stat().st_size / (1024 * 1024):.0f} MiB)")
    print(f"    {archive} ({archive.stat().st_size / (1024 * 1024):.0f} MiB)")
    print(f"    {appcast}")
    print("Check the app, then publish it with Meta/release.py publish.")


def appimage_tool(name: str) -> Path:
    url, sha256 = APPIMAGE_TOOLS[name]
    tag, file_name = url.split("/")[-2:]
    path = XENON_SOURCE_DIR / "Build" / "tools" / "appimage" / f"{name}-{tag}" / file_name
    if not path.is_file():
        print(f"Downloading {url}", flush=True)
        path.parent.mkdir(parents=True, exist_ok=True)
        partial = path.with_name(f"{file_name}.partial")
        with urllib.request.urlopen(url, timeout=120) as response, partial.open("wb") as file:
            shutil.copyfileobj(response, file)
        partial.rename(path)
    digest = hashlib.sha256(path.read_bytes()).hexdigest()
    if digest != sha256:
        path.unlink()
        sys.exit(f"{file_name} from {url} has the SHA-256 {digest}, not {sha256}")
    path.chmod(0o755)
    return path


def run_appimage_tool(name: str, *arguments, cwd: Path | None = None, **variables) -> str:
    environment = dict(os.environ, APPIMAGE_EXTRACT_AND_RUN="1", **variables)
    command = [str(part) for part in (appimage_tool(name), *arguments)]
    result = subprocess.run(
        command, cwd=cwd, env=environment, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True
    )
    if result.returncode != 0:
        sys.exit(f"{name} failed:\n{result.stdout}")
    return result.stdout


def elf_files(directory: Path) -> list[Path]:
    files = []
    for path in sorted(directory.rglob("*")):
        if path.is_file() and not path.is_symlink():
            with path.open("rb") as file:
                if file.read(4) == b"\x7fELF":
                    files.append(path)
    return files


def linked_libraries(binary: Path) -> dict[str, str]:
    libraries = {}
    output = subprocess.run(["ldd", str(binary)], capture_output=True, text=True).stdout
    for line in output.splitlines():
        if match := re.match(r"\s*(\S+) => (.+?)(?: \(0x[0-9a-f]+\))?$", line):
            libraries[match[1]] = match[2]
    return libraries


def needed_libraries(binary: Path) -> set[str]:
    return set(re.findall(r"\(NEEDED\)\s+Shared library: \[(.+)\]", run("readelf", "--dynamic", "--wide", binary)))


def dynamic_symbols(binary: Path, kind: str) -> set[str]:
    output = run("nm", "--dynamic", f"--{kind}-only", "--format=just-symbols", binary)
    return {symbol.split("@")[0] for symbol in output.split()}


def choose_shared_libraries(app_dir: Path, log: str):
    sources: dict[str, set[Path]] = {}
    for path in re.findall(r"^Deploying shared library (\S+)", log, re.MULTILINE):
        sources.setdefault(Path(path).name, set()).add(Path(path))
    needs = {binary: needed_libraries(binary) for binary in elf_files(app_dir / "usr")}
    for name, paths in sorted(sources.items()):
        own = sorted(path for path in paths if path.resolve().is_relative_to(XENON_SOURCE_DIR))
        system = sorted(path for path in paths if not path.resolve().is_relative_to(XENON_SOURCE_DIR))
        if not own or not system:
            continue
        used = set().union(*(dynamic_symbols(binary, "undefined") for binary, names in needs.items() if name in names))
        own_symbols, system_symbols = dynamic_symbols(own[0], "defined"), dynamic_symbols(system[0], "defined")
        if not (used & (own_symbols - system_symbols)):
            continue
        if missing := sorted(used & (system_symbols - own_symbols)):
            sys.exit(f"Neither {own[0]} nor {system[0]} has all that the AppImage uses from it: {', '.join(missing)}")
        shutil.copy2(own[0], app_dir / "usr" / "lib" / name)
        print(f"    {name} is the build's, not the system's, which lacks {len(used & (own_symbols - system_symbols))}"
              " functions the build uses")  # fmt: skip


def qt_plugins_directory(executable: Path) -> Path:
    qt_core = linked_libraries(executable).get("libQt6Core.so.6", "not found")
    if not qt_core.startswith("/"):
        sys.exit(f"{executable} doesn't link Qt, or the linker can't find it")
    library_directory = Path(qt_core).resolve().parent
    for candidate in (library_directory / "qt6" / "plugins", library_directory.parent / "plugins"):
        if (candidate / "platforms").is_dir():
            return candidate
    sys.exit(f"No Qt plugins beside {qt_core}")


def make_app_dir(app_dir: Path):
    executable = LINUX_PREFIX_DIR / "bin" / "Xenon"
    if not executable.is_file():
        sys.exit(f"No Xenon at {executable}. Build it first.")

    if app_dir.exists():
        shutil.rmtree(app_dir)
    usr = app_dir / "usr"
    (usr / "bin").mkdir(parents=True)
    shutil.copy2(executable, usr / "bin" / "Xenon")
    (usr / "libexec").mkdir()
    for helper in sorted((LINUX_PREFIX_DIR / "libexec").iterdir()):
        if helper.is_file() and os.access(helper, os.X_OK):
            shutil.copy2(helper, usr / "libexec" / helper.name)
    shutil.copytree(LINUX_PREFIX_DIR / "share" / "Lagom", usr / "share" / "Lagom")
    shutil.copytree(LINUX_ICONS_DIR, usr / "share" / "icons" / "hicolor")

    plugins_directory = qt_plugins_directory(executable)
    for pattern in LINUX_QT_PLUGINS:
        plugins = sorted(plugins_directory.glob(pattern))
        if not plugins and pattern in REQUIRED_LINUX_QT_PLUGINS:
            sys.exit(f"Qt has no {pattern} in {plugins_directory}. On Ubuntu, it's in qt6-wayland.")
        for plugin in plugins:
            destination = usr / "plugins" / plugin.relative_to(plugins_directory)
            destination.parent.mkdir(parents=True, exist_ok=True)
            shutil.copy2(plugin, destination)
    (usr / "bin" / "qt.conf").write_text("[Paths]\nPrefix = ..\nPlugins = plugins\n")

    arguments = [
        "--appdir", app_dir.resolve(),
        "--desktop-file", LINUX_DIR / "org.xenon.Xenon.desktop",
        "--icon-file", LINUX_ICONS_DIR / "256x256" / "apps" / "org.xenon.Xenon.png",
        "--custom-apprun", LINUX_DIR / "AppRun",
    ]  # fmt: skip
    for binary in elf_files(usr / "libexec") + elf_files(usr / "plugins"):
        arguments += ["--deploy-deps-only", binary.resolve()]
    log = run_appimage_tool("linuxdeploy", *arguments, NO_STRIP="1")
    (app_dir.parent / "linuxdeploy.log").write_text(log)
    print(f"    linuxdeploy copied {len(list((usr / 'lib').glob('*.so*')))} libraries into usr/lib", flush=True)
    choose_shared_libraries(app_dir, log)


def check_app_dir(app_dir: Path) -> list[str]:
    problems = []
    for binary in elf_files(app_dir):
        name = binary.relative_to(app_dir)
        dynamic = run("readelf", "--dynamic", "--wide", binary)
        for rpath in re.findall(r"\((?:RPATH|RUNPATH)\)\s+Library r(?:un)?path: \[(.*)\]", dynamic):
            if any(not entry.startswith("$ORIGIN") for entry in rpath.split(":")):
                problems.append(f"{name} has the rpath {rpath}")
        for library, location in linked_libraries(binary).items():
            if location == "not found":
                problems.append(f"{name} links {library}, which isn't there")
                continue
            resolved = Path(location).resolve()
            if resolved.is_relative_to(XENON_SOURCE_DIR) and not resolved.is_relative_to(app_dir.resolve()):
                problems.append(f"{name} loads {library} from {location}")
    return problems


def system_requirements(app_dir: Path) -> dict[str, str]:
    newest: dict[str, tuple] = {}
    for binary in elf_files(app_dir):
        references = run("objdump", "--private-headers", binary).partition("Version References:")[2]
        for name, version in re.findall(r"\b(GLIBC|GLIBCXX|CXXABI)_(\d+(?:\.\d+)*)\b", references):
            numbers = tuple(int(part) for part in version.split("."))
            if numbers > newest.get(name, ((),))[0]:
                newest[name] = (numbers, version)
    return {name: version for name, (_, version) in sorted(newest.items())}


def make_appimage(app_dir: Path, appimage: Path):
    appimage.unlink(missing_ok=True)
    zsync_path(appimage).unlink(missing_ok=True)
    run_appimage_tool(
        "appimagetool",
        "--runtime-file",
        appimage_tool("runtime"),
        "--updateinformation",
        APPIMAGE_UPDATE_INFORMATION,
        app_dir,
        appimage,
        cwd=appimage.parent,
        ARCH=LINUX_ARCHITECTURE,
    )
    if not appimage.is_file() or not zsync_path(appimage).is_file():
        sys.exit(f"appimagetool didn't write {appimage.name} and its .zsync file")
    appimage.chmod(0o755)


def package_linux_main(args):
    version = args.version or read_version()
    if not args.no_build:
        build()

    print(f"==> Packaging Xenon {version} as an AppImage", flush=True)
    DISTRIBUTION_DIR.mkdir(parents=True, exist_ok=True)
    app_dir = DISTRIBUTION_DIR / "Xenon.AppDir"
    make_app_dir(app_dir)
    if problems := check_app_dir(app_dir):
        sys.exit("The AppDir isn't right:\n    " + "\n    ".join(problems))
    requirements = system_requirements(app_dir)
    requirements_text = ", ".join(f"{name}_{number}" for name, number in requirements.items())

    print("==> Making the AppImage", flush=True)
    appimage = DISTRIBUTION_DIR / appimage_name(version)
    make_appimage(app_dir, appimage)
    (DISTRIBUTION_DIR / "linux-requirements.txt").write_text(f"{requirements_text}\n")

    print(f"    {appimage} ({appimage.stat().st_size / (1024 * 1024):.0f} MiB)")
    print(f"    {zsync_path(appimage)}")
    print(f"    It needs these of the system's glibc and libstdc++, or newer: {requirements_text}")
    print("Check it with Meta/release.py test, then publish it with the macOS release.")


def check_system_libraries(app_dir: Path) -> bool:
    missing: dict[str, Path] = {}
    for binary in elf_files(app_dir / "usr"):
        output = subprocess.run(["ldd", "-r", str(binary)], capture_output=True, text=True)
        for line in (output.stdout + output.stderr).splitlines():
            if match := re.match(r"\s*(\S+) => not found", line):
                missing.setdefault(match[1], binary.relative_to(app_dir))
            elif match := re.match(r"\s*undefined symbol: (\S+)", line):
                missing.setdefault(match[1], binary.relative_to(app_dir))
    for name, binary in sorted(missing.items()):
        print(f"    missing {name}, which {binary} uses", file=sys.stderr)
    if not missing:
        print("    all there, with every function Xenon uses")
    return not missing


def check_https(executable: Path, xenon) -> bool:
    env = dict(os.environ)
    with tempfile.TemporaryDirectory() as profile:

        def loads(url: str) -> tuple[bool, str]:
            _, output = xenon.run_headless(executable, env, ["--headless=text", "--profile-path", profile, url])
            return HTTPS_TEST_TEXT in output, output

        for _ in range(3):
            loaded, output = loads(HTTPS_TEST_URL)
            if loaded:
                print(f'    loaded {HTTPS_TEST_URL}, with "{HTTPS_TEST_TEXT}"')
                return True
            time.sleep(10)
        print(output[-4000:], file=sys.stderr)

        plain_url = HTTPS_TEST_URL.replace("https://", "http://")
        verb = "loads" if loads(plain_url)[0] else "doesn't load"
        print(f"    Xenon {verb} {plain_url}", file=sys.stderr)
        try:
            urllib.request.urlopen(HTTPS_TEST_URL, timeout=30).read()
            print(f"    Python loads {HTTPS_TEST_URL}", file=sys.stderr)
        except OSError as error:
            print(f"    Python doesn't load {HTTPS_TEST_URL} either: {error}", file=sys.stderr)
    return False


def test_linux_main(args):
    xenon = load_module("xenon", XENON_SOURCE_DIR / "Meta" / "xenon.py")
    failures = []
    if args.installed:
        executable = args.installed.absolute()
        app_dir = executable.resolve().parent
        if not (app_dir / "usr" / "bin" / "Xenon").is_file():
            sys.exit(f"{executable} doesn't lead to an AppDir's AppRun")
        print(f"==> Testing {executable}, which runs {app_dir}", flush=True)
        print("==> the system has every library Xenon leaves to it", flush=True)
        if not check_system_libraries(app_dir):
            failures.append("system libraries")
    else:
        executable = (args.appimage or DISTRIBUTION_DIR / appimage_name(read_version())).resolve()
        if not executable.is_file():
            sys.exit(f"No {executable}. Package it first.")
        executable.chmod(0o755)
        print(f"==> Testing {executable.name}", flush=True)
        print("==> the system has every library the AppImage leaves to it", flush=True)
        with tempfile.TemporaryDirectory() as directory:
            run(executable, "--appimage-extract", cwd=directory)
            if not check_system_libraries(Path(directory) / "squashfs-root"):
                failures.append("system libraries")

    failures += xenon.check_app(xenon.Platform(), executable, DISTRIBUTION_DIR / "smoke")

    if xenon.SKIPPED_CHECKS not in failures:
        print("==> a page over HTTPS, with the system's certificates", flush=True)
        if not check_https(executable, xenon):
            failures.append("HTTPS")

    if failures:
        sys.exit(f"{len(failures)} failed: {', '.join(failures)}")
    print("All passed.")


def publish_main(args):
    appcast = DISTRIBUTION_DIR / "appcast.xml"
    if not appcast.is_file():
        sys.exit("Nothing to publish. Run Meta/release.py package first, on a Mac.")
    enclosure = ElementTree.parse(appcast).find("channel/item/enclosure")
    archive_url = enclosure.get("url", "") if enclosure is not None else ""
    archive = DISTRIBUTION_DIR / archive_url.rsplit("/", 1)[-1]
    if not archive_url or not archive.is_file():
        sys.exit("Build/distribution has no archive that its appcast names. Package it again.")
    with tarfile.open(archive) as tar:
        info = plistlib.load(tar.extractfile(f"{APP_NAME}/Contents/Info.plist"))
    version = info["CFBundleShortVersionString"]
    installer = DISTRIBUTION_DIR / installer_name(version)
    if info["SUFeedURL"] != FEED_URL or archive.name != archive_name(version):
        sys.exit(f"Build/distribution isn't a release for {RELEASES_REPOSITORY}. Package it again.")
    if not installer.is_file():
        sys.exit(f"Build/distribution has no {installer.name}. Package it again.")
    appimage = DISTRIBUTION_DIR / appimage_name(version)
    linux_files = [path for path in (appimage, zsync_path(appimage)) if appimage.is_file() and path.is_file()]
    files = [installer, archive, *linux_files]
    if subprocess.run(["gh", "auth", "status"], capture_output=True).returncode != 0:
        sys.exit("gh isn't logged in to GitHub: gh auth login")

    tag = f"v{version}"
    commit = run("git", "-C", XENON_SOURCE_DIR, "rev-parse", "HEAD").strip()
    if run("git", "-C", XENON_SOURCE_DIR, "status", "--porcelain", "--untracked-files=no").strip():
        sys.exit("Xenon has uncommitted changes, so the release wouldn't match any commit.")
    xenon_url = f"https://github.com/{XENON_REPOSITORY}.git"
    run(*GIT_WITH_GITHUB_CREDENTIALS, "-C", XENON_SOURCE_DIR, "fetch", "--quiet", xenon_url, "main")
    on_main = subprocess.run(["git", "-C", XENON_SOURCE_DIR, "merge-base", "--is-ancestor", commit, "FETCH_HEAD"])
    if on_main.returncode != 0:
        sys.exit(f"{commit[:10]} isn't on {XENON_REPOSITORY}'s main. Release from a commit that is.")

    if not args.yes:
        print(f"Xenon {version} from {commit[:10]}: {', '.join(path.name for path in files)}.")
        app = DISTRIBUTION_DIR / APP_NAME
        if sys.platform == "darwin" and app.is_dir():
            signing = subprocess.run(["codesign", "-dvv", app], capture_output=True, text=True).stderr
            authority = re.search(r"^Authority=(.+)$", signing, re.MULTILINE)
            print(f"The app is signed {f'as {authority.group(1)}' if authority else 'ad hoc'}.")
        if not linux_files:
            print(f"There's no {appimage.name} in Build/distribution, so the release has nothing for Linux.")
        answer = input(f"Publish it to {RELEASES_REPOSITORY}, and tag it {tag}? [y/N] ")
        if answer.strip().lower() != "y":
            sys.exit("Not published.")

    notes_file = DISTRIBUTION_DIR / "notes.txt"
    notes = notes_file.read_text().strip() if notes_file.is_file() else f"Xenon {version}"
    notes += f"\n\nBuilt from https://github.com/{XENON_REPOSITORY}/commit/{commit}."

    print(f"==> Creating the {tag} release, with {', '.join(path.name for path in files)}", flush=True)
    run(
        "gh", "release", "create", tag, *files, "--repo", RELEASES_REPOSITORY, "--target", "main",
        "--title", f"Xenon {version}", "--notes", notes,
    )  # fmt: skip

    print("==> Publishing the appcast", flush=True)
    with tempfile.TemporaryDirectory() as checkout:
        run(*GIT_WITH_GITHUB_CREDENTIALS, "clone", "--depth", "1", f"{RELEASES_URL}.git", checkout)
        shutil.copy2(appcast, Path(checkout) / "appcast.xml")
        run("git", "-C", checkout, "add", "appcast.xml")
        run("git", "-C", checkout, "commit", "-m", f"Xenon {version}")
        run(*GIT_WITH_GITHUB_CREDENTIALS, "-C", checkout, "push", "origin", "HEAD:main")

    print(f"==> Tagging {commit[:10]} as {tag}", flush=True)
    run("git", "-C", XENON_SOURCE_DIR, "tag", tag, commit)
    run(*GIT_WITH_GITHUB_CREDENTIALS, "-C", XENON_SOURCE_DIR, "push", xenon_url, f"refs/tags/{tag}")

    print(f"Published {release_link(version)}")


class FeedServer:
    class Handler(http.server.SimpleHTTPRequestHandler):
        def do_GET(self):
            self.server.requested.append(self.path)
            download = re.fullmatch(r"/releases/download/[^/]+/([^/]+)", self.path)
            if download:
                self.send_response(302)
                self.send_header("Location", f"http://localhost:{self.server.server_address[1]}/assets/{download[1]}")
                self.end_headers()
                return
            super().do_GET()

        def log_message(self, *args):
            pass

    def __init__(self, directory: Path):
        handler = functools.partial(FeedServer.Handler, directory=str(directory))
        self.server = http.server.ThreadingHTTPServer(("127.0.0.1", 0), handler)
        self.server.requested = []
        self.url = f"http://127.0.0.1:{self.server.server_address[1]}/"
        threading.Thread(target=self.server.serve_forever, daemon=True).start()

    def downloaded(self, name: str) -> bool:
        return f"/assets/{name}" in self.server.requested

    def close(self):
        self.server.shutdown()


class RunningXenon:
    def __init__(self, app: Path, profile: Path, env: dict | None = None):
        self.process = subprocess.Popen(
            [app / "Contents" / "MacOS" / "Xenon", "--profile-path", profile, "about:blank"],
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
            text=True,
            env=env,
        )
        self.lines = queue.Queue()
        self.output = []
        threading.Thread(target=self.read, daemon=True).start()

    def read(self):
        for line in self.process.stdout:
            self.lines.put(line)
        self.lines.put(None)

    def wait_for(self, *patterns: str) -> str | None:
        deadline = time.monotonic() + TEST_TIMEOUT_SECONDS
        while time.monotonic() < deadline:
            try:
                line = self.lines.get(timeout=1)
            except queue.Empty:
                continue
            if line is None:
                return None
            self.output.append(line)
            if any(re.search(pattern, line) for pattern in patterns):
                return line
        return None

    def quit(self):
        self.process.terminate()
        try:
            self.process.wait(timeout=30)
        except subprocess.TimeoutExpired:
            self.process.kill()
            self.process.wait()


def bundle_version(app: Path) -> str | None:
    try:
        return plistlib.loads((app / "Contents" / "Info.plist").read_bytes())["CFBundleVersion"]
    except (OSError, KeyError, plistlib.InvalidFileException):
        return None


def test_main(args):
    check_signing_identity(args.sign)
    if not args.no_build:
        build()

    automatic = subprocess.run(
        ["defaults", "read", "com.xenon.Xenon", "updates.automatic"], capture_output=True, text=True
    ).stdout.strip()
    if automatic == "0":
        sys.exit(
            "Turn on Update Xenon automatically (Settings, Updates) first: the test needs Xenon to check by itself."
        )

    failures = []

    def expect(step: str, ok: bool, details: str = "", said: str | None = None):
        print(f"    {'ok' if ok else 'FAILED'}: {step}", flush=True)
        if ok and said:
            print(f"        {said.strip()}", flush=True)
        if not ok:
            failures.append(step)
            if details:
                print(details[-6000:], file=sys.stderr)

    with tempfile.TemporaryDirectory(prefix="xenon-update-test-") as temporary:
        root = Path(temporary).resolve()
        site = root / "site"
        (site / "assets").mkdir(parents=True)
        server = FeedServer(site)
        key = UpdateKey.generate(root / "update-key")
        other_key = UpdateKey.generate(root / "other-key")
        public_key = key.public_key()

        try:
            print("==> Packaging the test's releases", flush=True)
            template = root / "template" / APP_NAME
            make_bundle(template)

            def package(version: str) -> Path:
                app = root / "releases" / version / APP_NAME
                app.parent.mkdir(parents=True)
                run("cp", "-cR", template, app)
                configure_bundle(app, version, f"{server.url}appcast.xml", public_key, TEST_BUNDLE_IDENTIFIER)
                sign_bundle(app, args.sign)
                archive = root / "releases" / version / f"Xenon-{version}.tar.gz"
                make_archive(app, archive)
                return archive

            def publish(version: str, archive: Path, signature: str):
                shutil.copy2(archive, site / "assets" / archive.name)
                app = root / "releases" / version / APP_NAME
                archive_url = f"{server.url}releases/download/v{version}/{archive.name}"
                write_appcast(
                    site / "appcast.xml", app, site / "assets" / archive.name, archive_url, signature, server.url, None
                )
                key.sign(site / "appcast.xml")

            archives = {version: package(version) for version in TEST_VERSIONS}
            first, second, third = TEST_VERSIONS

            print(f"==> {first}'s installer", flush=True)
            installer = root / "releases" / first / installer_name(first)
            make_installer(root / "releases" / first / APP_NAME, installer, args.sign)
            problems = check_installer(installer)
            expect("the installer has the app, a link to Applications, and its window's layout", not problems,
                   "; ".join(problems))  # fmt: skip
            applications = root / "Applications"
            applications.mkdir()
            with MountedImage(installer) as volume:
                run("ditto", volume / APP_NAME, applications / APP_NAME)
            installed = applications / APP_NAME
            profile = root / "profile"

            print(f"==> {first} finds {second}, offers it in the toolbar, and installs it when it quits", flush=True)
            publish(second, archives[second], key.sign(archives[second]))
            xenon = RunningXenon(installed, profile, {**os.environ, "XENON_TEST_UPDATE_READY": TEST_STAND_IN_VERSION})
            ready = xenon.wait_for(rf"Xenon {re.escape(second)} is ready", r"Unable to update Xenon")
            downloaded = ready is not None and "is ready" in ready
            offered = xenon.wait_for(r"The toolbar offers to relaunch to update to Xenon") if downloaded else None
            xenon.quit()
            expect(f"{second} downloaded and checked", downloaded, "".join(xenon.output), ready)
            expect(
                f"the toolbar offers {second}, and not XENON_TEST_UPDATE_READY's stand-in",
                offered is not None
                and f"to Xenon {second}" in offered
                and TEST_STAND_IN_VERSION not in "".join(xenon.output),
                "".join(xenon.output),
                offered,
            )

            deadline = time.monotonic() + TEST_TIMEOUT_SECONDS
            while bundle_version(installed) != second and time.monotonic() < deadline:
                time.sleep(1)
            expect(f"{second} downloaded through a redirect, as from GitHub", server.downloaded(archives[second].name))
            expect(f"{second} installed in place of {first}", bundle_version(installed) == second)
            signature_check = subprocess.run(
                ["codesign", "--verify", "--deep", "--strict", installed], capture_output=True, text=True
            )
            expect("the installed app's signature checks out", signature_check.returncode == 0, signature_check.stderr)

            tampered = root / "tampered" / archives[third].name
            tampered.parent.mkdir()
            shutil.copy2(archives[third], tampered)
            with tampered.open("r+b") as file:
                file.seek(tampered.stat().st_size // 2)
                byte = file.read(1)
                file.seek(-1, os.SEEK_CUR)
                file.write(bytes([byte[0] ^ 0xFF]))

            for case, archive, signature in (
                ("an archive changed after it was signed", tampered, key.sign(archives[third])),
                ("an archive signed with another key", archives[third], other_key.sign(archives[third])),
            ):
                print(f"==> {second} turns down {case}", flush=True)
                publish(third, archive, signature)
                xenon = RunningXenon(installed, profile)
                outcome = xenon.wait_for(rf"Xenon {re.escape(third)} is ready", r"Unable to update Xenon")
                xenon.quit()
                refused = outcome is not None and "Unable to update" in outcome
                expect(f"{third} refused", refused, "".join(xenon.output), outcome)
                time.sleep(5)
                expect(f"{second} still installed", bundle_version(installed) == second)
        finally:
            server.close()
            subprocess.run(["defaults", "delete", TEST_BUNDLE_IDENTIFIER], capture_output=True)
            library = Path.home() / "Library"
            (library / "Preferences" / f"{TEST_BUNDLE_IDENTIFIER}.plist").unlink(missing_ok=True)
            for directory in ("Caches", "HTTPStorages"):
                shutil.rmtree(library / directory / TEST_BUNDLE_IDENTIFIER, ignore_errors=True)
            shutil.rmtree(
                library / "Saved Application State" / f"{TEST_BUNDLE_IDENTIFIER}.savedState", ignore_errors=True
            )

    if failures:
        sys.exit(f"{len(failures)} failed: {', '.join(failures)}")
    print("All passed.")


if __name__ == "__main__":
    main()
