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
    from pylinkaudio._version import version as __version__
except ImportError:
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
