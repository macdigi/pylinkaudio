// SPDX-License-Identifier: GPL-3.0-or-later
//
// Day-3 Link Audio binding.
//
// LinkAudio is a separate class from Link (per upstream — they cannot be used
// simultaneously). The class binds the audio-aware controller, and AudioSource
// subscribes to a remote channel and delivers buffers to Python via a
// lock-free queue.
//
// Threading contract:
//   - The LinkAudioSource callback fires on a Link-managed thread.
//   - That callback ONLY touches the queue (POD memcpy); never the GIL.
//   - Python's read() acquires data via the queue's Reader on the Python
//     thread.
//
// We use upstream's `link_audio::Queue<T>` (slot-pool design with two
// atomic_size_t cursors). It's already shipped on the ARM targets we care
// about, so we don't need to vendor moodycamel for v0.1.

#include <pybind11/pybind11.h>
#include <pybind11/numpy.h>
#include <pybind11/stl.h>

#include <ableton/LinkAudio.hpp>
#include <ableton/link_audio/Queue.hpp>

#include <algorithm>
#include <array>
#include <chrono>
#include <cstring>
#include <memory>
#include <sstream>
#include <thread>

namespace py = pybind11;

namespace {

using ableton::LinkAudio;
using ableton::LinkAudioSource;
using ableton::ChannelId;
using ableton::PeerId;
using NodeId = ableton::link::NodeId;

// Generous per-slot sample capacity. Upstream's reference renderer uses 512;
// 8192 covers any sane network audio buffer (stereo 4096-frame block).
constexpr std::size_t kMaxSamplesPerSlot = 8192;

struct Slot
{
    std::array<std::int16_t, kMaxSamplesPerSlot> samples{};
    std::size_t numFrames = 0;
    std::uint32_t numChannels = 0;
    std::uint32_t sampleRate = 0;
    std::uint64_t count = 0;
    double sessionBeatTime = 0.0;
    double tempo = 0.0;
};

using SlotQueue = ableton::link_audio::Queue<Slot>;

// AudioSink wrapper — announces a channel and provides a Python-friendly
// write() that captures session state, builds a BufferHandle, copies samples,
// and commits in one step.
//
// Inputs are numpy int16 frames in either 1D shape (frames,) for mono or
// 2D shape (frames, channels) where channels is 1 or 2. Float-to-int16
// conversion is left to the caller (`(x * 32767).clip(...).astype(np.int16)`)
// to keep the binding allocation-free in the hot path.
class AudioSink
{
public:
    AudioSink(LinkAudio& link, std::string name, std::size_t maxSamples)
      : mpLink(&link)
      , mSink(link, std::move(name), maxSamples)
    {
    }

    bool write(const py::array_t<std::int16_t,
                py::array::c_style | py::array::forcecast>& frames,
               std::uint32_t sampleRate,
               double quantum)
    {
        const auto info = frames.request();
        std::size_t numFrames = 0;
        std::size_t numChannels = 0;

        if (info.ndim == 1) {
            numFrames = static_cast<std::size_t>(info.shape[0]);
            numChannels = 1;
        } else if (info.ndim == 2) {
            numFrames = static_cast<std::size_t>(info.shape[0]);
            numChannels = static_cast<std::size_t>(info.shape[1]);
        } else {
            throw py::value_error(
                "frames must be 1D (mono) or 2D (frames, channels)");
        }

        if (numChannels != 1 && numChannels != 2) {
            throw py::value_error(
                "Link Audio supports only 1 or 2 channels");
        }
        if (numFrames == 0) {
            return false;
        }

        const auto totalSamples = numFrames * numChannels;
        if (totalSamples > mSink.maxNumSamples()) {
            throw py::value_error(
                "frames * channels exceeds the sink's max_num_samples; "
                "increase max_samples in the AudioSink constructor or call "
                ".request_max_num_samples()");
        }

        // Capture the session state and timestamp on the calling thread —
        // app-session is thread-safe but not RT-safe. Users on a hard
        // real-time thread should batch via the to-be-added audio-thread
        // overload (v0.2).
        auto state = mpLink->captureAppSessionState();
        const auto hostTimeUs = mpLink->clock().micros();

        ableton::LinkAudioSink::BufferHandle handle(mSink);
        if (!handle) {
            return false;  // no remote source listening or pool exhausted
        }

        std::memcpy(
            handle.samples,
            info.ptr,
            totalSamples * sizeof(std::int16_t));

        const auto beatsAtBufferBegin = state.beatAtTime(hostTimeUs, quantum);
        return handle.commit(
            state,
            beatsAtBufferBegin,
            quantum,
            numFrames,
            numChannels,
            sampleRate);
    }

