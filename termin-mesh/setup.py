#!/usr/bin/env python3

from setuptools import setup, Extension
from setuptools.command.build_ext import build_ext
from setuptools.command.build import build as _build
from pathlib import Path
import shutil
import subprocess
import sys
import os


def _split_prefix_path(raw):
    if not raw:
        return []
    normalized = raw.replace(";", os.pathsep)
    return [p for p in normalized.split(os.pathsep) if p]


def _get_sdk_prefix():
    override = os.environ.get("TERMIN_SDK_PREFIX")
    if override:
        return Path(override)
    if sys.platform == "win32":
        base = os.environ.get("LOCALAPPDATA", os.path.expanduser("~/AppData/Local"))
        return Path(base) / "termin-sdk"
    return Path("/opt/termin")


def _copytree(src, dst):
    if dst.exists():
        shutil.rmtree(dst)
    follow = sys.platform == "win32"
    shutil.copytree(src, dst, symlinks=not follow)


def _copy_upstream_libs(src_lib_dir, dst_lib_dir, dst_pkg_dir, name_prefix):
    if not src_lib_dir.exists():
        return

    dst_lib_dir.mkdir(parents=True, exist_ok=True)

    if sys.platform == "win32":
        for f in src_lib_dir.glob(f"{name_prefix}*.dll"):
            shutil.copy2(f, dst_pkg_dir / f.name)
        for f in src_lib_dir.glob(f"{name_prefix}*.lib"):
            shutil.copy2(f, dst_lib_dir / f.name)
    else:
        for f in sorted(src_lib_dir.glob(f"{name_prefix}*.so*")):
            dst = dst_lib_dir / f.name
            if f.is_file() and not f.is_symlink():
                shutil.copy2(f, dst)
        for f in sorted(src_lib_dir.glob(f"{name_prefix}*.so*")):
            dst = dst_lib_dir / f.name
            if f.is_symlink():
                if dst.exists() or dst.is_symlink():
                    dst.unlink()
                dst.symlink_to(os.readlink(f))


class CMakeBuild(_build):
    def run(self):
        self.run_command("build_ext")
        _build.run(self)


class CMakeBuildExt(build_ext):
    def build_extension(self, ext):
        source_dir = Path(directory)
        build_temp = Path(self.build_temp)
        build_temp.mkdir(parents=True, exist_ok=True)

        cfg = "Debug" if self.debug else "Release"

        staging_dir = (build_temp / "install").resolve()
        staging_dir.mkdir(parents=True, exist_ok=True)

        try:
            import tcbase
            tcbase_prefix = str(Path(tcbase.__file__).parent)
        except ImportError:
            tcbase_prefix = None

        sdk = _get_sdk_prefix()

        cmake_args = [
            str(source_dir),
            f"-DCMAKE_BUILD_TYPE={cfg}",
            "-DTERMIN_BUILD_PYTHON=ON",
            f"-DPython_EXECUTABLE={sys.executable}",
            f"-DCMAKE_INSTALL_PREFIX={staging_dir}",
            "-DCMAKE_INSTALL_LIBDIR=lib",
        ]

        prefix_paths = _split_prefix_path(os.environ.get("CMAKE_PREFIX_PATH"))
        if sdk and sdk.exists():
            prefix_paths.append(str(sdk))
        if tcbase_prefix:
            prefix_paths.append(tcbase_prefix)
        if prefix_paths:
            cmake_args.append(f"-DCMAKE_PREFIX_PATH={';'.join(prefix_paths)}")

        subprocess.check_call(["cmake", *cmake_args], cwd=build_temp)
        subprocess.check_call(
            ["cmake", "--build", ".", "--config", cfg, "--parallel"],
            cwd=build_temp,
        )
        subprocess.check_call(
            ["cmake", "--install", ".", "--config", cfg],
            cwd=build_temp,
        )

        if sys.platform == "win32" and sdk:
            sdk.mkdir(parents=True, exist_ok=True)
            subprocess.check_call(
                ["cmake", "--install", ".", "--config", cfg, "--prefix", str(sdk)],
                cwd=build_temp,
            )

        patterns = ["_tmesh_native.*.so", "_tmesh_native.*.pyd", "_tmesh_native.pyd"]
        built_files = []
        for pat in patterns:
            built_files.extend((build_temp / "python").glob(pat))
            built_files.extend((build_temp / "python" / cfg).glob(pat))
        if not built_files:
            raise RuntimeError("CMake build did not produce _tmesh_native module")

        built_module = built_files[0]

        ext_path = Path(self.get_ext_fullpath(ext.name))
        ext_path.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(built_module, ext_path)

        ext_pkg_dir = ext_path.parent
        if (staging_dir / "include").exists():
            _copytree(staging_dir / "include", ext_pkg_dir / "include")
        if (staging_dir / "lib").exists():
            _copytree(staging_dir / "lib", ext_pkg_dir / "lib")

        if tcbase_prefix:
            tcbase_lib_dir = Path(tcbase_prefix) / "lib"
            _copy_upstream_libs(tcbase_lib_dir, ext_pkg_dir / "lib", ext_pkg_dir, "libtermin_base")
            _copy_upstream_libs(tcbase_lib_dir, ext_pkg_dir / "lib", ext_pkg_dir, "termin_base")

        if sys.platform == "win32":
            for dll in (staging_dir / "lib").glob("*.dll"):
                shutil.copy2(dll, ext_pkg_dir / dll.name)
            for dll in (staging_dir / "bin").glob("*.dll"):
                shutil.copy2(dll, ext_pkg_dir / dll.name)

        tmesh_pkg_dir = source_dir / "python" / "tmesh"
        tmesh_pkg_dir.mkdir(parents=True, exist_ok=True)
        shutil.copy2(built_module, tmesh_pkg_dir / built_module.name)
        if (staging_dir / "include").exists():
            _copytree(staging_dir / "include", tmesh_pkg_dir / "include")
        if (staging_dir / "lib").exists():
            _copytree(staging_dir / "lib", tmesh_pkg_dir / "lib")

        if tcbase_prefix:
            tcbase_lib_dir = Path(tcbase_prefix) / "lib"
            _copy_upstream_libs(tcbase_lib_dir, tmesh_pkg_dir / "lib", tmesh_pkg_dir, "libtermin_base")
            _copy_upstream_libs(tcbase_lib_dir, tmesh_pkg_dir / "lib", tmesh_pkg_dir, "termin_base")

        if sys.platform == "win32":
            for dll in (staging_dir / "lib").glob("*.dll"):
                shutil.copy2(dll, tmesh_pkg_dir / dll.name)
            for dll in (staging_dir / "bin").glob("*.dll"):
                shutil.copy2(dll, tmesh_pkg_dir / dll.name)


directory = os.path.dirname(os.path.realpath(__file__))

setup(
    name="tmesh",
    version="0.1.0",
    license="MIT",
    description="Mesh library with Python bindings",
    author="mirmik",
    author_email="mirmikns@yandex.ru",
    python_requires=">=3.8",
    packages=["tmesh"],
    package_dir={"tmesh": "python/tmesh"},
    install_requires=["tcbase", "numpy"],
    package_data={
        "tmesh": [
            "include/**/*.h",
            "include/**/*.hpp",
            "lib/*.so*",
            "*.dll",
            "lib/*.dll",
            "lib/*.lib",
            "lib/cmake/termin_mesh/*.cmake",
        ],
    },
    ext_modules=[
        Extension("tmesh._tmesh_native", sources=[]),
    ],
    cmdclass={"build": CMakeBuild, "build_ext": CMakeBuildExt},
    zip_safe=False,
)
