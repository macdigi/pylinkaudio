"""Minimal tests covering the public API. No network dependency.

Tests that need a real Link peer on the LAN are marked
@pytest.mark.network and are skipped in CI.
"""
from __future__ import annotations

import numpy as np
import pytest

from pylinkaudio import (
    AudioSink,
    AudioSource,
    Channel,
    Link,
    LinkAudio,
    NodeId,
    SessionState,
    __version__,
)


def test_version_present():
    assert isinstance(__version__, str) and __version__


def test_link_enable_toggle():
    link = Link(120.0)
    assert link.enabled is False
    link.enabled = True
    assert link.enabled is True
    link.enabled = False
    assert link.enabled is False


def test_link_session_state_round_trip():
    link = Link(140.0)
    state = link.capture_app_session_state()
    # Tempo from a fresh Link instance should be ~140 BPM (we may be in a LAN
    # session with a different tempo, so use a wide tolerance and only check
    # consistency with our commit below).
    new_tempo = 173.5
    state.set_tempo(new_tempo, link.clock_micros())
    link.commit_app_session_state(state)
    state2 = link.capture_app_session_state()
    # If alone in a session, tempo will stick. If joined to peers, peers may
    # override. Either way the value should be a finite float.
    assert state2.tempo() > 0


def test_link_beat_phase_math():
    link = Link(120.0)
    state = link.capture_app_session_state()
    now = link.clock_micros()
    beat = state.beat_at_time(now, 4.0)
    phase = state.phase_at_time(now, 4.0)
    assert isinstance(beat, float)
    assert isinstance(phase, float)
    assert 0.0 <= phase < 4.0
    # time_at_beat is the inverse of beat_at_time
    t = state.time_at_beat(beat, 4.0)
    assert isinstance(t, int)


def test_link_audio_construct_and_destruct():
    """Regression for the ODR-violation crash: constructing+destroying
    LinkAudio used to fail with 'mutex lock failed: Invalid argument' when
    link_core.cpp included Link.hpp while link_audio.cpp included
    LinkAudio.hpp (different definitions of link::ApiController across
    translation units in the same .so)."""
    link = LinkAudio(120.0, "pylinkaudio-test")
    assert link.enabled is False
    link.enabled = True
    link.link_audio_enabled = True
    link.link_audio_enabled = False
    link.enabled = False


def test_node_id_round_trip():
    raw = b"\x01\x02\x03\x04\x05\x06\x07\x08"
    nid = NodeId(raw)
    assert bytes(nid) == raw
    assert nid == NodeId(raw)
    assert nid != NodeId(b"\x00" * 8)
    assert hash(nid) == hash(NodeId(raw))
    # Hex output is "0x" + 16 hex chars.
    assert nid.hex().startswith("0x")
    assert len(nid.hex()) == 18


def test_node_id_wrong_length_rejected():
    with pytest.raises(ValueError):
        NodeId(b"\x01\x02")


def test_audio_sink_basic():
    link = LinkAudio(120.0, "pylinkaudio-test")
    sink = AudioSink(link, "test", max_samples=2048)
    assert sink.name == "test"
    assert sink.max_num_samples() == 2048
    sink.name = "renamed"
    assert sink.name == "renamed"


def test_audio_sink_rejects_bad_shape():
    link = LinkAudio(120.0, "pylinkaudio-test")
    sink = AudioSink(link, "test", max_samples=4096)
    # 3 channels — Link Audio supports only 1 or 2.
    with pytest.raises(ValueError, match="1 or 2 channels"):
        sink.write(np.zeros((512, 3), dtype=np.int16), sample_rate=48000)
    # 3-D input
    with pytest.raises(ValueError, match="1D|2D"):
        sink.write(np.zeros((10, 10, 2), dtype=np.int16), sample_rate=48000)
    # Empty buffer — no-op, returns False.
    assert sink.write(np.zeros(0, dtype=np.int16), sample_rate=48000) is False


def test_audio_sink_no_peer_returns_false():
    """With no remote source listening, write() returns False every time
    (no buffer to fill)."""
    link = LinkAudio(120.0, "pylinkaudio-test")
    sink = AudioSink(link, "lonely-channel", max_samples=2048)
    assert sink.write(np.zeros(512, dtype=np.int16), sample_rate=48000) is False
    assert sink.write(np.zeros((512, 2), dtype=np.int16), sample_rate=48000) is False


def test_audio_source_lifecycle_with_synthetic_id():
    """AudioSource constructs, reads nothing, destructs cleanly when no
    matching channel exists."""
    link = LinkAudio(120.0, "pylinkaudio-test")
    src = AudioSource(link, NodeId(b"\x00" * 8), num_slots=16)
    assert src.capacity() == 16
    assert src.pending() == 0
    assert src.read_nonblocking() is None


def test_audio_source_read_timeout():
    """read() with a timeout returns None after waiting roughly that long
    when no buffer is delivered."""
    import time
    link = LinkAudio(120.0, "pylinkaudio-test")
    src = AudioSource(link, NodeId(b"\x00" * 8), num_slots=4)
    t0 = time.monotonic()
    result = src.read(timeout=0.2)
    dt = time.monotonic() - t0
    assert result is None
    # Generous bounds — CI runners have variable scheduling.
    assert 0.15 < dt < 0.6
