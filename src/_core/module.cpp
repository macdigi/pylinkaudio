// pylinkaudio: Python bindings for Ableton Link 4.0 + Link Audio.
// SPDX-License-Identifier: GPL-3.0-or-later

#include <pybind11/pybind11.h>

namespace py = pybind11;

void bind_link_core(py::module_& m);
void bind_link_audio(py::module_& m);

PYBIND11_MODULE(_core, m) {
    m.doc() = "pylinkaudio: Python bindings for Ableton Link 4.0 with Link Audio support";
    bind_link_core(m);
    bind_link_audio(m);
}
