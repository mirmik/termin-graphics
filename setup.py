#!/usr/bin/env python3

from setuptools import setup, Extension
from setuptools.command.build_ext import build_ext
from setuptools.command.build import build as _build
from pathlib import Path
import shutil
import subprocess
import sys
import os


def _get_sdk_prefix():
    """On Windows, return path to shared termin-sdk directory."""
    if sys.platform == "win32":
        base = os.environ.get("LOCALAPPDATA", os.path.expanduser("~/AppData/Local"))
        return Path(base) / "termin-sdk"
    return None


def _copytree(src, dst):
    """Copy directory tree. On Windows, dereferences symlinks."""
    if dst.exists():
        shutil.rmtree(dst)
    follow = sys.platform == "win32"
    shutil.copytree(src, dst, symlinks=not follow)


def _copy_upstream_libs(src_lib_dir, dst_lib_dir, dst_pkg_dir, name_prefix):
    """Copy upstream shared library files into package.
    On Linux: copies .so files and symlinks to lib/ dir.
    On Windows: copies .dll to pkg dir (next to .pyd), .lib to lib/ dir.
    """
    if not src_lib_dir.exists():
        return

    dst_lib_dir.mkdir(parents=True, exist_ok=True)

    if sys.platform == "win32":
        for f in src_lib_dir.glob(f"{name_prefix}*.dll"):
            shutil.copy2(f, dst_pkg_dir / f.name)
        for f in src_lib_dir.glob(f"{name_prefix}*.lib"):
            shutil.copy2(f, dst_lib_dir / f.name)
    else:
        # Copy real files first, then symlinks
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
    """Run build_ext before build_py so that DLLs copied to source tree
    during compilation are picked up by package_data."""

    def run(self):
        self.run_command("build_ext")
        _build.run(self)


class CMakeBuildExt(build_ext):
    """
    Build nanobind extension via CMake.
    CMake builds termin_graphics library + _tgfx_native module.
    Also installs C++ artifacts (headers, libs, cmake configs) into the package.
    """

    def build_extension(self, ext):
        source_dir = Path(directory)
        build_temp = Path(self.build_temp)
        build_temp.mkdir(parents=True, exist_ok=True)

        cfg = "Debug" if self.debug else "Release"

        staging_dir = (build_temp / "install").resolve()
        staging_dir.mkdir(parents=True, exist_ok=True)

        # Find tcbase C++ prefix (headers, lib, cmake configs)
        try:
            import tcbase
            tcbase_prefix = str(Path(tcbase.__file__).parent)
        except ImportError:
            tcbase_prefix = None

        # On Windows, prefer SDK directory for cmake find_package
        sdk = _get_sdk_prefix()

        cmake_args = [
            str(source_dir),
            f"-DCMAKE_BUILD_TYPE={cfg}",
            f"-DTGFX_BUILD_PYTHON=ON",
            f"-DPython_EXECUTABLE={sys.executable}",
            f"-DCMAKE_INSTALL_PREFIX={staging_dir}",
            f"-DCMAKE_INSTALL_LIBDIR=lib",
        ]

        # Build CMAKE_PREFIX_PATH from available sources
        prefix_paths = []
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

        # On Windows, also install C++ artifacts into shared SDK directory
        if sdk:
            sdk.mkdir(parents=True, exist_ok=True)
            subprocess.check_call(
                ["cmake", "--install", ".", "--config", cfg, "--prefix", str(sdk)],
                cwd=build_temp,
            )

        # Find the built native module (.so on Linux, .pyd on Windows)
        # On MSVC, outputs land in a Release/ or Debug/ subdirectory
        patterns = ["_tgfx_native.*.so", "_tgfx_native.*.pyd", "_tgfx_native.pyd"]
        built_files = []
        for pat in patterns:
            built_files.extend((build_temp / "python").glob(pat))
            built_files.extend((build_temp / "python" / cfg).glob(pat))
        if not built_files:
            raise RuntimeError("CMake build did not produce _tgfx_native module")

        built_module = built_files[0]

        # Copy to where setuptools expects it (makes install/bdist_wheel work)
        ext_path = Path(self.get_ext_fullpath(ext.name))
        ext_path.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(built_module, ext_path)

        # Copy C++ artifacts next to the module
        ext_pkg_dir = ext_path.parent
        if (staging_dir / "include").exists():
            _copytree(staging_dir / "include", ext_pkg_dir / "include")
        if (staging_dir / "lib").exists():
            _copytree(staging_dir / "lib", ext_pkg_dir / "lib")

        # Copy libtermin_base from tcbase (runtime dependency of libtermin_graphics)
        if tcbase_prefix:
            tcbase_lib_dir = Path(tcbase_prefix) / "lib"
            _copy_upstream_libs(tcbase_lib_dir, ext_pkg_dir / "lib", ext_pkg_dir, "libtermin_base")
            _copy_upstream_libs(tcbase_lib_dir, ext_pkg_dir / "lib", ext_pkg_dir, "termin_base")

        # On Windows, copy DLLs next to the .pyd
        if sys.platform == "win32":
            for dll in (staging_dir / "lib").glob("*.dll"):
                shutil.copy2(dll, ext_pkg_dir / dll.name)
            for dll in (staging_dir / "bin").glob("*.dll"):
                shutil.copy2(dll, ext_pkg_dir / dll.name)

        # Also copy to source tree so build_py picks them up via package_data
        tgfx_pkg_dir = source_dir / "python" / "tgfx"
        tgfx_pkg_dir.mkdir(parents=True, exist_ok=True)
        shutil.copy2(built_module, tgfx_pkg_dir / built_module.name)
        if (staging_dir / "include").exists():
            _copytree(staging_dir / "include", tgfx_pkg_dir / "include")
        if (staging_dir / "lib").exists():
            _copytree(staging_dir / "lib", tgfx_pkg_dir / "lib")

        if tcbase_prefix:
            tcbase_lib_dir = Path(tcbase_prefix) / "lib"
            _copy_upstream_libs(tcbase_lib_dir, tgfx_pkg_dir / "lib", tgfx_pkg_dir, "libtermin_base")
            _copy_upstream_libs(tcbase_lib_dir, tgfx_pkg_dir / "lib", tgfx_pkg_dir, "termin_base")

        if sys.platform == "win32":
            for dll in (staging_dir / "lib").glob("*.dll"):
                shutil.copy2(dll, tgfx_pkg_dir / dll.name)
            for dll in (staging_dir / "bin").glob("*.dll"):
                shutil.copy2(dll, tgfx_pkg_dir / dll.name)


directory = os.path.dirname(os.path.realpath(__file__))

setup(
    name="tgfx",
    version="0.1.0",
    license="MIT",
    description="Graphics backend library with Python bindings",
    author="mirmik",
    author_email="mirmikns@yandex.ru",
    python_requires=">=3.8",
    packages=["tgfx"],
    package_dir={"tgfx": "python/tgfx"},
    install_requires=["tcbase", "numpy"],
    package_data={
        "tgfx": [
            "include/**/*.h",
            "include/**/*.hpp",
            # Linux
            "lib/*.so*",
            # Windows
            "*.dll",
            "lib/*.dll",
            "lib/*.lib",
            # CMake configs
            "lib/cmake/termin_graphics/*.cmake",
        ],
    },
    ext_modules=[
        Extension("tgfx._tgfx_native", sources=[]),
    ],
    cmdclass={"build": CMakeBuild, "build_ext": CMakeBuildExt},
    zip_safe=False,
)
