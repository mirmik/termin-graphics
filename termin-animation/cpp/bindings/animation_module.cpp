#include <nanobind/nanobind.h>
#include <nanobind/stl/string.h>
#include <nanobind/stl/unordered_map.h>
#include <nanobind/stl/vector.h>

#include "termin/animation/tc_animation_handle.hpp"
#include "termin/inspect/tc_kind.hpp"
#include <tcbase/tc_log.hpp>

namespace nb = nanobind;
using namespace termin;
using namespace termin::animation;

void bind_tc_animation_clip(nb::module_& m) {
    nb::class_<TcAnimationClip>(m, "TcAnimationClip")
        .def(nb::init<>())
        .def_static("from_uuid", &TcAnimationClip::from_uuid, nb::arg("uuid"))
        .def_static("get_or_create", &TcAnimationClip::get_or_create, nb::arg("uuid"))
        .def_static("create", &TcAnimationClip::create,
            nb::arg("name") = "", nb::arg("uuid_hint") = "")
        .def_prop_ro("is_valid", &TcAnimationClip::is_valid)
        .def_prop_ro("uuid", &TcAnimationClip::uuid)
        .def_prop_ro("name", &TcAnimationClip::name)
        .def_prop_ro("version", &TcAnimationClip::version)
        .def_prop_ro("duration", &TcAnimationClip::duration)
        .def_prop_ro("tps", &TcAnimationClip::tps)
        .def_prop_ro("channel_count", &TcAnimationClip::channel_count)
        .def_prop_ro("loop", &TcAnimationClip::loop)
        .def("set_tps", &TcAnimationClip::set_tps, nb::arg("value"))
        .def("set_loop", &TcAnimationClip::set_loop, nb::arg("value"))
        .def("ensure_loaded", &TcAnimationClip::ensure_loaded)
        .def("recompute_duration", &TcAnimationClip::recompute_duration)
        .def("bump_version", &TcAnimationClip::bump_version)
        .def("find_channel", &TcAnimationClip::find_channel, nb::arg("target_name"))
        .def("sample", [](const TcAnimationClip& self, double t_seconds) {
            // Return list of dicts with sample data
            tc_animation* anim = self.get();
            if (!anim || anim->channel_count == 0) {
                return nb::list();
            }

            std::vector<tc_channel_sample> samples(anim->channel_count);
            tc_animation_sample(anim, t_seconds, samples.data());

            nb::list result;
            for (size_t i = 0; i < anim->channel_count; i++) {
                nb::dict ch_dict;
                ch_dict["target_name"] = anim->channels[i].target_name;

                const tc_channel_sample& s = samples[i];
                if (s.has_translation) {
                    nb::list tr;
                    tr.append(s.translation[0]);
                    tr.append(s.translation[1]);
                    tr.append(s.translation[2]);
                    ch_dict["translation"] = tr;
                } else {
                    ch_dict["translation"] = nb::none();
                }

                if (s.has_rotation) {
                    nb::list rot;
                    rot.append(s.rotation[0]);
                    rot.append(s.rotation[1]);
                    rot.append(s.rotation[2]);
                    rot.append(s.rotation[3]);
                    ch_dict["rotation"] = rot;
                } else {
                    ch_dict["rotation"] = nb::none();
                }

                if (s.has_scale) {
                    ch_dict["scale"] = s.scale;
                } else {
                    ch_dict["scale"] = nb::none();
                }

                result.append(ch_dict);
            }
            return result;
        }, nb::arg("t_seconds"))
        .def("serialize", [](const TcAnimationClip& self) {
            nb::dict d;
            if (!self.is_valid()) {
                d["type"] = "none";
                return d;
            }
            d["uuid"] = self.uuid();
            d["name"] = self.name();
            d["type"] = "uuid";
            return d;
        })
        // Set channels from Python data
        // channels_data: list of dicts with:
        //   - target_name: str
        //   - translation_keys: list of (time, [x, y, z])
        //   - rotation_keys: list of (time, [x, y, z, w])
        //   - scale_keys: list of (time, value)
        .def("set_channels", [](TcAnimationClip& self, nb::list channels_data) {
            tc_animation* anim = self.get();
            if (!anim) {
                tc::Log::error("TcAnimationClip::set_channels: invalid clip");
                return;
            }

            size_t count = nb::len(channels_data);
            tc_animation_channel* channels = tc_animation_alloc_channels(anim, count);
            if (!channels) {
                tc::Log::error("TcAnimationClip::set_channels: failed to allocate %zu channels", count);
                return;
            }

            for (size_t i = 0; i < count; i++) {
                nb::dict ch_data = nb::cast<nb::dict>(channels_data[i]);
                tc_animation_channel* ch = &channels[i];

                // Target name
                if (ch_data.contains("target_name")) {
                    std::string target = nb::cast<std::string>(ch_data["target_name"]);
                    strncpy(ch->target_name, target.c_str(), TC_CHANNEL_NAME_MAX - 1);
                    ch->target_name[TC_CHANNEL_NAME_MAX - 1] = '\0';
                }

                double max_time = 0.0;

                // Translation keys
                if (ch_data.contains("translation_keys")) {
                    nb::list tr_keys = nb::cast<nb::list>(ch_data["translation_keys"]);
                    size_t tr_count = nb::len(tr_keys);
                    if (tr_count > 0) {
                        tc_keyframe_vec3* keys = tc_animation_channel_alloc_translation(ch, tr_count);
                        for (size_t j = 0; j < tr_count; j++) {
                            nb::tuple kf = nb::cast<nb::tuple>(tr_keys[j]);
                            keys[j].time = nb::cast<double>(kf[0]);
                            nb::object val = kf[1];
                            keys[j].value[0] = nb::cast<double>(val[nb::int_(0)]);
                            keys[j].value[1] = nb::cast<double>(val[nb::int_(1)]);
                            keys[j].value[2] = nb::cast<double>(val[nb::int_(2)]);
                            if (keys[j].time > max_time) max_time = keys[j].time;
                        }
                    }
                }

                // Rotation keys
                if (ch_data.contains("rotation_keys")) {
                    nb::list rot_keys = nb::cast<nb::list>(ch_data["rotation_keys"]);
                    size_t rot_count = nb::len(rot_keys);
                    if (rot_count > 0) {
                        tc_keyframe_quat* keys = tc_animation_channel_alloc_rotation(ch, rot_count);
                        for (size_t j = 0; j < rot_count; j++) {
                            nb::tuple kf = nb::cast<nb::tuple>(rot_keys[j]);
                            keys[j].time = nb::cast<double>(kf[0]);
                            nb::object val = kf[1];
                            keys[j].value[0] = nb::cast<double>(val[nb::int_(0)]);
                            keys[j].value[1] = nb::cast<double>(val[nb::int_(1)]);
                            keys[j].value[2] = nb::cast<double>(val[nb::int_(2)]);
                            keys[j].value[3] = nb::cast<double>(val[nb::int_(3)]);
                            if (keys[j].time > max_time) max_time = keys[j].time;
                        }
                    }
                }

                // Scale keys
                if (ch_data.contains("scale_keys")) {
                    nb::list sc_keys = nb::cast<nb::list>(ch_data["scale_keys"]);
                    size_t sc_count = nb::len(sc_keys);
                    if (sc_count > 0) {
                        tc_keyframe_scalar* keys = tc_animation_channel_alloc_scale(ch, sc_count);
                        for (size_t j = 0; j < sc_count; j++) {
                            nb::tuple kf = nb::cast<nb::tuple>(sc_keys[j]);
                            keys[j].time = nb::cast<double>(kf[0]);
                            keys[j].value = nb::cast<double>(kf[1]);
                            if (keys[j].time > max_time) max_time = keys[j].time;
                        }
                    }
                }

                ch->duration = max_time;
            }

            tc_animation_recompute_duration(anim);
        }, nb::arg("channels_data"));
}

