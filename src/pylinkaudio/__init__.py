"""Python bindings for Ableton Link 4.0 with Link Audio support."""

from pylinkaudio._core import Link

try:
    from pylinkaudio._version import version as __version__
except ImportError:
    __version__ = "0.0.0+unknown"

__all__ = ["Link", "__version__"]
