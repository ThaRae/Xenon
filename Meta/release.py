#!/usr/bin/env python3

# Packages Xenon for release on macOS, publishes the release, and tests that one release updates itself to the next.
#
#   Meta/release.py package    build Xenon, then write Build/distribution: a Xenon.app that runs on any Mac, signed;
#                              Xenon-<version>.dmg, the disk image people install it from; Xenon-<version>.tar.xz,
#                              signed with Xenon's update key; and appcast.xml, which lists it
#   Meta/release.py publish    publish what package wrote, as a release of ThaRae/xenon-releases on GitHub
#   Meta/release.py test       check that a release finds, checks and installs the next one from a local server, and
#                              turns down ones that were tampered with
#
# The version is VERSION.txt's. Xenon updates itself with Sparkle (UI/Updater.h), which finds the appcast's URL and the
# update key's public half in the Info.plist this writes. Documentation/Updates.md describes the whole process, and how
# to set up the signing certificate and the update key.

from __future__ import annotations

import argparse
import base64
import email.utils
import functools
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
import tempfile
import threading
import time

from pathlib import Path
from xml.sax.saxutils import escape
from xml.sax.saxutils import quoteattr

XENON_SOURCE_DIR = Path(__file__).resolve().parent.parent
LADYBIRD_SOURCE_DIR = XENON_SOURCE_DIR / "ladybird"
BUILD_DIR = XENON_SOURCE_DIR / "Build" / "release"
DISTRIBUTION_DIR = XENON_SOURCE_DIR / "Build" / "distribution"

# Releases are GitHub releases of their own repository, whose main holds the appcast. Xenon reads the appcast from
# raw.githubusercontent.com, and downloads each archive from github.com, which redirects to GitHub's storage.
XENON_REPOSITORY = "ThaRae/Xenon"
RELEASES_REPOSITORY = "ThaRae/xenon-releases"
RELEASES_URL = f"https://github.com/{RELEASES_REPOSITORY}"
FEED_URL = f"https://raw.githubusercontent.com/{RELEASES_REPOSITORY}/main/appcast.xml"
# Pushes to GitHub go over HTTPS with gh's credentials, whatever Xenon's origin is.
GIT_WITH_GITHUB_CREDENTIALS = ("git", "-c", "credential.helper=", "-c", "credential.helper=!gh auth git-credential")

# The certificate releases are signed with, and the login keychain account that holds the key their updates are signed
# with, as Sparkle's generate_keys made it (Documentation/Updates.md).
SIGNING_IDENTITY = "Xenon Release"
UPDATE_KEY_ACCOUNT = "xenon"

APP_NAME = "Xenon.app"
BUNDLE_IDENTIFIER = "org.xenon.Xenon"

# The installer: a disk image whose window shows Xenon.app beside a link to /Applications, over a background that says to
# drag one onto the other (UI/Icons/macos/installer-background.svg, which Meta/generate-app-icon.py renders, at 1x and
# 2x). dmgbuild writes the window's layout itself, without scripting Finder, so it works the same on GitHub's runners.
DMGBUILD_VERSION = "1.6.7"
INSTALLER_BACKGROUND = XENON_SOURCE_DIR / "UI" / "Icons" / "macos" / "installer-background.png"
INSTALLER_WINDOW_SIZE = (640, 400)
INSTALLER_ICON_SIZE = 128
INSTALLER_ICON_LOCATIONS = {APP_NAME: (170, 190), "Applications": (470, 190)}

# The Qt plugins Xenon loads: the platform and the style at startup, and the image formats when Qt reads one.
QT_PLUGINS = (
    "platforms/libqcocoa.dylib",
    "styles/libqmacstyle.dylib",
    "imageformats/libqgif.dylib",
    "imageformats/libqico.dylib",
    "imageformats/libqjpeg.dylib",
)

# The test's releases have a bundle identifier of their own, so that Sparkle keeps what it remembers about them (in the
# preferences of that identifier) apart from Xenon's.
TEST_BUNDLE_IDENTIFIER = "org.xenon.Xenon.UpdateTest"
TEST_VERSIONS = ("0.0.1", "0.0.2", "0.0.3")
TEST_TIMEOUT_SECONDS = 180


def load_ladybird_bundler():
    """Ladybird's Meta/package-macos-test-bundle.py, which makes its own app run on other Macs. Xenon's bundle needs the
    same: Qt's plugins copied in, the libraries it links copied in, and rpaths relative to the bundle."""
    path = LADYBIRD_SOURCE_DIR / "Meta" / "package-macos-test-bundle.py"
    spec = importlib.util.spec_from_file_location("package_macos_test_bundle", path)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


