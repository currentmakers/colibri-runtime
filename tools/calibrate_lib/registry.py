"""Dynamic discovery of io_*/module.py plugins next to calibrate.py.

No packaging/entry_points needed: dropping a new io_foo/module.py next to
calibrate.py that subclasses IOModule is enough -- discover() just imports
every io_*/module.py it finds, and IOModule.__init_subclass__ does the
actual registration as a side effect of that import.
"""

from __future__ import annotations

import importlib.util
import pathlib

from calibrate_lib.module_base import IOModule, registry


def discover(base_dir: pathlib.Path) -> None:
    for module_py in sorted(base_dir.glob("io_*/module.py")):
        package_dir = module_py.parent
        spec = importlib.util.spec_from_file_location(
            f"colibri_io_modules.{package_dir.name}", module_py
        )
        module = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(module)


def get(module_id: str) -> type[IOModule]:
    modules = registry()
    if module_id not in modules:
        available = ", ".join(sorted(modules)) or "(none found)"
        raise KeyError(f"unknown module '{module_id}' -- available: {available}")
    return modules[module_id]
