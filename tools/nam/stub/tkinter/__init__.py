# Stand-in so nam imports without a display toolkit (training here is headless).
import sys, types
class _Any:
    def __init__(self, *a, **k): pass
    def __getattr__(self, k): return _Any()
    def __call__(self, *a, **k): return _Any()
def _mod_getattr(k):
    if k.startswith('__'): raise AttributeError(k)
    return _Any
for _n in ("filedialog", "messagebox", "ttk", "font", "simpledialog", "scrolledtext"):
    m = types.ModuleType("tkinter." + _n); m.__getattr__ = _mod_getattr; sys.modules["tkinter." + _n] = m
def __getattr__(k):
    if k.startswith('__'): raise AttributeError(k)
    return _Any