bundler = load_ladybird_bundler()


def main():
    parser = argparse.ArgumentParser(description="Package, publish and test Xenon's releases on macOS")
    subparsers = parser.add_subparsers(dest="command", required=True)

    package_parser = subparsers.add_parser("package", help="build and package a release into Build/distribution")
    package_parser.add_argument("--no-build", action="store_true", help="package the build as it is")
    package_parser.add_argument("--version", help="the version to release as, rather than VERSION.txt's")
    package_parser.add_argument("--sign", default=SIGNING_IDENTITY, help='the signing identity, or "-" for ad hoc')
    package_parser.add_argument(
        "--update-key", type=Path, help="a file with the update key, rather than the keychain's"
    )
    package_parser.add_argument("--feed-url", default=FEED_URL, help="where the release looks for updates")
    package_parser.add_argument("--notes", type=Path, help="a file with the release notes, in plain text")

    publish_parser = subparsers.add_parser("publish", help="publish Build/distribution to ThaRae/xenon-releases")
    publish_parser.add_argument("--yes", action="store_true", help="don't ask before publishing")

    test_parser = subparsers.add_parser("test", help="test that a release updates itself to the next")
    test_parser.add_argument("--no-build", action="store_true", help="test the build as it is")
    test_parser.add_argument("--sign", default="-", help='the signing identity, or "-" for ad hoc (the default)')

    args = parser.parse_args()
    if sys.platform != "darwin":
        sys.exit("Xenon's releases are packaged on macOS")

    if args.command == "package":
        package_main(args)
    elif args.command == "publish":
        publish_main(args)
    elif args.command == "test":
        test_main(args)


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
    """One of the tools in the Sparkle release that the build fetched (UI/Xenon.cmake)."""
    tools = sorted((BUILD_DIR / "Xenon").glob(f"Sparkle-*/bin/{name}"))
    if not tools:
        sys.exit(f"No Sparkle {name} in {BUILD_DIR / 'Xenon'}. Build Xenon first.")
    return tools[-1]


def find_openssl() -> str:
    """An OpenSSL that does Ed25519, which macOS's LibreSSL doesn't."""
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


# The DER prefix of a PKCS #8 Ed25519 private key; the 32-byte seed follows it.
ED25519_PKCS8_PREFIX = bytes.fromhex("302e020100300506032b657004220420")


class UpdateKey:
    """The Ed25519 key updates are signed with: in the login keychain, where Sparkle's generate_keys puts it, or in a file
    of its base64 seed, as generate_keys -x exports it."""

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
        """Signs an archive, and returns the signature, or signs an appcast in place."""
        return run(sparkle_tool("sign_update"), *self.options(), "-p", path).strip()

    @staticmethod
    def generate(path: Path):
        """A new key, in a file, for tests."""
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
    """Copies the build's Xenon.app to `app`, and makes it run on any Mac. The build's bundle links the libraries in the
    build directory and vcpkg's, by absolute rpaths, and finds Qt's plugins in vcpkg's tree. This copies all of them in,
    makes every rpath relative, and strips the binaries. The bundle needs signing after."""
    built_app = BUILD_DIR / "bin" / APP_NAME
    built_executable = built_app / "Contents" / "MacOS" / "Xenon"
    if not built_executable.is_file():
        sys.exit(f"No Xenon at {built_app}. Build it first.")

    # The build's executables find their libraries through these.
    search_directories = [Path(entry) for entry in bundler.rpath_entries(built_executable) if entry.startswith("/")]
    search_directories = [directory for directory in search_directories if directory.is_dir()]

    if app.exists():
        shutil.rmtree(app)
    app.parent.mkdir(parents=True, exist_ok=True)
    run("ditto", built_app, app)

    contents = app / "Contents"
    # The build links Contents/lib to its library directory. The libraries the bundle needs are copied in instead.
    (contents / "lib").unlink()
    shutil.rmtree(contents / "_CodeSignature", ignore_errors=True)
    # Sparkle's XPC services are for sandboxed apps, which Xenon isn't.
    framework = contents / "Frameworks" / "Sparkle.framework"
    shutil.rmtree(framework / "Versions" / "B" / "XPCServices")
    (framework / "XPCServices").unlink()

    bundler.rpath_entries.cache_clear()
    deploy_qt_plugins(app, bundler.find_qt_plugins_directory(search_directories))
    bundler.fix_rpaths(app)
    copied = bundler.copy_linked_libraries(app, search_directories)
    bundler.fix_rpaths(app)

    # Local symbols only: crash reports still name every exported function.
    for binary in bundler.mach_o_files(contents / "MacOS", contents / "lib", contents / "PlugIns"):
        run("strip", "-x", binary)
    bundler.rpath_entries.cache_clear()
    print(f"    copied {len(copied)} libraries into the bundle, and stripped it")


