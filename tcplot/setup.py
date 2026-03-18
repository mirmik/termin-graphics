from setuptools import setup, find_packages

setup(
    name="tcplot",
    version="0.1.0",
    packages=find_packages(),
    install_requires=[
        "tcbase",
        "tgfx",
        "tcgui",
        "numpy",
    ],
    zip_safe=False,
)
