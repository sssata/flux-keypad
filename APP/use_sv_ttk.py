from functools import partial
from pathlib import Path
import sys

inited = False
root = None


if getattr(sys, 'frozen', False) and hasattr(sys, '_MEIPASS'):
    print('running in a PyInstaller bundle')
    SV_TCL_PATH = (Path(__file__).parent / "sv.tcl").resolve()
else:
    print('running in a normal Python process')
    SV_TCL_PATH = (Path(__file__).parent / "Sun-Valley-ttk-theme" / "sv.tcl").resolve()

def init_theme(func):
    def wrapper(*args, **kwargs):
        global inited
        global root

        if not inited:
            from tkinter import _default_root

            path = SV_TCL_PATH

            try:
                _default_root.tk.call("source", str(path))
            except AttributeError:
                raise RuntimeError(
                    "can't set theme, because tkinter is not initialized. "
                    + "Please create a tkinter.Tk instance first and then set the theme."
                ) from None
            else:
                inited = True
                root = _default_root

        return func(*args, **kwargs)

    return wrapper


@init_theme
def set_theme(theme):
    if theme not in {"dark", "light"}:
        raise RuntimeError("not a valid theme name: {}".format(theme))

    assert root is not None
    root.tk.call("set_theme", theme)


@init_theme
def get_theme():
    assert root is not None
    theme = root.tk.call("ttk::style", "theme", "use")

    return {"sun-valley-dark": "dark", "sun-valley-light": "light"}.get(theme, theme)


@init_theme
def toggle_theme():
    if get_theme() == "light":
        use_dark_theme()
    else:
        use_light_theme()


use_dark_theme = partial(set_theme, "dark")
use_light_theme = partial(set_theme, "light")
