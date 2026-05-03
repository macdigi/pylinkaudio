"""Python bindings for Ableton Link 4.0 with Link Audio support."""

from pylinkaudio._core import (
    Link,
    SessionState,
    LinkAudio,
    NodeId,
    Channel,
    AudioSource,
    AudioSink,
)

try:
    from importlib.metadata import version as _v
    __version__ = _v("pylinkaudio")
    del _v
except Exception:
    __version__ = "0.0.0+unknown"

__all__ = [
    "Link",
    "SessionState",
    "LinkAudio",
    "NodeId",
    "Channel",
    "AudioSource",
    "AudioSink",
    "__version__",
]
