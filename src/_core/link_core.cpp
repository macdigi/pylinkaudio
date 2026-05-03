// SPDX-License-Identifier: GPL-3.0-or-later
//
// Day-2 Link binding: full Link + SessionState API.
//
// Time values cross the C++/Python boundary as int64 microseconds (not
// datetime.timedelta) — Link uses microseconds-since-clock-epoch and ints
// are far more ergonomic than timedeltas for absolute timestamps.
//
// Callbacks (setNumPeersCallback / setTempoCallback / setStartStopCallback)
// are intentionally NOT bound in v0.1 — they fire on a Link-managed thread,
// and a clean Python-side dispatch needs the threading + GIL machinery that
// lands with v0.2's asyncio_ext.

#include <pybind11/pybind11.h>
#include <ableton/Link.hpp>

#include <chrono>
#include <cstdint>

namespace py = pybind11;

namespace {

// Helpers — keep the bindings readable.
inline std::chrono::microseconds us(std::int64_t v) {
    return std::chrono::microseconds(v);
}

inline std::int64_t to_us(std::chrono::microseconds m) {
    return m.count();
}

}  // namespace

void bind_link_core(py::module_& m) {
    using Link = ableton::Link;
    using SessionState = Link::SessionState;

    py::class_<SessionState>(m, "SessionState",
        "A snapshot of a Link session's timeline + transport start/stop state. "
        "Obtained via Link.capture_audio_session_state() (audio thread only) or "
        "Link.capture_app_session_state() (any thread). Mutate locally, then commit "
        "back via the matching Link.commit_*_session_state() to broadcast to peers.")

        .def("tempo", &SessionState::tempo,
             "Tempo of the timeline in BPM. Stable display value; actual beat "
             "progress may vary slightly due to clock-drift compensation.")

        .def("set_tempo",
            [](SessionState& self, double bpm, std::int64_t at_time_us) {
                self.setTempo(bpm, us(at_time_us));
            },
            py::arg("bpm"), py::arg("at_time_us"),
            "Set the timeline tempo, taking effect at the given clock time (us).")

        .def("beat_at_time",
            [](const SessionState& self, std::int64_t time_us, double quantum) {
                return self.beatAtTime(us(time_us), quantum);
            },
            py::arg("time_us"), py::arg("quantum"),
            "Beat value at the given clock time for the given quantum. Beat phase "
            "(fmod) is shared across all session peers.")

        .def("phase_at_time",
            [](const SessionState& self, std::int64_t time_us, double quantum) {
                return self.phaseAtTime(us(time_us), quantum);
            },
            py::arg("time_us"), py::arg("quantum"),
            "Phase in [0, quantum) at the given clock time.")

        .def("time_at_beat",
            [](const SessionState& self, double beat, double quantum) {
                return to_us(self.timeAtBeat(beat, quantum));
            },
            py::arg("beat"), py::arg("quantum"),
            "Inverse of beat_at_time: clock time (us) at which the given beat occurs.")

        .def("request_beat_at_time",
            [](SessionState& self, double beat, std::int64_t time_us, double quantum) {
                self.requestBeatAtTime(beat, us(time_us), quantum);
            },
            py::arg("beat"), py::arg("time_us"), py::arg("quantum"),
            "Quantized re-mapping of beat <-> time. With other peers, defers to the "
            "next phase-aligned moment; alone in a session, snaps immediately.")

        .def("force_beat_at_time",
            [](SessionState& self, double beat, std::int64_t time_us, double quantum) {
                self.forceBeatAtTime(beat, us(time_us), quantum);
            },
            py::arg("beat"), py::arg("time_us"), py::arg("quantum"),
            "DANGER: Unconditionally re-maps beat <-> time and broadcasts. "
            "Will cause beat discontinuities for all peers — only legitimate use "
            "is bridging Link to an external master clock.")

        .def("is_playing", &SessionState::isPlaying,
             "Whether transport is currently playing.")

        .def("set_is_playing",
            [](SessionState& self, bool is_playing, std::int64_t time_us) {
                self.setIsPlaying(is_playing, us(time_us));
            },
            py::arg("is_playing"), py::arg("time_us"),
            "Request transport start/stop at the given clock time (us). "
            "Only broadcasts when start_stop_sync_enabled is True.")

        .def("time_for_is_playing",
            [](const SessionState& self) { return to_us(self.timeForIsPlaying()); },
            "Clock time (us) at which the current transport state took effect.")

        .def("request_beat_at_start_playing_time",
            &SessionState::requestBeatAtStartPlayingTime,
            py::arg("beat"), py::arg("quantum"),
            "Map the given beat to the transport start time. No-op if not playing.")

        .def("set_is_playing_and_request_beat_at_time",
            [](SessionState& self, bool is_playing, std::int64_t time_us,
               double beat, double quantum) {
                self.setIsPlayingAndRequestBeatAtTime(is_playing, us(time_us), beat, quantum);
            },
            py::arg("is_playing"), py::arg("time_us"),
            py::arg("beat"), py::arg("quantum"),
            "Combined start/stop + quantized beat mapping in one call.")

        .def("__eq__", [](const SessionState& a, const SessionState& b) { return a == b; })
        .def("__ne__", [](const SessionState& a, const SessionState& b) { return a != b; })
        ;

    py::class_<Link>(m, "Link",
        "An Ableton Link session participant. Construct with the initial tempo "
        "in BPM, then set `enabled = True` to join the local-network session.")

        .def(py::init<double>(), py::arg("bpm"),
             "Construct a Link instance with the given initial tempo (BPM).")

        .def_property("enabled",
            &Link::isEnabled,
            &Link::enable,
            "Whether this Link instance is participating in the network session. "
            "Setter is NOT realtime-safe.")

        .def_property("start_stop_sync_enabled",
            &Link::isStartStopSyncEnabled,
            &Link::enableStartStopSync,
            "Whether transport start/stop is synchronized across peers.")

        .def("num_peers",
            [](const Link& self) { return self.numPeers(); },
            "Number of other peers currently visible on the LAN.")

        .def("clock_micros",
            [](const Link& self) { return to_us(self.clock().micros()); },
            "Current Link clock value in microseconds since clock epoch. "
            "Realtime-safe.")

        .def("capture_audio_session_state", &Link::captureAudioSessionState,
            "Capture session state from the audio thread (realtime-safe). "
            "MUST only be called from the audio thread; not thread-safe.")

        .def("commit_audio_session_state", &Link::commitAudioSessionState,
            py::arg("state"),
            "Commit session state from the audio thread (realtime-safe). "
            "MUST only be called from the audio thread; not thread-safe.")

        .def("capture_app_session_state", &Link::captureAppSessionState,
            "Capture session state from any application thread (thread-safe, "
            "not realtime-safe).")

        .def("commit_app_session_state", &Link::commitAppSessionState,
            py::arg("state"),
            "Commit session state from any application thread (thread-safe, "
            "not realtime-safe).")
        ;
}
