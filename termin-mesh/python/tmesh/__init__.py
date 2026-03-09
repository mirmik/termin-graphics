import sys as _sys
import os as _os

# On Windows, add directories containing dependency DLLs to search path.
if _sys.platform == "win32":
    try:
        import tcbase as _tcbase
        _tcbase_dir = _os.path.dirname(_tcbase.__file__)
        if _os.path.isdir(_tcbase_dir):
            _os.add_dll_directory(_tcbase_dir)
    except ImportError:
        pass

from tmesh._tmesh_native import *
from tmesh._tmesh_native import log