void register_animation_kind_handlers() {
    // C++ handler for C++ fields
    tc::register_cpp_handle_kind<TcAnimationClip>("tc_animation_clip");

    // Python handler for Python fields
    tc::KindRegistry::instance().register_python(
        "tc_animation_clip",
        // serialize
        nb::cpp_function([](nb::object obj) -> nb::object {
            TcAnimationClip clip = nb::cast<TcAnimationClip>(obj);
            nb::dict d;
            if (!clip.is_valid()) {
                d["type"] = "none";
                return d;
            }
            d["uuid"] = clip.uuid();
            d["name"] = clip.name();
            d["type"] = "uuid";
            return d;
        }),
        // deserialize
        nb::cpp_function([](nb::object data) -> nb::object {
            // Handle UUID string
            if (nb::isinstance<nb::str>(data)) {
                return nb::cast(TcAnimationClip::from_uuid(nb::cast<std::string>(data)));
            }
            // Handle dict format
            if (nb::isinstance<nb::dict>(data)) {
                nb::dict d = nb::cast<nb::dict>(data);
                if (d.contains("uuid")) {
                    std::string uuid = nb::cast<std::string>(d["uuid"]);
                    return nb::cast(TcAnimationClip::from_uuid(uuid));
                }
            }
            return nb::cast(TcAnimationClip());
        })
    );
}

NB_MODULE(_animation_native, m) {
    m.doc() = "Native C++ animation module for termin";

    bind_tc_animation_clip(m);

    // Register kind handlers for serialization
    register_animation_kind_handlers();
}
