#!/usr/bin/env python3

from setuptools import setup, Extension
from setuptools.command.build_ext import build_ext
from pathlib import Path
import shutil
import subprocess
import sys
import os


def _copytree(src, dst):
    """Copy directory tree, preserving symlinks."""
    if dst.exists():
        shutil.rmtree(dst)
    shutil.copytree(src, dst, symlinks=True)


def _copy_shared_libs(src_dir, dst_dir, name_prefix):
    """Copy shared library files and symlinks (e.g., libfoo.so*)."""
    for f in sorted(src_dir.glob(f"{name_prefix}*")):
        dst = dst_dir / f.name
        if f.is_symlink():
            link_target = os.readlink(f)
            if dst.exists() or dst.is_symlink():
                dst.unlink()
            dst.symlink_to(link_target)
        elif f.is_file():
            shutil.copy2(f, dst)


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

        staging_dir = build_temp / "install"
        staging_dir.mkdir(parents=True, exist_ok=True)

        # Find tcbase C++ prefix (headers, lib, cmake configs)
        try:
            import tcbase
            tcbase_prefix = str(Path(tcbase.__file__).parent)
        except ImportError:
            raise RuntimeError(
                "tcbase must be installed before building tgfx.\n"
                "Install it first: pip install <path-to-termin-base>\n"
                "Then: pip install --no-build-isolation <path-to-termin-graphics>"
            )

        cfg = "Debug" if self.debug else "Release"
        cmake_args = [
            str(source_dir),
            f"-DCMAKE_BUILD_TYPE={cfg}",
            f"-DTGFX_BUILD_PYTHON=ON",
            f"-DPython_EXECUTABLE={sys.executable}",
            f"-DCMAKE_PREFIX_PATH={tcbase_prefix}",
            f"-DCMAKE_INSTALL_PREFIX={staging_dir}",
            f"-DCMAKE_INSTALL_LIBDIR=lib",
        ]

        subprocess.check_call(["cmake", *cmake_args], cwd=build_temp)
        subprocess.check_call(
            ["cmake", "--build", ".", "--config", cfg, "--parallel"],
            cwd=build_temp,
        )
        subprocess.check_call(
            ["cmake", "--install", ".", "--config", cfg],
            cwd=build_temp,
        )

        # Find the built .so
        built_files = list((build_temp / "python").glob("_tgfx_native.*"))
        if not built_files:
            raise RuntimeError("CMake build did not produce _tgfx_native module")

        built_so = built_files[0]

        # Copy to where setuptools expects it (makes install/bdist_wheel work)
        ext_path = Path(self.get_ext_fullpath(ext.name))
        ext_path.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(built_so, ext_path)

        # Copy C++ artifacts next to the .so
        ext_pkg_dir = ext_path.parent
        if (staging_dir / "include").exists():
            _copytree(staging_dir / "include", ext_pkg_dir / "include")
        if (staging_dir / "lib").exists():
            _copytree(staging_dir / "lib", ext_pkg_dir / "lib")

        # Copy libtermin_base.so from tcbase into tgfx/lib/
        # (libtermin_graphics.so depends on it at runtime)
        tcbase_lib_dir = Path(tcbase_prefix) / "lib"
        installed_lib_dir = ext_pkg_dir / "lib"
        if tcbase_lib_dir.exists():
            for f in sorted(tcbase_lib_dir.glob("libtermin_base.so*")):
                dst = installed_lib_dir / f.name
                if f.is_symlink():
                    link_target = os.readlink(f)
                    if dst.exists() or dst.is_symlink():
                        dst.unlink()
                    dst.symlink_to(link_target)
                elif f.is_file():
                    shutil.copy2(f, dst)

        # Also copy to source tree for editable/development use
        tgfx_pkg_dir = source_dir / "python" / "tgfx"
        tgfx_pkg_dir.mkdir(parents=True, exist_ok=True)
        shutil.copy2(built_so, tgfx_pkg_dir / built_so.name)
        if (staging_dir / "include").exists():
            _copytree(staging_dir / "include", tgfx_pkg_dir / "include")
        if (staging_dir / "lib").exists():
            _copytree(staging_dir / "lib", tgfx_pkg_dir / "lib")

        # Copy libtermin_base.so into source tree lib/ too
        src_lib_dir = tgfx_pkg_dir / "lib"
        if tcbase_lib_dir.exists():
            for f in sorted(tcbase_lib_dir.glob("libtermin_base.so*")):
                dst = src_lib_dir / f.name
                if f.is_symlink():
                    link_target = os.readlink(f)
                    if dst.exists() or dst.is_symlink():
                        dst.unlink()
                    dst.symlink_to(link_target)
                elif f.is_file():
                    shutil.copy2(f, dst)


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
            "lib/*.so*",
            "lib/*.dll",
            "lib/*.lib",
            "lib/cmake/termin_graphics/*.cmake",
        ],
    },
    ext_modules=[
        Extension("tgfx._tgfx_native", sources=[]),
    ],
    cmdclass={"build_ext": CMakeBuildExt},
    zip_safe=False,
)
