// Copyright 2026 sendspin-cpp-cli Contributors
// Modified 2026 CARALACM for bt-speaker-cli
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include "audio_sink.h"

#include "log.h"
#include "null_sink.h"

#ifdef BT_SPEAKER_CLI_HAVE_ALSA
#include "alsa_sink.h"
#endif

#ifdef BT_SPEAKER_CLI_HAVE_PULSE
#include "pulse_sink.h"
#endif

#ifdef BT_SPEAKER_CLI_HAVE_PIPEWIRE
#include "pipewire_sink.h"
#endif

#include <algorithm>
#include <array>
#include <memory>
#include <string>
#include <vector>

namespace bt_speaker_cli {

namespace {

enum class DeviceArg {
    None,
    Required,
    Optional,
};

struct BuiltBackend {
    const char* name;
    SinkBackend backend;
    DeviceArg device_arg;
};

constexpr BuiltBackend BUILT_BACKENDS[] = {
    {"null", SinkBackend::Null, DeviceArg::None},
    {"stdout", SinkBackend::Stdout, DeviceArg::None},
#ifdef BT_SPEAKER_CLI_HAVE_PULSE
    {"pulse", SinkBackend::Pulse, DeviceArg::Optional},
#endif
#ifdef BT_SPEAKER_CLI_HAVE_PIPEWIRE
    {"pipewire", SinkBackend::PipeWire, DeviceArg::Optional},
#endif
#ifdef BT_SPEAKER_CLI_HAVE_ALSA
    {"alsa", SinkBackend::Alsa, DeviceArg::Required},
#endif
};

struct ReservedBackend {
    const char* name;
    const char* reason;
    const char* alsa_plugin;
};

constexpr ReservedBackend RESERVED_BACKENDS[] = {
    {"pulse",
     "the PulseAudio backend is not in this build -- libpulse was missing, or it was configured "
     "with -DBT_SPEAKER_CLI_WITH_PULSE=OFF",
     "pulse"},
    {"pipewire",
     "the PipeWire backend is not in this build -- libpipewire was missing, or it was configured "
     "with -DBT_SPEAKER_CLI_WITH_PIPEWIRE=OFF",
     "pipewire"},
    {"alsa",
     "the ALSA backend is not in this build -- libasound was missing, or it was configured "
     "with -DBT_SPEAKER_CLI_WITH_ALSA=OFF",
     nullptr},
};

std::string unavailable_error(const ReservedBackend& reserved) {
    std::string error = std::string(reserved.reason) + ". This build has: " + audio_backend_list();
#ifdef BT_SPEAKER_CLI_HAVE_ALSA
    if (reserved.alsa_plugin != nullptr) {
        error += ". That server is still reachable through ALSA's plugin PCM as -o alsa:";
        error += reserved.alsa_plugin;
    }
#endif
    return error;
}

template <typename T>
std::string join_or(const std::vector<T>& values, const char* empty) {
    if (values.empty()) {
        return empty;
    }
    std::string list;
    for (const T& value : values) {
        if (!list.empty()) {
            list += ' ';
        }
        list += std::to_string(static_cast<unsigned>(value));
    }
    return list;
}

void warn_if_shadowing_an_alsa_pcm([[maybe_unused]] const std::string& spec,
                                   [[maybe_unused]] const DeviceSpec& resolved) {
#ifdef BT_SPEAKER_CLI_HAVE_ALSA
    if (resolved.backend != SinkBackend::Pulse && resolved.backend != SinkBackend::PipeWire) {
        return;
    }
    if (spec.find(':') != std::string::npos) {
        return;
    }
    log_line(LogLevel::INFO, LOG_TAG_AUDIO,
             "-o %s is the native %s backend, not ALSA's plugin PCM of the same name -- write "
             "-o alsa:%s for that",
             spec.c_str(), spec.c_str(), spec.c_str());
#endif
}

}  // namespace

SinkCapabilities SinkCapabilities::permissive() {
    return {{PROBE_RATES.begin(), PROBE_RATES.end()},
            {PROBE_BIT_DEPTHS.begin(), PROBE_BIT_DEPTHS.end()},
            {PROBE_CHANNELS.begin(), PROBE_CHANNELS.end()}};
}

std::string audio_backend_list() {
    std::string list;
    for (const BuiltBackend& entry : BUILT_BACKENDS) {
        if (!list.empty()) {
            list += ", ";
        }
        list += entry.name;
    }
    return list;
}

bool alsa_pcm_is_reachable(const std::string& pcm) {
#ifdef BT_SPEAKER_CLI_HAVE_ALSA
    DeviceSpec spec;
    std::string error;
    if (!resolve_device_spec(pcm, spec, error)) {
        return false;
    }
    return spec.backend == SinkBackend::Alsa && spec.device == pcm;
#else
    static_cast<void>(pcm);
    return false;
#endif
}

void print_sink_capabilities(std::FILE* out, const SinkCapabilities& caps,
                             const std::array<const char*, PROBE_BIT_DEPTHS.size()>& depth_names) {
    std::string formats;
    for (size_t i = 0; i < PROBE_BIT_DEPTHS.size(); ++i) {
        if (std::find(caps.bit_depths.begin(), caps.bit_depths.end(), PROBE_BIT_DEPTHS[i]) !=
            caps.bit_depths.end()) {
            if (!formats.empty()) {
                formats += ' ';
            }
            formats += depth_names[i];
        }
    }

    std::fprintf(out, "      rates:    %s\n",
                 join_or(caps.rates, "(none of the probed rates)").c_str());
    std::fprintf(out, "      formats:  %s\n",
                 formats.empty() ? "(none bt-speaker-cli can use)" : formats.c_str());
    std::fprintf(out, "      channels: %s\n",
                 join_or(caps.channels, "(none of the probed counts)").c_str());
}

bool resolve_device_spec(const std::string& spec, DeviceSpec& out, std::string& error) {
    if (spec.empty()) {
        error = "empty output device -- run with -l to list what this build has";
        return false;
    }

    if (spec == "-") {
        out = {SinkBackend::Stdout, ""};
        return true;
    }
    for (const BuiltBackend& entry : BUILT_BACKENDS) {
        if (spec != entry.name) {
            continue;
        }
        if (entry.device_arg == DeviceArg::Required) {
            error = "-o '" + spec + "' names a backend but no device -- write -o " + spec +
                    ":<device>, or -l to list them";
            return false;
        }
        out = {entry.backend, ""};
        return true;
    }

    const size_t colon = spec.find(':');
    const std::string prefix = spec.substr(0, colon);
    if (colon != std::string::npos) {
        const std::string rest = spec.substr(colon + 1);
        for (const BuiltBackend& entry : BUILT_BACKENDS) {
            if (prefix != entry.name) {
                continue;
            }
            if (entry.device_arg == DeviceArg::None) {
                error = "the " + prefix + " backend takes no device, so -o '" + spec +
                        "' means nothing -- use -o " + prefix + " on its own";
                return false;
            }
            if (rest.empty()) {
                error = "-o '" + spec + "' names no device -- write -o " + prefix + ":<device>";
                if (entry.device_arg == DeviceArg::Optional) {
                    error += ", or -o " + prefix + " on its own for this host's default";
                }
                error += ", or -l to list them";
                return false;
            }
            out = {entry.backend, rest};
            return true;
        }
    }
    for (const ReservedBackend& reserved : RESERVED_BACKENDS) {
        if (prefix != reserved.name) {
            continue;
        }
#ifdef BT_SPEAKER_CLI_HAVE_ALSA
        if (colon == std::string::npos && reserved.alsa_plugin != nullptr) {
            break;
        }
#endif
        error = unavailable_error(reserved);
        return false;
    }

#ifdef BT_SPEAKER_CLI_HAVE_ALSA
    out = {SinkBackend::Alsa, spec};
    return true;
#else
    error = "unknown output device '" + spec + "' -- this build has: " + audio_backend_list() +
            " (run with -l)";
    return false;
#endif
}

std::unique_ptr<AudioSink> make_audio_sink(const std::string& device,
                                           [[maybe_unused]] uint32_t buffer_ms,
                                           [[maybe_unused]] const std::string& alsa_mixer,
                                           [[maybe_unused]] const std::string& alsa_mixer_curve,
                                           std::string& error) {
    DeviceSpec spec;
    if (!resolve_device_spec(device, spec, error)) {
        return nullptr;
    }
    warn_if_shadowing_an_alsa_pcm(device, spec);

    switch (spec.backend) {
        case SinkBackend::Null:
            return std::make_unique<NullAudioSink>(NullSinkOutput::Discard);
        case SinkBackend::Stdout:
            return std::make_unique<NullAudioSink>(NullSinkOutput::Stdout);
        case SinkBackend::Pulse: {
#ifdef BT_SPEAKER_CLI_HAVE_PULSE
            std::string probe_err;
            if (!PulseAudioSink::probe(spec.device, probe_err)) {
                log_line(LogLevel::WARN, LOG_TAG_AUDIO,
                         "%s -- waiting for PulseAudio to become ready", probe_err.c_str());
            }
            return std::make_unique<PulseAudioSink>(spec.device, buffer_ms);
#else
            break;
#endif
        }
        case SinkBackend::PipeWire: {
#ifdef BT_SPEAKER_CLI_HAVE_PIPEWIRE
            std::string probe_err;
            if (!PipeWireSink::probe(spec.device, probe_err)) {
                log_line(LogLevel::WARN, LOG_TAG_AUDIO,
                         "%s -- waiting for PipeWire to become ready", probe_err.c_str());
            }
            return std::make_unique<PipeWireSink>(spec.device, buffer_ms);
#else
            break;
#endif
        }
        case SinkBackend::Alsa: {
#ifdef BT_SPEAKER_CLI_HAVE_ALSA
            if (!AlsaAudioSink::probe(spec.device, error)) {
                return nullptr;
            }
            if (!alsa_mixer.empty() && !AlsaAudioSink::probe_mixer(alsa_mixer, error)) {
                return nullptr;
            }
            MixerCurve curve = MixerCurve::kSpec;
            parse_mixer_curve(alsa_mixer_curve, curve);
            return std::make_unique<AlsaAudioSink>(spec.device, buffer_ms, alsa_mixer, curve);
#else
            break;
#endif
        }
    }

    error = "internal error: output device '" + device +
            "' resolved to a backend this build cannot construct";
    return nullptr;
}

void print_audio_devices(std::FILE* out) {
    std::fprintf(out, "Output devices (-o):\n");
    std::fprintf(out, "  null      discard audio; needs no sound card at all\n");
    std::fprintf(out, "  stdout    raw interleaved PCM on stdout, e.g. | aplay -f cd\n");
    std::fprintf(out, "  -         alias for stdout\n");
#ifdef BT_SPEAKER_CLI_HAVE_PULSE
    std::fprintf(out, "  pulse     the PulseAudio server's own default sink\n");
#endif
#ifdef BT_SPEAKER_CLI_HAVE_PIPEWIRE
    std::fprintf(out, "  pipewire  wherever the PipeWire graph routes a playback stream\n");
#endif

    std::fprintf(out,
                 "\nHow -o reads its argument, in this order:\n"
                 "  1. one of the reserved names above;\n"
                 "  2. <backend>:<device>, split on the FIRST colon, where <backend> is one\n"
                 "     of: %s;\n",
                 audio_backend_list().c_str());
#ifdef BT_SPEAKER_CLI_HAVE_ALSA
    std::fprintf(out,
                 "  3. anything else is an ALSA PCM name, e.g. -o hw:1,0 or -o default.\n");
#else
    std::fprintf(out, "  3. anything else fails without an ALSA backend.\n");
#endif

#ifdef BT_SPEAKER_CLI_HAVE_PULSE
    std::fprintf(out, "\nPulseAudio sinks on this host (-o pulse:<sink>):\n");
    PulseAudioSink::list_devices(out);
#endif

#ifdef BT_SPEAKER_CLI_HAVE_PIPEWIRE
    std::fprintf(out, "\nPipeWire audio sink nodes on this host (-o pipewire:<node>):\n");
    PipeWireSink::list_devices(out);
#endif

#ifdef BT_SPEAKER_CLI_HAVE_ALSA
    std::fprintf(out, "\nALSA PCMs on this host:\n");
    AlsaAudioSink::list_devices(out);
#endif
}

}  // namespace bt_speaker_cli