    std::string name() const { return mSink.name(); }
    void setName(std::string name) { mSink.setName(std::move(name)); }
    std::size_t maxNumSamples() const { return mSink.maxNumSamples(); }
    void requestMaxNumSamples(std::size_t n) { mSink.requestMaxNumSamples(n); }

private:
    LinkAudio* mpLink;  // lifetime guarded by py::keep_alive<1, 2>
    ableton::LinkAudioSink mSink;
};

// AudioSource — owns a LinkAudioSource and a slot queue. The callback runs
// on a Link-managed thread; it copies the buffer into a queue slot and
// releases. Python reads from the queue on its own thread.
class AudioSource
{
public:
    AudioSource(LinkAudio& link, ChannelId channelId, std::size_t numSlots = 64)
    {
        auto queue = SlotQueue(numSlots, Slot{});
        mWriter = std::make_shared<SlotQueue::Writer>(std::move(queue.writer()));
        mReader = std::make_shared<SlotQueue::Reader>(std::move(queue.reader()));

        // Capture the writer by shared_ptr so the queue outlives the
        // LinkAudioSource even if mSource holds the last reference to the
        // callback. mSource is destroyed first in our destructor.
        auto writer = mWriter;
        mSource = std::make_unique<LinkAudioSource>(
            link, channelId,
            [writer](LinkAudioSource::BufferHandle bh) {
                // Realtime-ish path: no Python, no allocation, no GIL.
                if (!writer->retainSlot()) {
                    return;  // queue full — drop this buffer
                }
                Slot& slot = *(*writer)[0];
                const auto numSamples = bh.info.numFrames * bh.info.numChannels;
                const auto copyN = std::min(numSamples, kMaxSamplesPerSlot);
                std::memcpy(slot.samples.data(), bh.samples, copyN * sizeof(std::int16_t));
                slot.numFrames = bh.info.numFrames;
                slot.numChannels = static_cast<std::uint32_t>(bh.info.numChannels);
                slot.sampleRate = bh.info.sampleRate;
                slot.count = bh.info.count;
                slot.sessionBeatTime = bh.info.sessionBeatTime;
                slot.tempo = bh.info.tempo;
                writer->releaseSlot();
            });
    }

    AudioSource(const AudioSource&) = delete;
    AudioSource& operator=(const AudioSource&) = delete;

    ~AudioSource()
    {
        // Drop the source first so no more callbacks fire. Release the GIL
        // in case the underlying impl waits on a worker that needs to settle.
        py::gil_scoped_release release;
        mSource.reset();
    }

    // Returns (numpy_int16[num_frames, num_channels], info dict) or None.
    py::object readNonblocking()
    {
        // Pull every slot the writer has released into our retained view.
        while (mReader->retainSlot()) {}
        if (mReader->numRetainedSlots() == 0) {
            return py::none();
        }

        const Slot& slot = *(*mReader)[0];
        const auto numFrames = slot.numFrames;
        const auto numChannels = slot.numChannels;

        // Shape (frames, channels) — interleaved storage matches numpy
        // C-order layout exactly, single memcpy.
        py::array_t<std::int16_t> arr(
            {static_cast<py::ssize_t>(numFrames),
             static_cast<py::ssize_t>(numChannels)});
        std::memcpy(
            arr.mutable_data(),
            slot.samples.data(),
            numFrames * numChannels * sizeof(std::int16_t));

        py::dict info;
        info["num_frames"] = numFrames;
        info["num_channels"] = numChannels;
        info["sample_rate"] = slot.sampleRate;
        info["count"] = slot.count;
        info["session_beat_time"] = slot.sessionBeatTime;
        info["tempo"] = slot.tempo;

        mReader->releaseSlot();
        return py::make_tuple(std::move(arr), std::move(info));
    }

