// SPDX-License-Identifier: GPL-3.0-or-later
//
// Day-1 minimal Link binding: construct, enable/disable, num_peers.
// Full SessionState / transport bindings land on day 2.

#include <pybind11/pybind11.h>
#include <ableton/Link.hpp>

namespace py = pybind11;

void bind_link_core(py::module_& m) {
    py::class_<ableton::Link>(m, "Link",
        "An Ableton Link session. Construct with the initial tempo in BPM, "
        "then set `enabled = True` to join the local-network session.")
        .def(py::init<double>(), py::arg("bpm"),
             "Construct a Link instance with the given initial tempo (BPM).")
        .def_property("enabled",
            &ableton::Link::isEnabled,
            &ableton::Link::enable,
            "Whether this Link instance is participating in the network session.")
        .def_property("start_stop_sync_enabled",
            &ableton::Link::isStartStopSyncEnabled,
            &ableton::Link::enableStartStopSync,
            "Whether transport start/stop is synchronized across peers.")
        .def("num_peers",
            [](const ableton::Link& self) { return self.numPeers(); },
            "Number of other peers currently visible on the LAN.");
}