def configure_bundle(app: Path, version: str, feed_url: str, public_key: str, bundle_identifier: str):
    """Gives the bundle its version, and tells Sparkle where its updates come from and what they're signed with."""
    info_path = app / "Contents" / "Info.plist"
    info = plistlib.loads(info_path.read_bytes())
    info.update(
        {
            "CFBundleIdentifier": bundle_identifier,
            "CFBundleShortVersionString": version,
            "CFBundleVersion": version,
            "SUFeedURL": feed_url,
            "SUPublicEDKey": public_key,
            # Sparkle checks an update's signature before it unpacks anything, and turns down an appcast that isn't
            # signed with the same key.
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
    """A helper's release entitlements (ladybird_sign_helper_process). They leave library validation on, which only works
    when everything in the bundle carries one team identifier. A certificate of Xenon's own or an ad hoc signature has
    none, so with those, library validation stays off, as in development builds."""
    release = BUILD_DIR / "Services" / executable.name / f"{executable.name}.release.entitlements"
    entitlements = plistlib.loads(release.read_bytes()) if release.is_file() else {}
    if not team_signed:
        entitlements["com.apple.security.cs.disable-library-validation"] = True
    path = directory / f"{executable.name}.entitlements"
    path.write_bytes(plistlib.dumps(entitlements))
    return path


def sign_bundle(app: Path, identity: str):
    """Signs everything in the bundle, from the inside out: Sparkle's helpers and framework, the libraries and plugins,
    the engine's helper processes with their entitlements, and the app."""
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
    """The Python of a virtual environment of dmgbuild's own, under Build/tools."""
    environment = XENON_SOURCE_DIR / "Build" / "tools" / f"dmgbuild-{DMGBUILD_VERSION}"
    python = environment / "bin" / "python"
    if not (environment / "bin" / "dmgbuild").is_file():
        print(f"Installing dmgbuild {DMGBUILD_VERSION} into {environment.relative_to(XENON_SOURCE_DIR)}", flush=True)
        shutil.rmtree(environment, ignore_errors=True)
        run(sys.executable, "-m", "venv", environment)
        run(python, "-m", "pip", "install", "--quiet", "--disable-pip-version-check", f"dmgbuild=={DMGBUILD_VERSION}")
    return python


def make_installer(app: Path, dmg: Path, identity: str):
    """The disk image people install Xenon from: the app, a link to /Applications, the window laid out to drag one onto
    the other, and Xenon's icon on the volume. Signed with the app's identity, unless that's ad hoc."""
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
    """A disk image attached read-only, out of sight of Finder, for as long as the with block lasts."""

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
    """What's wrong with an installer disk image, if anything."""
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
        # dmgbuild combines the background's 1x and 2x into .background.tiff.
        if not (volume / ".DS_Store").is_file() or not (volume / ".background.tiff").is_file():
            problems.append("its window has no layout or background")
        if not (volume / ".VolumeIcon.icns").is_file():
            problems.append("its volume has no icon")
    return problems


def make_archive(app: Path, archive: Path):
    """Archives the bundle as Sparkle expects: symlinks kept, extended attributes left out."""
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
    """An appcast that lists one release: the newest, which is all Sparkle needs to update any older one."""
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


def publish_main(args):
    appcast = DISTRIBUTION_DIR / "appcast.xml"
    app = DISTRIBUTION_DIR / APP_NAME
    if not appcast.is_file() or not app.is_dir():
        sys.exit("Nothing to publish. Run Meta/release.py package first.")
    info = plistlib.loads((app / "Contents" / "Info.plist").read_bytes())
    version = info["CFBundleShortVersionString"]
    archive = DISTRIBUTION_DIR / archive_name(version)
    installer = DISTRIBUTION_DIR / installer_name(version)
    if info["SUFeedURL"] != FEED_URL or not archive.is_file() or archive.name not in appcast.read_text():
        sys.exit(f"Build/distribution isn't a release for {RELEASES_REPOSITORY}. Package it again.")
    if not installer.is_file():
        sys.exit(f"Build/distribution has no {installer.name}. Package it again.")
    if subprocess.run(["gh", "auth", "status"], capture_output=True).returncode != 0:
        sys.exit("gh isn't logged in to GitHub: gh auth login")

    tag = f"v{version}"
    commit = run("git", "-C", XENON_SOURCE_DIR, "rev-parse", "HEAD").strip()
    if run("git", "-C", XENON_SOURCE_DIR, "status", "--porcelain", "--untracked-files=no").strip():
        sys.exit("Xenon has uncommitted changes, so the release wouldn't match any commit.")
    # A release is made from a commit everyone can see, on main.
    xenon_url = f"https://github.com/{XENON_REPOSITORY}.git"
    run(*GIT_WITH_GITHUB_CREDENTIALS, "-C", XENON_SOURCE_DIR, "fetch", "--quiet", xenon_url, "main")
    on_main = subprocess.run(["git", "-C", XENON_SOURCE_DIR, "merge-base", "--is-ancestor", commit, "FETCH_HEAD"])
    if on_main.returncode != 0:
        sys.exit(f"{commit[:10]} isn't on {XENON_REPOSITORY}'s main. Release from a commit that is.")

    signing = subprocess.run(["codesign", "-dvv", app], capture_output=True, text=True).stderr
    authority = re.search(r"^Authority=(.+)$", signing, re.MULTILINE)
    signed_as = authority.group(1) if authority else "ad hoc"
    if not args.yes:
        print(f"Xenon {version} from {commit[:10]}, signed {'' if signed_as == 'ad hoc' else 'as '}{signed_as}.")
        answer = input(f"Publish it to {RELEASES_REPOSITORY}, and tag it {tag}? [y/N] ")
        if answer.strip().lower() != "y":
            sys.exit("Not published.")

    notes_file = DISTRIBUTION_DIR / "notes.txt"
    notes = notes_file.read_text().strip() if notes_file.is_file() else f"Xenon {version}"
    notes += f"\n\nBuilt from https://github.com/{XENON_REPOSITORY}/commit/{commit}."

    print(f"==> Creating the {tag} release, with {installer.name} and {archive.name}", flush=True)
    run(
        "gh", "release", "create", tag, installer, archive, "--repo", RELEASES_REPOSITORY, "--target", "main",
        "--title", f"Xenon {version}", "--notes", notes,
    )  # fmt: skip

    # The appcast goes up only once the archive it names is there.
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
    """Serves a directory over HTTP on 127.0.0.1, as the test's ThaRae/xenon-releases: the appcast as
    raw.githubusercontent.com does, and archives as GitHub's release downloads do, which redirect from github.com to
    another host (release-assets.githubusercontent.com). Here, that's localhost."""

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
        """Whether an archive was fetched through the redirect."""
        return f"/assets/{name}" in self.server.requested

    def close(self):
        self.server.shutdown()


class RunningXenon:
    """A test release, run with a profile of the test's own, whose output the test reads line by line."""

    def __init__(self, app: Path, profile: Path):
        self.process = subprocess.Popen(
            [app / "Contents" / "MacOS" / "Xenon", "--profile-path", profile, "about:blank"],
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
            text=True,
        )
        self.lines = queue.Queue()
        self.output = []
        threading.Thread(target=self.read, daemon=True).start()

    def read(self):
        for line in self.process.stdout:
            self.lines.put(line)
        self.lines.put(None)

    def wait_for(self, *patterns: str) -> str | None:
        """The first line that matches any of the patterns, or None if Xenon exits or the time runs out first."""
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

    # Xenon's own settings aren't the profile's, so the test runs with the user's (UI/XenonSettings.cpp).
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

            # Installed as a user would: from the installer, copied into a folder of applications.
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

            print(f"==> {first} finds {second}, and installs it when it quits", flush=True)
            publish(second, archives[second], key.sign(archives[second]))
            xenon = RunningXenon(installed, profile)
            ready = xenon.wait_for(rf"Xenon {re.escape(second)} is ready", r"Unable to update Xenon")
            xenon.quit()
            expect(
                f"{second} downloaded and checked",
                ready is not None and "is ready" in ready,
                "".join(xenon.output),
                ready,
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
            # What Sparkle and the system kept about the test's releases.
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