    py::object read(double timeoutSec)
    {
        if (timeoutSec <= 0.0) {
            return readNonblocking();
        }
        const auto deadline = std::chrono::steady_clock::now()
            + std::chrono::microseconds(static_cast<std::int64_t>(timeoutSec * 1e6));
        for (;;) {
            auto result = readNonblocking();
            if (!result.is_none()) {
                return result;
            }
            if (std::chrono::steady_clock::now() >= deadline) {
                return py::none();
            }
            // Sleep without holding the GIL so the rest of Python can run.
            py::gil_scoped_release release;
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
    }

    std::size_t pending() const { return mReader->numQueuedSlots(); }
    std::size_t capacity() const { return mReader->numSlots(); }

private:
    std::shared_ptr<SlotQueue::Writer> mWriter;
    std::shared_ptr<SlotQueue::Reader> mReader;
    std::unique_ptr<LinkAudioSource> mSource;
};

py::bytes nodeIdToBytes(const NodeId& id)
{
    return py::bytes(reinterpret_cast<const char*>(id.data()), id.size());
}

NodeId nodeIdFromBytes(const py::bytes& b)
{
    std::string s = b;
    if (s.size() != 8) {
        throw py::value_error("NodeId requires exactly 8 bytes");
    }
    NodeId id;
    std::memcpy(id.data(), s.data(), 8);
    return id;
}

std::string nodeIdHex(const NodeId& id)
{
    std::ostringstream oss;
    oss << id;
    return oss.str();
}

}  // namespace


void bind_link_audio(py::module_& m)
{
    using ableton::LinkAudio;
    using SessionState = LinkAudio::SessionState;

    // ---- NodeId (used as ChannelId / PeerId / SessionId) ----
    py::class_<NodeId>(m, "NodeId",
        "Opaque 8-byte identifier used for ChannelId, PeerId, and SessionId. "
        "Compares by bytes, hashable.")
        .def(py::init(&nodeIdFromBytes), py::arg("data"),
             "Construct from an 8-byte bytes object.")
        .def("__bytes__", &nodeIdToBytes)
        .def("hex", &nodeIdHex, "Hex string representation: '0xaabbccdd...'.")
        .def("__repr__", [](const NodeId& id) {
            return "NodeId(" + nodeIdHex(id) + ")";
        })
        .def("__str__", &nodeIdHex)
        .def("__eq__", [](const NodeId& a, const NodeId& b) {
            return std::memcmp(a.data(), b.data(), a.size()) == 0;
        })
        .def("__ne__", [](const NodeId& a, const NodeId& b) {
            return std::memcmp(a.data(), b.data(), a.size()) != 0;
        })
        .def("__hash__", [](const NodeId& id) {
            std::size_t h = 0;
            for (auto b : id) { h = h * 31 + b; }
            return h;
        });

    // ---- Channel ----
    py::class_<LinkAudio::Channel>(m, "Channel",
        "An audio channel announced by a peer in the current Link session.")
        .def_readonly("id", &LinkAudio::Channel::id,
            "Stable identifier for the channel (NodeId).")
        .def_readonly("name", &LinkAudio::Channel::name,
            "Display name of the channel.")
        .def_readonly("peer_id", &LinkAudio::Channel::peerId,
            "Identifier of the peer providing this channel.")
        .def_readonly("peer_name", &LinkAudio::Channel::peerName,
            "Display name of the peer providing this channel.")
        .def("__repr__", [](const LinkAudio::Channel& c) {
            return "Channel(" + c.peerName + " | " + c.name + ")";
        });

    // ---- LinkAudio ----
    // Note: LinkAudio is NOT a Python subclass of Link. The C++ types both
    // descend from BasicLink<Clock> but neither inherits from the other.
    // We re-bind the Link-side methods on LinkAudio (small lambda wrappers).
    py::class_<LinkAudio>(m, "LinkAudio",
        "A Link session participant with audio-streaming support. Replaces "
        "Link in audio-capable applications — do not use both simultaneously.")

        .def(py::init<double, std::string>(),
             py::arg("bpm"), py::arg("name"),
             "Construct with initial tempo (BPM) and a peer name (visible to "
             "other peers; truncated at 256 chars).")

        // ---- Link parts ----
        .def_property("enabled",
            &LinkAudio::isEnabled,
            &LinkAudio::enable,
            "Whether this instance is participating in the Link session.")

        .def_property("start_stop_sync_enabled",
            &LinkAudio::isStartStopSyncEnabled,
            &LinkAudio::enableStartStopSync,
            "Whether transport start/stop is synchronized across peers.")

        .def("num_peers",
            [](const LinkAudio& self) { return self.numPeers(); },
            "Number of other peers currently visible.")

        .def("clock_micros",
            [](const LinkAudio& self) {
                return self.clock().micros().count();
            },
            "Current Link clock value in microseconds since clock epoch.")

        .def("capture_audio_session_state", &LinkAudio::captureAudioSessionState,
             "Capture session state from the audio thread (RT-safe; not thread-safe).")
        .def("commit_audio_session_state", &LinkAudio::commitAudioSessionState,
             py::arg("state"),
             "Commit session state from the audio thread (RT-safe; not thread-safe).")
        .def("capture_app_session_state", &LinkAudio::captureAppSessionState,
             "Capture session state from any application thread (thread-safe).")
        .def("commit_app_session_state", &LinkAudio::commitAppSessionState,
             py::arg("state"),
             "Commit session state from any application thread (thread-safe).")

        // ---- LinkAudio-specific ----
        .def_property("link_audio_enabled",
            &LinkAudio::isLinkAudioEnabled,
            &LinkAudio::enableLinkAudio,
            "Whether audio streaming is enabled on this instance.")

        .def_property("peer_name",
            &LinkAudio::peerName,
            &LinkAudio::setPeerName,
            "Local peer name visible to other Link peers.")

        .def("channels",
            [](const LinkAudio& self) { return self.channels(); },
            "List of currently available audio channels in the session.")
        ;

    // ---- AudioSink ----
    py::class_<AudioSink>(m, "AudioSink",
        "Announces an audio channel to the Link session. Other peers with a "
        "matching AudioSource will receive the audio buffers committed via "
        "write(). The sink lifetime is what determines channel visibility — "
        "as long as the AudioSink is alive, the channel is announced.")
        .def(py::init<LinkAudio&, std::string, std::size_t>(),
             py::arg("link"),
             py::arg("name"),
             py::arg("max_samples") = 4096,
             py::keep_alive<1, 2>(),
             "Construct a sink for the given channel name. max_samples is "
             "the largest single buffer (frames * channels) ever passed to "
             "write(); 4096 covers stereo 2048-frame blocks.")

        .def("write", &AudioSink::write,
             py::arg("frames"),
             py::arg("sample_rate"),
             py::arg("quantum") = 4.0,
             "Send a buffer of int16 audio. `frames` is a numpy int16 array "
             "shape (num_frames,) for mono or (num_frames, channels) for "
             "stereo. Returns True if a buffer was committed, False if no "
             "remote source is listening or no internal buffer was available.")

        .def_property("name", &AudioSink::name, &AudioSink::setName,
             "Display name of the channel. Truncated to 256 chars.")

        .def("max_num_samples", &AudioSink::maxNumSamples,
             "Current maximum buffer capacity in samples (frames * channels).")

        .def("request_max_num_samples", &AudioSink::requestMaxNumSamples,
             py::arg("num_samples"),
             "Request a larger buffer-handle capacity. No-op if already big enough.")
        ;

    // ---- AudioSource ----
    py::class_<AudioSource>(m, "AudioSource",
        "Subscribes to a remote audio channel. Buffers arrive on a "
        "Link-managed thread, are queued lock-free, and consumed via read().")
        .def(py::init<LinkAudio&, ChannelId, std::size_t>(),
             py::arg("link"),
             py::arg("channel_id"),
             py::arg("num_slots") = 64,
             py::keep_alive<1, 2>(),
             "Subscribe to the given channel. num_slots controls the queue "
             "capacity (~each slot holds one network buffer).")

        .def("read_nonblocking", &AudioSource::readNonblocking,
             "Returns (numpy_int16_array(shape=(frames, channels)), info_dict) "
             "if a buffer is pending, else None. Never blocks.")

        .def("read", &AudioSource::read, py::arg("timeout") = 0.0,
             "Like read_nonblocking but waits up to `timeout` seconds for a "
             "buffer to arrive. Returns None on timeout.")

        .def("pending", &AudioSource::pending,
             "Number of buffers currently sitting in the queue.")

        .def("capacity", &AudioSource::capacity,
             "Total queue capacity (number of slots).")
        ;
}
