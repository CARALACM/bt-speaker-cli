// Copyright 2026 CARALACM
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

#include "bt_manager.h"

#include "clock.h"
#include "log.h"

#ifdef BT_SPEAKER_CLI_HAVE_DBUS
#include <dbus/dbus.h>
#endif

#include <algorithm>
#include <cctype>
#include <cstring>
#include <map>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace bt_speaker_cli {

static constexpr const char* LOG_TAG = LOG_TAG_BT;

#ifdef BT_SPEAKER_CLI_HAVE_DBUS

namespace {

constexpr const char* BLUEZ_SERVICE = "org.bluez";
constexpr const char* BLUEZ_ROOT_PATH = "/org/bluez";
constexpr const char* AGENT_PATH = "/org/bt_speaker_cli/agent";

constexpr const char* IFACE_OBJECT_MANAGER = "org.freedesktop.DBus.ObjectManager";
constexpr const char* IFACE_PROPERTIES = "org.freedesktop.DBus.Properties";
constexpr const char* IFACE_INTROSPECTABLE = "org.freedesktop.DBus.Introspectable";
constexpr const char* IFACE_ADAPTER = "org.bluez.Adapter1";
constexpr const char* IFACE_DEVICE = "org.bluez.Device1";
constexpr const char* IFACE_AGENT_MANAGER = "org.bluez.AgentManager1";
constexpr const char* IFACE_AGENT = "org.bluez.Agent1";
constexpr const char* IFACE_MEDIA_PLAYER = "org.bluez.MediaPlayer1";
constexpr const char* IFACE_MEDIA_TRANSPORT = "org.bluez.MediaTransport1";

constexpr int DBUS_SHORT_TIMEOUT_MS = 1000;
constexpr int64_t SETUP_RETRY_INTERVAL_MS = 3000;
constexpr int64_t PAIRING_TIMEOUT_MS = 60000;

constexpr const char* AGENT_INTROSPECTION_XML =
    "<!DOCTYPE node PUBLIC \"-//freedesktop//DTD D-BUS Object Introspection 1.0//EN\" "
    "\"http://www.freedesktop.org/standards/dbus/1.0/introspect.dtd\">\n"
    "<node>\n"
    "  <interface name=\"org.bluez.Agent1\">\n"
    "    <method name=\"Release\"/>\n"
    "    <method name=\"RequestPinCode\">\n"
    "      <arg type=\"o\" name=\"device\" direction=\"in\"/>\n"
    "      <arg type=\"s\" name=\"pincode\" direction=\"out\"/>\n"
    "    </method>\n"
    "    <method name=\"DisplayPinCode\">\n"
    "      <arg type=\"o\" name=\"device\" direction=\"in\"/>\n"
    "      <arg type=\"s\" name=\"pincode\" direction=\"in\"/>\n"
    "    </method>\n"
    "    <method name=\"RequestPasskey\">\n"
    "      <arg type=\"o\" name=\"device\" direction=\"in\"/>\n"
    "      <arg type=\"u\" name=\"passkey\" direction=\"out\"/>\n"
    "    </method>\n"
    "    <method name=\"DisplayPasskey\">\n"
    "      <arg type=\"o\" name=\"device\" direction=\"in\"/>\n"
    "      <arg type=\"u\" name=\"passkey\" direction=\"in\"/>\n"
    "      <arg type=\"q\" name=\"entered\" direction=\"in\"/>\n"
    "    </method>\n"
    "    <method name=\"RequestConfirmation\">\n"
    "      <arg type=\"o\" name=\"device\" direction=\"in\"/>\n"
    "      <arg type=\"u\" name=\"passkey\" direction=\"in\"/>\n"
    "    </method>\n"
    "    <method name=\"RequestAuthorization\">\n"
    "      <arg type=\"o\" name=\"device\" direction=\"in\"/>\n"
    "    </method>\n"
    "    <method name=\"AuthorizeService\">\n"
    "      <arg type=\"o\" name=\"device\" direction=\"in\"/>\n"
    "      <arg type=\"s\" name=\"uuid\" direction=\"in\"/>\n"
    "    </method>\n"
    "    <method name=\"Cancel\"/>\n"
    "  </interface>\n"
    "</node>\n";

std::string to_lower_ascii(std::string s) {
    for (char& c : s) {
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    return s;
}

bool is_audio_service_uuid(const std::string& uuid) {
    const std::string lower = to_lower_ascii(uuid);
    return lower.rfind("0000110a", 0) == 0 ||  // A2DP AudioSource
           lower.rfind("0000110b", 0) == 0 ||  // A2DP AudioSink
           lower.rfind("0000110c", 0) == 0 ||  // AVRCP Remote Control Target
           lower.rfind("0000110d", 0) == 0 ||  // Advanced Audio Distribution
           lower.rfind("0000110e", 0) == 0 ||  // AVRCP Remote Control
           lower.rfind("0000110f", 0) == 0;    // AVRCP Remote Control Controller
}

std::string adapter_short_name(const std::string& path) {
    const size_t slash = path.find_last_of('/');
    return slash == std::string::npos ? path : path.substr(slash + 1);
}

bool read_string_variant(DBusMessageIter* variant, std::string& out) {
    if (dbus_message_iter_get_arg_type(variant) != DBUS_TYPE_STRING &&
        dbus_message_iter_get_arg_type(variant) != DBUS_TYPE_OBJECT_PATH) {
        return false;
    }
    const char* val = nullptr;
    dbus_message_iter_get_basic(variant, &val);
    out = val != nullptr ? val : "";
    return true;
}

bool read_bool_variant(DBusMessageIter* variant, bool& out) {
    if (dbus_message_iter_get_arg_type(variant) != DBUS_TYPE_BOOLEAN) {
        return false;
    }
    dbus_bool_t val = FALSE;
    dbus_message_iter_get_basic(variant, &val);
    out = val != FALSE;
    return true;
}

bool read_uint16_variant(DBusMessageIter* variant, uint16_t& out) {
    if (dbus_message_iter_get_arg_type(variant) != DBUS_TYPE_UINT16) {
        return false;
    }
    dbus_uint16_t val = 0;
    dbus_message_iter_get_basic(variant, &val);
    out = val;
    return true;
}

bool read_uint32_variant(DBusMessageIter* variant, uint32_t& out) {
    if (dbus_message_iter_get_arg_type(variant) != DBUS_TYPE_UINT32) {
        return false;
    }
    dbus_uint32_t val = 0;
    dbus_message_iter_get_basic(variant, &val);
    out = val;
    return true;
}

bool read_int16_variant(DBusMessageIter* variant, int16_t& out) {
    if (dbus_message_iter_get_arg_type(variant) != DBUS_TYPE_INT16) {
        return false;
    }
    dbus_int16_t val = 0;
    dbus_message_iter_get_basic(variant, &val);
    out = val;
    return true;
}

uint8_t transport_vol_to_percent(uint16_t raw) {
    const uint16_t clamped = std::min<uint16_t>(raw, 127);
    return static_cast<uint8_t>((static_cast<uint32_t>(clamped) * 100U + 63U) / 127U);
}

uint16_t percent_to_transport_vol(uint8_t percent) {
    const uint8_t clamped = std::min<uint8_t>(percent, 100);
    return static_cast<uint16_t>((static_cast<uint32_t>(clamped) * 127U + 50U) / 100U);
}

bool set_dbus_property_bool(DBusConnection* conn, const std::string& path, const char* iface,
                            const char* prop, bool value) {
    DBusMessage* msg =
        dbus_message_new_method_call(BLUEZ_SERVICE, path.c_str(), IFACE_PROPERTIES, "Set");
    if (msg == nullptr) {
        return false;
    }
    DBusMessageIter iter;
    dbus_message_iter_init_append(msg, &iter);
    dbus_message_iter_append_basic(&iter, DBUS_TYPE_STRING, &iface);
    dbus_message_iter_append_basic(&iter, DBUS_TYPE_STRING, &prop);

    DBusMessageIter variant;
    dbus_message_iter_open_container(&iter, DBUS_TYPE_VARIANT, "b", &variant);
    dbus_bool_t v = value ? TRUE : FALSE;
    dbus_message_iter_append_basic(&variant, DBUS_TYPE_BOOLEAN, &v);
    dbus_message_iter_close_container(&iter, &variant);

    DBusError err;
    dbus_error_init(&err);
    DBusMessage* reply =
        dbus_connection_send_with_reply_and_block(conn, msg, DBUS_SHORT_TIMEOUT_MS, &err);
    dbus_message_unref(msg);
    if (reply != nullptr) {
        dbus_message_unref(reply);
    }
    if (dbus_error_is_set(&err)) {
        cli_log(LogLevel::DEBUG, "Failed to set %s.%s on %s: %s", iface, prop, path.c_str(),
                err.message);
        dbus_error_free(&err);
        return false;
    }
    return true;
}

bool set_dbus_property_uint32(DBusConnection* conn, const std::string& path, const char* iface,
                              const char* prop, uint32_t value) {
    DBusMessage* msg =
        dbus_message_new_method_call(BLUEZ_SERVICE, path.c_str(), IFACE_PROPERTIES, "Set");
    if (msg == nullptr) {
        return false;
    }
    DBusMessageIter iter;
    dbus_message_iter_init_append(msg, &iter);
    dbus_message_iter_append_basic(&iter, DBUS_TYPE_STRING, &iface);
    dbus_message_iter_append_basic(&iter, DBUS_TYPE_STRING, &prop);

    DBusMessageIter variant;
    dbus_message_iter_open_container(&iter, DBUS_TYPE_VARIANT, "u", &variant);
    dbus_uint32_t v = value;
    dbus_message_iter_append_basic(&variant, DBUS_TYPE_UINT32, &v);
    dbus_message_iter_close_container(&iter, &variant);

    DBusError err;
    dbus_error_init(&err);
    DBusMessage* reply =
        dbus_connection_send_with_reply_and_block(conn, msg, DBUS_SHORT_TIMEOUT_MS, &err);
    dbus_message_unref(msg);
    if (reply != nullptr) {
        dbus_message_unref(reply);
    }
    if (dbus_error_is_set(&err)) {
        dbus_error_free(&err);
        return false;
    }
    return true;
}

bool set_dbus_property_uint16(DBusConnection* conn, const std::string& path, const char* iface,
                              const char* prop, uint16_t value) {
    DBusMessage* msg =
        dbus_message_new_method_call(BLUEZ_SERVICE, path.c_str(), IFACE_PROPERTIES, "Set");
    if (msg == nullptr) {
        return false;
    }
    DBusMessageIter iter;
    dbus_message_iter_init_append(msg, &iter);
    dbus_message_iter_append_basic(&iter, DBUS_TYPE_STRING, &iface);
    dbus_message_iter_append_basic(&iter, DBUS_TYPE_STRING, &prop);

    DBusMessageIter variant;
    dbus_message_iter_open_container(&iter, DBUS_TYPE_VARIANT, "q", &variant);
    dbus_uint16_t v = value;
    dbus_message_iter_append_basic(&variant, DBUS_TYPE_UINT16, &v);
    dbus_message_iter_close_container(&iter, &variant);

    const bool ok = dbus_connection_send(conn, msg, nullptr) != FALSE;
    dbus_message_unref(msg);
    return ok;
}

bool set_dbus_property_string(DBusConnection* conn, const std::string& path, const char* iface,
                              const char* prop, const std::string& value) {
    DBusMessage* msg =
        dbus_message_new_method_call(BLUEZ_SERVICE, path.c_str(), IFACE_PROPERTIES, "Set");
    if (msg == nullptr) {
        return false;
    }
    DBusMessageIter iter;
    dbus_message_iter_init_append(msg, &iter);
    dbus_message_iter_append_basic(&iter, DBUS_TYPE_STRING, &iface);
    dbus_message_iter_append_basic(&iter, DBUS_TYPE_STRING, &prop);

    DBusMessageIter variant;
    dbus_message_iter_open_container(&iter, DBUS_TYPE_VARIANT, "s", &variant);
    const char* v = value.c_str();
    dbus_message_iter_append_basic(&variant, DBUS_TYPE_STRING, &v);
    dbus_message_iter_close_container(&iter, &variant);

    DBusError err;
    dbus_error_init(&err);
    DBusMessage* reply =
        dbus_connection_send_with_reply_and_block(conn, msg, DBUS_SHORT_TIMEOUT_MS, &err);
    dbus_message_unref(msg);
    if (reply != nullptr) {
        dbus_message_unref(reply);
    }
    if (dbus_error_is_set(&err)) {
        cli_log(LogLevel::DEBUG, "Failed to set %s.%s on %s: %s", iface, prop, path.c_str(),
                err.message);
        dbus_error_free(&err);
        return false;
    }
    return true;
}

}  // namespace

struct BtManager::Impl {
    explicit Impl(BtManagerConfig cfg) : config(std::move(cfg)) {}

    ~Impl() {
        this->cleanup();
    }

    BtManagerConfig config;
    std::string remembered_device;
    std::function<void(const std::string& mac)> on_device_connected;
    std::function<void(uint8_t volume)> on_transport_volume_changed;

    DBusConnection* conn{nullptr};
    bool agent_registered{false};
    bool object_path_registered{false};
    bool match_rules_added{false};
    bool discoverable_desired{true};

    std::vector<BtAdapterInfo> adapters;
    std::string active_adapter_path;
    std::map<std::string, BtDeviceInfo> devices_by_path;
    std::string media_player_path;
    std::string media_transport_path;
    BtMediaInfo media;

    BtReconnectPacer pacer;
    int64_t next_setup_ms{0};
    bool bluez_missing_logged{false};
    bool adapter_missing_logged{false};

    DBusPendingCall* pending_connect{nullptr};
    std::string pending_connect_mac;

    DBusMessage* pending_pairing_msg{nullptr};
    std::string pending_pairing_device_path;
    BtPendingPairing pending_pairing;
    int64_t pending_pairing_deadline_ms{0};

    static DBusHandlerResult message_filter(DBusConnection* conn, DBusMessage* msg,
                                            void* userdata);
    static DBusHandlerResult agent_message_handler(DBusConnection* conn, DBusMessage* msg,
                                                   void* userdata);

    bool ensure_connected();
    bool setup_bluez(int64_t now_ms);
    bool register_agent();
    void unregister_agent();
    bool refresh_managed_objects();
    void apply_adapter_settings();
    void select_active_adapter();
    void parse_interface_properties(const std::string& path, const std::string& iface,
                                    DBusMessageIter* props_dict);
    void handle_interfaces_added(DBusMessage* msg);
    void handle_interfaces_removed(DBusMessage* msg);
    void handle_properties_changed(DBusMessage* msg);
    void handle_signal(DBusMessage* msg);
    void check_pending_connect();
    void check_pending_pairing_timeout(int64_t now_ms);
    void resolve_pending_pairing(bool confirm, const char* reject_reason = "Rejected by operator");
    bool start_async_connect(const std::string& path, const std::string& mac);
    void cleanup();
    const BtDeviceInfo* find_device_by_mac(const std::string& mac) const;
    bool adapter_matches(const BtAdapterInfo& info) const;
};

bool BtManager::Impl::adapter_matches(const BtAdapterInfo& info) const {
    if (this->config.adapter.empty()) {
        return true;
    }
    if (info.name == this->config.adapter || info.object_path == this->config.adapter) {
        return true;
    }
    std::string norm;
    if (normalize_mac_address(this->config.adapter, norm)) {
        std::string info_norm;
        return normalize_mac_address(info.address, info_norm) && info_norm == norm;
    }
    return false;
}

const BtDeviceInfo* BtManager::Impl::find_device_by_mac(const std::string& mac) const {
    std::string norm;
    if (!normalize_mac_address(mac, norm)) {
        return nullptr;
    }
    for (const auto& [path, dev] : this->devices_by_path) {
        if (dev.address == norm) {
            return &dev;
        }
    }
    return nullptr;
}

void BtManager::Impl::cleanup() {
    this->resolve_pending_pairing(false, "Daemon shutting down");
    if (this->pending_connect != nullptr) {
        dbus_pending_call_cancel(this->pending_connect);
        dbus_pending_call_unref(this->pending_connect);
        this->pending_connect = nullptr;
    }
    this->unregister_agent();
    if (this->conn != nullptr) {
        if (this->object_path_registered) {
            dbus_connection_unregister_object_path(this->conn, AGENT_PATH);
            this->object_path_registered = false;
        }
        dbus_connection_remove_filter(this->conn, &Impl::message_filter, this);
        dbus_connection_unref(this->conn);
        this->conn = nullptr;
    }
    this->match_rules_added = false;
}

bool BtManager::Impl::ensure_connected() {
    if (this->config.no_dbus) {
        return false;
    }
    if (this->conn != nullptr && dbus_connection_get_is_connected(this->conn)) {
        return true;
    }
    if (this->conn != nullptr) {
        this->cleanup();
    }

    DBusError err;
    dbus_error_init(&err);
    this->conn = dbus_bus_get_private(DBUS_BUS_SYSTEM, &err);
    if (this->conn == nullptr) {
        if (!this->bluez_missing_logged) {
            log_line(LogLevel::WARN, LOG_TAG,
                     "Cannot connect to system D-Bus (%s) -- retrying in background",
                     dbus_error_is_set(&err) ? err.message : "unknown error");
            this->bluez_missing_logged = true;
        }
        if (dbus_error_is_set(&err)) {
            dbus_error_free(&err);
        }
        return false;
    }
    if (dbus_error_is_set(&err)) {
        dbus_error_free(&err);
    }

    dbus_connection_set_exit_on_disconnect(this->conn, FALSE);
    dbus_connection_add_filter(this->conn, &Impl::message_filter, this, nullptr);

    static const DBusObjectPathVTable agent_vtable = {
        nullptr,
        &Impl::agent_message_handler,
        nullptr,
        nullptr,
        nullptr,
        nullptr,
    };
    if (!dbus_connection_register_object_path(this->conn, AGENT_PATH, &agent_vtable, this)) {
        log_line(LogLevel::ERROR, LOG_TAG, "Cannot register BlueZ agent object path");
        this->cleanup();
        return false;
    }
    this->object_path_registered = true;

    dbus_bus_add_match(this->conn,
                       "type='signal',sender='org.bluez',interface='org.freedesktop.DBus."
                       "ObjectManager'",
                       nullptr);
    dbus_bus_add_match(this->conn,
                       "type='signal',sender='org.bluez',interface='org.freedesktop.DBus."
                       "Properties',member='PropertiesChanged'",
                       nullptr);
    dbus_bus_add_match(this->conn,
                       "type='signal',sender='org.freedesktop.DBus',interface='org.freedesktop."
                       "DBus',member='NameOwnerChanged',arg0='org.bluez'",
                       nullptr);
    dbus_connection_flush(this->conn);
    this->match_rules_added = true;
    return true;
}

bool BtManager::Impl::register_agent() {
    if (this->agent_registered || this->conn == nullptr) {
        return this->agent_registered;
    }
    const char* capability = this->config.allow_unpaired ? "NoInputNoOutput" : "DisplayYesNo";

    DBusMessage* msg =
        dbus_message_new_method_call(BLUEZ_SERVICE, BLUEZ_ROOT_PATH, IFACE_AGENT_MANAGER,
                                     "RegisterAgent");
    if (msg == nullptr) {
        return false;
    }
    const char* path = AGENT_PATH;
    dbus_message_append_args(msg, DBUS_TYPE_OBJECT_PATH, &path, DBUS_TYPE_STRING, &capability,
                             DBUS_TYPE_INVALID);

    DBusError err;
    dbus_error_init(&err);
    DBusMessage* reply =
        dbus_connection_send_with_reply_and_block(this->conn, msg, DBUS_SHORT_TIMEOUT_MS, &err);
    dbus_message_unref(msg);
    if (reply != nullptr) {
        dbus_message_unref(reply);
    }
    if (dbus_error_is_set(&err)) {
        const bool already = std::strcmp(err.name, "org.bluez.Error.AlreadyExists") == 0;
        if (!already) {
            if (!this->bluez_missing_logged) {
                log_line(LogLevel::WARN, LOG_TAG_AGENT,
                         "BlueZ not ready for agent registration (%s) -- waiting", err.message);
                this->bluez_missing_logged = true;
            }
            dbus_error_free(&err);
            return false;
        }
        dbus_error_free(&err);
    }

    DBusMessage* req_default =
        dbus_message_new_method_call(BLUEZ_SERVICE, BLUEZ_ROOT_PATH, IFACE_AGENT_MANAGER,
                                     "RequestDefaultAgent");
    if (req_default != nullptr) {
        dbus_message_append_args(req_default, DBUS_TYPE_OBJECT_PATH, &path, DBUS_TYPE_INVALID);
        DBusError def_err;
        dbus_error_init(&def_err);
        DBusMessage* def_reply = dbus_connection_send_with_reply_and_block(
            this->conn, req_default, DBUS_SHORT_TIMEOUT_MS, &def_err);
        dbus_message_unref(req_default);
        if (def_reply != nullptr) {
            dbus_message_unref(def_reply);
        }
        if (dbus_error_is_set(&def_err)) {
            log_line(LogLevel::WARN, LOG_TAG_AGENT, "RequestDefaultAgent failed: %s",
                     def_err.message);
            dbus_error_free(&def_err);
        }
    }

    this->agent_registered = true;
    this->bluez_missing_logged = false;
    log_line(LogLevel::INFO, LOG_TAG_AGENT, "Registered BlueZ Agent1 (%s) at %s", capability,
             AGENT_PATH);
    return true;
}

void BtManager::Impl::unregister_agent() {
    if (!this->agent_registered || this->conn == nullptr) {
        this->agent_registered = false;
        return;
    }
    DBusMessage* msg = dbus_message_new_method_call(BLUEZ_SERVICE, BLUEZ_ROOT_PATH,
                                                    IFACE_AGENT_MANAGER, "UnregisterAgent");
    if (msg != nullptr) {
        const char* path = AGENT_PATH;
        dbus_message_append_args(msg, DBUS_TYPE_OBJECT_PATH, &path, DBUS_TYPE_INVALID);
        dbus_connection_send(this->conn, msg, nullptr);
        dbus_connection_flush(this->conn);
        dbus_message_unref(msg);
    }
    this->agent_registered = false;
}

void BtManager::Impl::parse_interface_properties(const std::string& path, const std::string& iface,
                                                 DBusMessageIter* props_dict) {
    if (iface == IFACE_ADAPTER) {
        BtAdapterInfo info;
        auto existing = std::find_if(this->adapters.begin(), this->adapters.end(),
                                     [&path](const BtAdapterInfo& a) { return a.object_path == path; });
        if (existing != this->adapters.end()) {
            info = *existing;
        } else {
            info.object_path = path;
            info.name = adapter_short_name(path);
        }

        DBusMessageIter entry;
        for (dbus_message_iter_recurse(props_dict, &entry);
             dbus_message_iter_get_arg_type(&entry) == DBUS_TYPE_DICT_ENTRY;
             dbus_message_iter_next(&entry)) {
            DBusMessageIter kv;
            dbus_message_iter_recurse(&entry, &kv);
            const char* key = nullptr;
            dbus_message_iter_get_basic(&kv, &key);
            if (!dbus_message_iter_next(&kv) || key == nullptr) {
                continue;
            }
            DBusMessageIter variant;
            dbus_message_iter_recurse(&kv, &variant);
            if (std::strcmp(key, "Address") == 0) {
                read_string_variant(&variant, info.address);
            } else if (std::strcmp(key, "Alias") == 0) {
                read_string_variant(&variant, info.alias);
            } else if (std::strcmp(key, "Powered") == 0) {
                read_bool_variant(&variant, info.powered);
            } else if (std::strcmp(key, "Discoverable") == 0) {
                read_bool_variant(&variant, info.discoverable);
            } else if (std::strcmp(key, "Pairable") == 0) {
                read_bool_variant(&variant, info.pairable);
            }
        }
        if (existing != this->adapters.end()) {
            *existing = info;
        } else {
            this->adapters.push_back(std::move(info));
        }
        return;
    }

    if (iface == IFACE_DEVICE) {
        BtDeviceInfo& dev = this->devices_by_path[path];
        const bool was_connected = dev.connected;
        dev.object_path = path;

        DBusMessageIter entry;
        for (dbus_message_iter_recurse(props_dict, &entry);
             dbus_message_iter_get_arg_type(&entry) == DBUS_TYPE_DICT_ENTRY;
             dbus_message_iter_next(&entry)) {
            DBusMessageIter kv;
            dbus_message_iter_recurse(&entry, &kv);
            const char* key = nullptr;
            dbus_message_iter_get_basic(&kv, &key);
            if (!dbus_message_iter_next(&kv) || key == nullptr) {
                continue;
            }
            DBusMessageIter variant;
            dbus_message_iter_recurse(&kv, &variant);
            if (std::strcmp(key, "Address") == 0) {
                std::string raw;
                if (read_string_variant(&variant, raw)) {
                    std::string norm;
                    dev.address = normalize_mac_address(raw, norm) ? norm : raw;
                }
            } else if (std::strcmp(key, "Alias") == 0 && dev.name.empty()) {
                read_string_variant(&variant, dev.name);
            } else if (std::strcmp(key, "Name") == 0) {
                read_string_variant(&variant, dev.name);
            } else if (std::strcmp(key, "Paired") == 0) {
                read_bool_variant(&variant, dev.paired);
            } else if (std::strcmp(key, "Trusted") == 0) {
                read_bool_variant(&variant, dev.trusted);
            } else if (std::strcmp(key, "Connected") == 0) {
                read_bool_variant(&variant, dev.connected);
            } else if (std::strcmp(key, "RSSI") == 0) {
                read_int16_variant(&variant, dev.rssi);
            } else if (std::strcmp(key, "UUIDs") == 0) {
                if (dbus_message_iter_get_arg_type(&variant) == DBUS_TYPE_ARRAY) {
                    DBusMessageIter arr;
                    for (dbus_message_iter_recurse(&variant, &arr);
                         dbus_message_iter_get_arg_type(&arr) == DBUS_TYPE_STRING;
                         dbus_message_iter_next(&arr)) {
                        const char* u = nullptr;
                        dbus_message_iter_get_basic(&arr, &u);
                        if (u != nullptr && is_audio_service_uuid(u)) {
                            dev.has_audio_source = true;
                        }
                    }
                }
            }
        }

        if (!was_connected && dev.connected) {
            log_line(LogLevel::INFO, LOG_TAG, "Bluetooth device connected: %s (%s)",
                     dev.name.empty() ? "unknown" : dev.name.c_str(), dev.address.c_str());
            if (!dev.address.empty() && this->on_device_connected) {
                this->on_device_connected(dev.address);
            }
        } else if (was_connected && !dev.connected) {
            log_line(LogLevel::INFO, LOG_TAG, "Bluetooth device disconnected: %s (%s)",
                     dev.name.empty() ? "unknown" : dev.name.c_str(), dev.address.c_str());
            if (this->media_player_path.rfind(path + "/", 0) == 0) {
                this->media_player_path.clear();
                this->media.status.clear();
                this->media.title.clear();
                this->media.artist.clear();
                this->media.album.clear();
                this->media.position_ms.reset();
                this->media.duration_ms.reset();
            }
            if (this->media_transport_path.rfind(path + "/", 0) == 0) {
                this->media_transport_path.clear();
                this->media.transport_active = false;
                this->media.transport_volume.reset();
            }
        }
        return;
    }

    if (iface == IFACE_MEDIA_PLAYER) {
        this->media_player_path = path;
        DBusMessageIter entry;
        for (dbus_message_iter_recurse(props_dict, &entry);
             dbus_message_iter_get_arg_type(&entry) == DBUS_TYPE_DICT_ENTRY;
             dbus_message_iter_next(&entry)) {
            DBusMessageIter kv;
            dbus_message_iter_recurse(&entry, &kv);
            const char* key = nullptr;
            dbus_message_iter_get_basic(&kv, &key);
            if (!dbus_message_iter_next(&kv) || key == nullptr) {
                continue;
            }
            DBusMessageIter variant;
            dbus_message_iter_recurse(&kv, &variant);
            if (std::strcmp(key, "Status") == 0) {
                read_string_variant(&variant, this->media.status);
            } else if (std::strcmp(key, "Position") == 0) {
                uint32_t pos = 0;
                if (read_uint32_variant(&variant, pos)) {
                    this->media.position_ms = pos;
                }
            } else if (std::strcmp(key, "Track") == 0 &&
                       dbus_message_iter_get_arg_type(&variant) == DBUS_TYPE_ARRAY) {
                this->media.title.clear();
                this->media.artist.clear();
                this->media.album.clear();
                this->media.duration_ms.reset();
                DBusMessageIter t_entry;
                for (dbus_message_iter_recurse(&variant, &t_entry);
                     dbus_message_iter_get_arg_type(&t_entry) == DBUS_TYPE_DICT_ENTRY;
                     dbus_message_iter_next(&t_entry)) {
                    DBusMessageIter tkv;
                    dbus_message_iter_recurse(&t_entry, &tkv);
                    const char* tkey = nullptr;
                    dbus_message_iter_get_basic(&tkv, &tkey);
                    if (!dbus_message_iter_next(&tkv) || tkey == nullptr) {
                        continue;
                    }
                    DBusMessageIter tval;
                    dbus_message_iter_recurse(&tkv, &tval);
                    if (std::strcmp(tkey, "Title") == 0) {
                        read_string_variant(&tval, this->media.title);
                    } else if (std::strcmp(tkey, "Artist") == 0) {
                        read_string_variant(&tval, this->media.artist);
                    } else if (std::strcmp(tkey, "Album") == 0) {
                        read_string_variant(&tval, this->media.album);
                    } else if (std::strcmp(tkey, "Duration") == 0) {
                        uint32_t dur = 0;
                        if (read_uint32_variant(&tval, dur)) {
                            this->media.duration_ms = dur;
                        }
                    }
                }
                if (!this->media.title.empty()) {
                    log_line(LogLevel::INFO, LOG_TAG_AVRCP, "Now playing: %s - %s",
                             this->media.artist.empty() ? "Unknown artist"
                                                        : this->media.artist.c_str(),
                             this->media.title.c_str());
                }
            }
        }
        return;
    }

    if (iface == IFACE_MEDIA_TRANSPORT) {
        this->media_transport_path = path;
        DBusMessageIter entry;
        for (dbus_message_iter_recurse(props_dict, &entry);
             dbus_message_iter_get_arg_type(&entry) == DBUS_TYPE_DICT_ENTRY;
             dbus_message_iter_next(&entry)) {
            DBusMessageIter kv;
            dbus_message_iter_recurse(&entry, &kv);
            const char* key = nullptr;
            dbus_message_iter_get_basic(&kv, &key);
            if (!dbus_message_iter_next(&kv) || key == nullptr) {
                continue;
            }
            DBusMessageIter variant;
            dbus_message_iter_recurse(&kv, &variant);
            if (std::strcmp(key, "State") == 0) {
                std::string state;
                if (read_string_variant(&variant, state)) {
                    this->media.transport_active = (state == "active" || state == "pending");
                }
            } else if (std::strcmp(key, "Volume") == 0) {
                uint16_t raw = 0;
                if (read_uint16_variant(&variant, raw)) {
                    const uint8_t pct = transport_vol_to_percent(raw);
                    const bool changed =
                        !this->media.transport_volume.has_value() ||
                        *this->media.transport_volume != pct;
                    this->media.transport_volume = pct;
                    if (changed && this->on_transport_volume_changed) {
                        this->on_transport_volume_changed(pct);
                    }
                }
            }
        }
    }
}

bool BtManager::Impl::refresh_managed_objects() {
    if (this->conn == nullptr) {
        return false;
    }
    DBusMessage* msg =
        dbus_message_new_method_call(BLUEZ_SERVICE, "/", IFACE_OBJECT_MANAGER,
                                     "GetManagedObjects");
    if (msg == nullptr) {
        return false;
    }
    DBusError err;
    dbus_error_init(&err);
    DBusMessage* reply =
        dbus_connection_send_with_reply_and_block(this->conn, msg, DBUS_SHORT_TIMEOUT_MS, &err);
    dbus_message_unref(msg);
    if (reply == nullptr) {
        if (dbus_error_is_set(&err)) {
            dbus_error_free(&err);
        }
        return false;
    }

    this->adapters.clear();
    this->devices_by_path.clear();
    this->media_player_path.clear();
    this->media_transport_path.clear();

    DBusMessageIter iter;
    if (dbus_message_iter_init(reply, &iter) &&
        dbus_message_iter_get_arg_type(&iter) == DBUS_TYPE_ARRAY) {
        DBusMessageIter obj_entry;
        for (dbus_message_iter_recurse(&iter, &obj_entry);
             dbus_message_iter_get_arg_type(&obj_entry) == DBUS_TYPE_DICT_ENTRY;
             dbus_message_iter_next(&obj_entry)) {
            DBusMessageIter okv;
            dbus_message_iter_recurse(&obj_entry, &okv);
            const char* obj_path = nullptr;
            dbus_message_iter_get_basic(&okv, &obj_path);
            if (!dbus_message_iter_next(&okv) || obj_path == nullptr) {
                continue;
            }
            if (dbus_message_iter_get_arg_type(&okv) != DBUS_TYPE_ARRAY) {
                continue;
            }
            DBusMessageIter iface_entry;
            for (dbus_message_iter_recurse(&okv, &iface_entry);
                 dbus_message_iter_get_arg_type(&iface_entry) == DBUS_TYPE_DICT_ENTRY;
                 dbus_message_iter_next(&iface_entry)) {
                DBusMessageIter ikv;
                dbus_message_iter_recurse(&iface_entry, &ikv);
                const char* iface = nullptr;
                dbus_message_iter_get_basic(&ikv, &iface);
                if (!dbus_message_iter_next(&ikv) || iface == nullptr) {
                    continue;
                }
                if (dbus_message_iter_get_arg_type(&ikv) == DBUS_TYPE_ARRAY) {
                    this->parse_interface_properties(obj_path, iface, &ikv);
                }
            }
        }
    }

    dbus_message_unref(reply);
    this->select_active_adapter();
    return true;
}

void BtManager::Impl::select_active_adapter() {
    this->active_adapter_path.clear();
    for (const BtAdapterInfo& adapter : this->adapters) {
        if (this->adapter_matches(adapter)) {
            this->active_adapter_path = adapter.object_path;
            this->adapter_missing_logged = false;
            return;
        }
    }
    if (!this->adapter_missing_logged) {
        if (this->config.adapter.empty()) {
            log_line(LogLevel::WARN, LOG_TAG,
                     "No Bluetooth adapter found yet -- waiting for hardware");
        } else {
            log_line(LogLevel::WARN, LOG_TAG,
                     "Bluetooth adapter '%s' not found yet -- waiting",
                     this->config.adapter.c_str());
        }
        this->adapter_missing_logged = true;
    }
}

void BtManager::Impl::apply_adapter_settings() {
    if (this->conn == nullptr || this->active_adapter_path.empty()) {
        return;
    }
    set_dbus_property_bool(this->conn, this->active_adapter_path, IFACE_ADAPTER, "Powered", true);
    if (!this->config.speaker_name.empty()) {
        set_dbus_property_string(this->conn, this->active_adapter_path, IFACE_ADAPTER, "Alias",
                                 this->config.speaker_name);
    }
    set_dbus_property_uint32(this->conn, this->active_adapter_path, IFACE_ADAPTER,
                             "DiscoverableTimeout", 0);
    set_dbus_property_uint32(this->conn, this->active_adapter_path, IFACE_ADAPTER,
                             "PairableTimeout", 0);
    set_dbus_property_bool(this->conn, this->active_adapter_path, IFACE_ADAPTER, "Pairable", true);
    set_dbus_property_bool(this->conn, this->active_adapter_path, IFACE_ADAPTER, "Discoverable",
                           this->discoverable_desired);

    for (BtAdapterInfo& adapter : this->adapters) {
        if (adapter.object_path == this->active_adapter_path) {
            adapter.powered = true;
            adapter.pairable = true;
            adapter.discoverable = this->discoverable_desired;
            if (!this->config.speaker_name.empty()) {
                adapter.alias = this->config.speaker_name;
            }
            log_line(LogLevel::INFO, LOG_TAG,
                     "Adapter %s (%s) ready as \"%s\" (discoverable: %s)", adapter.name.c_str(),
                     adapter.address.c_str(), adapter.alias.c_str(),
                     this->discoverable_desired ? "on" : "off");
            break;
        }
    }
}

bool BtManager::Impl::setup_bluez(int64_t now_ms) {
    if (this->config.no_dbus) {
        return false;
    }
    if (!this->ensure_connected()) {
        this->next_setup_ms = now_ms + SETUP_RETRY_INTERVAL_MS;
        return false;
    }
    if (!this->register_agent()) {
        this->next_setup_ms = now_ms + SETUP_RETRY_INTERVAL_MS;
        return false;
    }
    if (!this->refresh_managed_objects()) {
        this->next_setup_ms = now_ms + SETUP_RETRY_INTERVAL_MS;
        return false;
    }
    if (this->active_adapter_path.empty()) {
        this->next_setup_ms = now_ms + SETUP_RETRY_INTERVAL_MS;
        return false;
    }
    this->apply_adapter_settings();
    return true;
}

void BtManager::Impl::handle_interfaces_added(DBusMessage* msg) {
    DBusMessageIter iter;
    if (!dbus_message_iter_init(msg, &iter) ||
        dbus_message_iter_get_arg_type(&iter) != DBUS_TYPE_OBJECT_PATH) {
        return;
    }
    const char* path = nullptr;
    dbus_message_iter_get_basic(&iter, &path);
    if (!dbus_message_iter_next(&iter) || path == nullptr ||
        dbus_message_iter_get_arg_type(&iter) != DBUS_TYPE_ARRAY) {
        return;
    }

    const bool had_adapter = !this->active_adapter_path.empty();
    DBusMessageIter iface_entry;
    for (dbus_message_iter_recurse(&iter, &iface_entry);
         dbus_message_iter_get_arg_type(&iface_entry) == DBUS_TYPE_DICT_ENTRY;
         dbus_message_iter_next(&iface_entry)) {
        DBusMessageIter ikv;
        dbus_message_iter_recurse(&iface_entry, &ikv);
        const char* iface = nullptr;
        dbus_message_iter_get_basic(&ikv, &iface);
        if (!dbus_message_iter_next(&ikv) || iface == nullptr) {
            continue;
        }
        if (dbus_message_iter_get_arg_type(&ikv) == DBUS_TYPE_ARRAY) {
            this->parse_interface_properties(path, iface, &ikv);
        }
    }

    if (!had_adapter) {
        this->select_active_adapter();
        if (!this->active_adapter_path.empty()) {
            this->apply_adapter_settings();
        }
    }
}

void BtManager::Impl::handle_interfaces_removed(DBusMessage* msg) {
    DBusMessageIter iter;
    if (!dbus_message_iter_init(msg, &iter) ||
        dbus_message_iter_get_arg_type(&iter) != DBUS_TYPE_OBJECT_PATH) {
        return;
    }
    const char* path = nullptr;
    dbus_message_iter_get_basic(&iter, &path);
    if (!dbus_message_iter_next(&iter) || path == nullptr ||
        dbus_message_iter_get_arg_type(&iter) != DBUS_TYPE_ARRAY) {
        return;
    }

    DBusMessageIter arr;
    for (dbus_message_iter_recurse(&iter, &arr);
         dbus_message_iter_get_arg_type(&arr) == DBUS_TYPE_STRING; dbus_message_iter_next(&arr)) {
        const char* iface = nullptr;
        dbus_message_iter_get_basic(&arr, &iface);
        if (iface == nullptr) {
            continue;
        }
        if (std::strcmp(iface, IFACE_ADAPTER) == 0) {
            this->adapters.erase(std::remove_if(this->adapters.begin(), this->adapters.end(),
                                                [path](const BtAdapterInfo& a) {
                                                    return a.object_path == path;
                                                }),
                                 this->adapters.end());
            if (this->active_adapter_path == path) {
                log_line(LogLevel::WARN, LOG_TAG, "Bluetooth adapter %s removed", path);
                this->active_adapter_path.clear();
                this->select_active_adapter();
            }
        } else if (std::strcmp(iface, IFACE_DEVICE) == 0) {
            this->devices_by_path.erase(path);
        } else if (std::strcmp(iface, IFACE_MEDIA_PLAYER) == 0 &&
                   this->media_player_path == path) {
            this->media_player_path.clear();
            this->media.status.clear();
            this->media.title.clear();
            this->media.artist.clear();
            this->media.album.clear();
        } else if (std::strcmp(iface, IFACE_MEDIA_TRANSPORT) == 0 &&
                   this->media_transport_path == path) {
            this->media_transport_path.clear();
            this->media.transport_active = false;
            this->media.transport_volume.reset();
        }
    }
}

void BtManager::Impl::handle_properties_changed(DBusMessage* msg) {
    const char* path = dbus_message_get_path(msg);
    if (path == nullptr) {
        return;
    }
    DBusMessageIter iter;
    if (!dbus_message_iter_init(msg, &iter) ||
        dbus_message_iter_get_arg_type(&iter) != DBUS_TYPE_STRING) {
        return;
    }
    const char* iface = nullptr;
    dbus_message_iter_get_basic(&iter, &iface);
    if (!dbus_message_iter_next(&iter) || iface == nullptr ||
        dbus_message_iter_get_arg_type(&iter) != DBUS_TYPE_ARRAY) {
        return;
    }
    this->parse_interface_properties(path, iface, &iter);
}

void BtManager::Impl::handle_signal(DBusMessage* msg) {
    if (dbus_message_is_signal(msg, IFACE_OBJECT_MANAGER, "InterfacesAdded")) {
        this->handle_interfaces_added(msg);
    } else if (dbus_message_is_signal(msg, IFACE_OBJECT_MANAGER, "InterfacesRemoved")) {
        this->handle_interfaces_removed(msg);
    } else if (dbus_message_is_signal(msg, IFACE_PROPERTIES, "PropertiesChanged")) {
        this->handle_properties_changed(msg);
    } else if (dbus_message_is_signal(msg, "org.freedesktop.DBus", "NameOwnerChanged")) {
        const char* name = nullptr;
        const char* old_owner = nullptr;
        const char* new_owner = nullptr;
        if (dbus_message_get_args(msg, nullptr, DBUS_TYPE_STRING, &name, DBUS_TYPE_STRING,
                                  &old_owner, DBUS_TYPE_STRING, &new_owner, DBUS_TYPE_INVALID) &&
            name != nullptr && std::strcmp(name, BLUEZ_SERVICE) == 0) {
            if (new_owner == nullptr || *new_owner == '\0') {
                log_line(LogLevel::WARN, LOG_TAG, "bluetoothd stopped -- waiting for restart");
                this->agent_registered = false;
                this->active_adapter_path.clear();
                this->adapters.clear();
                this->devices_by_path.clear();
            } else {
                log_line(LogLevel::INFO, LOG_TAG, "bluetoothd appeared -- re-registering");
                this->agent_registered = false;
                this->next_setup_ms = 0;
            }
        }
    }
}

DBusHandlerResult BtManager::Impl::message_filter(DBusConnection* /*conn*/, DBusMessage* msg,
                                                  void* userdata) {
    auto* self = static_cast<Impl*>(userdata);
    if (dbus_message_get_type(msg) == DBUS_MESSAGE_TYPE_SIGNAL) {
        self->handle_signal(msg);
    }
    return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
}

DBusHandlerResult BtManager::Impl::agent_message_handler(DBusConnection* conn, DBusMessage* msg,
                                                         void* userdata) {
    auto* self = static_cast<Impl*>(userdata);
    const char* iface = dbus_message_get_interface(msg);
    const char* member = dbus_message_get_member(msg);
    if (iface == nullptr || member == nullptr) {
        return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
    }

    if (std::strcmp(iface, IFACE_INTROSPECTABLE) == 0 && std::strcmp(member, "Introspect") == 0) {
        DBusMessage* reply = dbus_message_new_method_return(msg);
        if (reply != nullptr) {
            const char* xml = AGENT_INTROSPECTION_XML;
            dbus_message_append_args(reply, DBUS_TYPE_STRING, &xml, DBUS_TYPE_INVALID);
            dbus_connection_send(conn, reply, nullptr);
            dbus_message_unref(reply);
        }
        return DBUS_HANDLER_RESULT_HANDLED;
    }

    if (std::strcmp(iface, IFACE_AGENT) != 0) {
        return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
    }

    auto send_empty_reply = [conn](DBusMessage* req) {
        DBusMessage* reply = dbus_message_new_method_return(req);
        if (reply != nullptr) {
            dbus_connection_send(conn, reply, nullptr);
            dbus_message_unref(reply);
        }
    };
    auto send_error_reply = [conn](DBusMessage* req, const char* name, const char* text) {
        DBusMessage* reply = dbus_message_new_error(req, name, text);
        if (reply != nullptr) {
            dbus_connection_send(conn, reply, nullptr);
            dbus_message_unref(reply);
        }
    };

    if (std::strcmp(member, "Release") == 0) {
        log_line(LogLevel::INFO, LOG_TAG_AGENT, "BlueZ agent released");
        self->agent_registered = false;
        send_empty_reply(msg);
        return DBUS_HANDLER_RESULT_HANDLED;
    }

    if (std::strcmp(member, "Cancel") == 0) {
        log_line(LogLevel::INFO, LOG_TAG_AGENT, "Pairing cancelled by remote or BlueZ");
        self->resolve_pending_pairing(false, "Cancelled");
        send_empty_reply(msg);
        return DBUS_HANDLER_RESULT_HANDLED;
    }

    if (std::strcmp(member, "DisplayPinCode") == 0) {
        const char* device = nullptr;
        const char* pin = nullptr;
        if (dbus_message_get_args(msg, nullptr, DBUS_TYPE_OBJECT_PATH, &device, DBUS_TYPE_STRING,
                                  &pin, DBUS_TYPE_INVALID)) {
            log_line(LogLevel::INFO, LOG_TAG_AGENT, "Pairing PIN for %s: %s",
                     device != nullptr ? device : "?", pin != nullptr ? pin : "");
        }
        send_empty_reply(msg);
        return DBUS_HANDLER_RESULT_HANDLED;
    }

    if (std::strcmp(member, "DisplayPasskey") == 0) {
        const char* device = nullptr;
        dbus_uint32_t passkey = 0;
        dbus_uint16_t entered = 0;
        if (dbus_message_get_args(msg, nullptr, DBUS_TYPE_OBJECT_PATH, &device, DBUS_TYPE_UINT32,
                                  &passkey, DBUS_TYPE_UINT16, &entered, DBUS_TYPE_INVALID)) {
            log_line(LogLevel::INFO, LOG_TAG_AGENT, "Pairing passkey for %s: %06u",
                     device != nullptr ? device : "?", static_cast<unsigned>(passkey));
        }
        send_empty_reply(msg);
        return DBUS_HANDLER_RESULT_HANDLED;
    }

    if (std::strcmp(member, "RequestPinCode") == 0) {
        send_error_reply(msg, "org.bluez.Error.Rejected", "PIN code entry not supported");
        return DBUS_HANDLER_RESULT_HANDLED;
    }

    if (std::strcmp(member, "RequestPasskey") == 0) {
        send_error_reply(msg, "org.bluez.Error.Rejected", "Passkey entry not supported");
        return DBUS_HANDLER_RESULT_HANDLED;
    }

    if (std::strcmp(member, "AuthorizeService") == 0) {
        const char* device_path = nullptr;
        const char* uuid = nullptr;
        if (!dbus_message_get_args(msg, nullptr, DBUS_TYPE_OBJECT_PATH, &device_path,
                                   DBUS_TYPE_STRING, &uuid, DBUS_TYPE_INVALID) ||
            device_path == nullptr || uuid == nullptr) {
            send_error_reply(msg, "org.bluez.Error.Rejected", "Invalid arguments");
            return DBUS_HANDLER_RESULT_HANDLED;
        }
        if (!is_audio_service_uuid(uuid)) {
            log_line(LogLevel::DEBUG, LOG_TAG_AGENT, "Rejecting non-audio service %s for %s", uuid,
                     device_path);
            send_error_reply(msg, "org.bluez.Error.Rejected", "Only audio profiles are permitted");
            return DBUS_HANDLER_RESULT_HANDLED;
        }
        if (!self->config.target_device.empty()) {
            const auto it = self->devices_by_path.find(device_path);
            if (it != self->devices_by_path.end() &&
                it->second.address != self->config.target_device) {
                log_line(LogLevel::WARN, LOG_TAG_AGENT,
                         "Rejecting service for %s (restricted to --device %s)",
                         it->second.address.c_str(), self->config.target_device.c_str());
                send_error_reply(msg, "org.bluez.Error.Rejected", "Device not permitted");
                return DBUS_HANDLER_RESULT_HANDLED;
            }
        }
        log_line(LogLevel::INFO, LOG_TAG_AGENT, "Authorized audio profile %s for %s", uuid,
                 device_path);
        send_empty_reply(msg);
        return DBUS_HANDLER_RESULT_HANDLED;
    }

    if (std::strcmp(member, "RequestConfirmation") == 0 ||
        std::strcmp(member, "RequestAuthorization") == 0) {
        const char* device_path = nullptr;
        dbus_uint32_t passkey = 0;
        bool parsed = false;
        if (std::strcmp(member, "RequestConfirmation") == 0) {
            parsed = dbus_message_get_args(msg, nullptr, DBUS_TYPE_OBJECT_PATH, &device_path,
                                           DBUS_TYPE_UINT32, &passkey, DBUS_TYPE_INVALID) != FALSE;
        } else {
            parsed = dbus_message_get_args(msg, nullptr, DBUS_TYPE_OBJECT_PATH, &device_path,
                                           DBUS_TYPE_INVALID) != FALSE;
        }
        if (!parsed || device_path == nullptr) {
            send_error_reply(msg, "org.bluez.Error.Rejected", "Invalid arguments");
            return DBUS_HANDLER_RESULT_HANDLED;
        }

        std::string address = device_path;
        std::string name;
        const auto it = self->devices_by_path.find(device_path);
        if (it != self->devices_by_path.end()) {
            address = it->second.address;
            name = it->second.name;
        }

        if (!self->config.target_device.empty() && address != self->config.target_device) {
            log_line(LogLevel::WARN, LOG_TAG_AGENT,
                     "Rejecting pairing from %s (restricted to --device %s)", address.c_str(),
                     self->config.target_device.c_str());
            send_error_reply(msg, "org.bluez.Error.Rejected", "Device not permitted");
            return DBUS_HANDLER_RESULT_HANDLED;
        }

        if (self->config.allow_unpaired) {
            log_line(LogLevel::INFO, LOG_TAG_AGENT, "Auto-accepting pairing for %s (%s)",
                     name.empty() ? "device" : name.c_str(), address.c_str());
            set_dbus_property_bool(conn, device_path, IFACE_DEVICE, "Trusted", true);
            send_empty_reply(msg);
            return DBUS_HANDLER_RESULT_HANDLED;
        }

        if (self->pending_pairing_msg != nullptr) {
            send_error_reply(msg, "org.bluez.Error.Rejected", "Another pairing is in progress");
            return DBUS_HANDLER_RESULT_HANDLED;
        }

        self->pending_pairing_msg = dbus_message_ref(msg);
        self->pending_pairing_device_path = device_path;
        self->pending_pairing.active = true;
        self->pending_pairing.address = address;
        self->pending_pairing.name = name;
        if (std::strcmp(member, "RequestConfirmation") == 0) {
            char buf[16];
            std::snprintf(buf, sizeof(buf), "%06u", static_cast<unsigned>(passkey));
            self->pending_pairing.passkey = buf;
            log_line(LogLevel::INFO, LOG_TAG_AGENT,
                     "*** Pairing request from %s (%s), passkey %s *** -- run 'bt-speaker-cli "
                     "pair confirm' or 'bt-speaker-cli pair cancel'",
                     name.empty() ? "device" : name.c_str(), address.c_str(), buf);
        } else {
            self->pending_pairing.passkey.clear();
            log_line(LogLevel::INFO, LOG_TAG_AGENT,
                     "*** Pairing request from %s (%s) *** -- run 'bt-speaker-cli pair confirm' "
                     "or 'bt-speaker-cli pair cancel'",
                     name.empty() ? "device" : name.c_str(), address.c_str());
        }
        self->pending_pairing_deadline_ms = monotonic_ms() + PAIRING_TIMEOUT_MS;
        return DBUS_HANDLER_RESULT_HANDLED;
    }

    return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
}

void BtManager::Impl::resolve_pending_pairing(bool confirm, const char* reject_reason) {
    if (this->pending_pairing_msg == nullptr) {
        return;
    }
    if (this->conn != nullptr) {
        if (confirm) {
            if (!this->pending_pairing_device_path.empty()) {
                set_dbus_property_bool(this->conn, this->pending_pairing_device_path, IFACE_DEVICE,
                                       "Trusted", true);
            }
            DBusMessage* reply = dbus_message_new_method_return(this->pending_pairing_msg);
            if (reply != nullptr) {
                dbus_connection_send(this->conn, reply, nullptr);
                dbus_message_unref(reply);
            }
        } else {
            DBusMessage* reply = dbus_message_new_error(
                this->pending_pairing_msg, "org.bluez.Error.Rejected", reject_reason);
            if (reply != nullptr) {
                dbus_connection_send(this->conn, reply, nullptr);
                dbus_message_unref(reply);
            }
        }
        dbus_connection_flush(this->conn);
    }
    dbus_message_unref(this->pending_pairing_msg);
    this->pending_pairing_msg = nullptr;
    this->pending_pairing_device_path.clear();
    this->pending_pairing = BtPendingPairing{};
    this->pending_pairing_deadline_ms = 0;
}

void BtManager::Impl::check_pending_pairing_timeout(int64_t now_ms) {
    if (this->pending_pairing_msg != nullptr && now_ms >= this->pending_pairing_deadline_ms) {
        log_line(LogLevel::WARN, LOG_TAG_AGENT, "Pairing request from %s timed out",
                 this->pending_pairing.address.c_str());
        this->resolve_pending_pairing(false, "Pairing timed out");
    }
}

void BtManager::Impl::check_pending_connect() {
    if (this->pending_connect == nullptr ||
        !dbus_pending_call_get_completed(this->pending_connect)) {
        return;
    }
    DBusMessage* reply = dbus_pending_call_steal_reply(this->pending_connect);
    dbus_pending_call_unref(this->pending_connect);
    this->pending_connect = nullptr;

    if (reply != nullptr) {
        DBusError err;
        dbus_error_init(&err);
        if (dbus_set_error_from_message(&err, reply)) {
            log_line(LogLevel::DEBUG, LOG_TAG, "Connect to %s did not succeed: %s",
                     this->pending_connect_mac.c_str(), err.message);
            dbus_error_free(&err);
        } else {
            log_line(LogLevel::INFO, LOG_TAG, "Connect request to %s completed",
                     this->pending_connect_mac.c_str());
        }
        dbus_message_unref(reply);
    }
    this->pending_connect_mac.clear();
}

bool BtManager::Impl::start_async_connect(const std::string& path, const std::string& mac) {
    if (this->conn == nullptr || this->pending_connect != nullptr) {
        return false;
    }
    DBusMessage* msg =
        dbus_message_new_method_call(BLUEZ_SERVICE, path.c_str(), IFACE_DEVICE, "Connect");
    if (msg == nullptr) {
        return false;
    }
    DBusPendingCall* pending = nullptr;
    if (!dbus_connection_send_with_reply(this->conn, msg, &pending, 10000) || pending == nullptr) {
        dbus_message_unref(msg);
        return false;
    }
    dbus_message_unref(msg);
    this->pending_connect = pending;
    this->pending_connect_mac = mac;
    return true;
}

#else

struct BtManager::Impl {
    explicit Impl(BtManagerConfig cfg) : config(std::move(cfg)) {}

    BtManagerConfig config;
    std::string remembered_device;
    std::function<void(const std::string& mac)> on_device_connected;
    std::function<void(uint8_t volume)> on_transport_volume_changed;
    bool discoverable_desired{true};
    std::vector<BtAdapterInfo> adapters;
    std::string active_adapter_path;
    std::map<std::string, BtDeviceInfo> devices_by_path;
    BtMediaInfo media;
    BtPendingPairing pending_pairing;
};

#endif

BtManager::BtManager(BtManagerConfig config) : impl_(std::make_unique<Impl>(std::move(config))) {
    this->impl_->discoverable_desired = this->impl_->config.discoverable;
}

BtManager::~BtManager() {
    this->stop();
}

void BtManager::start(const std::string& remembered_device) {
    this->impl_->remembered_device = remembered_device;
    this->impl_->on_device_connected = [this](const std::string& mac) {
        this->impl_->remembered_device = mac;
        if (this->on_device_connected) {
            this->on_device_connected(mac);
        }
    };
    this->impl_->on_transport_volume_changed = [this](uint8_t vol) {
        if (this->on_transport_volume_changed) {
            this->on_transport_volume_changed(vol);
        }
    };

#ifdef BT_SPEAKER_CLI_HAVE_DBUS
    if (this->impl_->config.no_dbus) {
        log_line(LogLevel::INFO, LOG_TAG, "Bluetooth D-Bus control disabled (--no-dbus)");
        return;
    }
    this->impl_->setup_bluez(monotonic_ms());
#else
    log_line(LogLevel::WARN, LOG_TAG, "Built without libdbus-1; Bluetooth control disabled");
#endif
}

void BtManager::poll(int64_t now_ms) {
#ifdef BT_SPEAKER_CLI_HAVE_DBUS
    if (this->impl_->config.no_dbus) {
        return;
    }

    if (this->impl_->conn == nullptr || !dbus_connection_get_is_connected(this->impl_->conn) ||
        !this->impl_->agent_registered || this->impl_->active_adapter_path.empty()) {
        if (now_ms >= this->impl_->next_setup_ms) {
            this->impl_->setup_bluez(now_ms);
        }
        if (this->impl_->conn == nullptr) {
            return;
        }
    }

    dbus_connection_read_write_dispatch(this->impl_->conn, 0);
    this->impl_->check_pending_connect();
    this->impl_->check_pending_pairing_timeout(now_ms);

    const std::optional<BtDeviceInfo> connected = this->connected_device();
    const bool is_connected = connected.has_value();
    if (this->impl_->pacer.note_connected(is_connected, now_ms)) {
        log_line(LogLevel::INFO, LOG_TAG,
                 "No active Bluetooth peer -- waiting for reconnect in %lld ms",
                 static_cast<long long>(this->impl_->pacer.current_delay_ms()));
    }

    if (!is_connected && this->impl_->active_adapter_path.size() > 0 &&
        this->impl_->pending_connect == nullptr && this->impl_->pacer.should_attempt(now_ms)) {
        const std::vector<BtDeviceInfo> known = this->devices();
        const BtDeviceInfo* target =
            select_reconnect_target(known, this->impl_->config.target_device,
                                    this->impl_->remembered_device,
                                    this->impl_->config.auto_connect);
        if (target != nullptr) {
            this->impl_->pacer.note_attempt(now_ms);
            log_line(LogLevel::DEBUG, LOG_TAG, "Auto-connecting to %s (%s)",
                     target->name.empty() ? "device" : target->name.c_str(),
                     target->address.c_str());
            this->impl_->start_async_connect(target->object_path, target->address);
        } else {
            this->impl_->pacer.note_attempt(now_ms);
        }
    }
#else
    static_cast<void>(now_ms);
#endif
}

void BtManager::stop() {
#ifdef BT_SPEAKER_CLI_HAVE_DBUS
    this->impl_->cleanup();
#endif
}

bool BtManager::available() const {
#ifdef BT_SPEAKER_CLI_HAVE_DBUS
    return !this->impl_->config.no_dbus && this->impl_->conn != nullptr &&
           dbus_connection_get_is_connected(this->impl_->conn);
#else
    return false;
#endif
}

bool BtManager::adapter_ready() const {
#ifdef BT_SPEAKER_CLI_HAVE_DBUS
    return this->available() && !this->impl_->active_adapter_path.empty();
#else
    return false;
#endif
}

std::optional<BtAdapterInfo> BtManager::adapter() const {
    for (const BtAdapterInfo& a : this->impl_->adapters) {
        if (a.object_path == this->impl_->active_adapter_path) {
            return a;
        }
    }
    return std::nullopt;
}

std::vector<BtDeviceInfo> BtManager::devices() const {
    std::vector<BtDeviceInfo> list;
    list.reserve(this->impl_->devices_by_path.size());
    for (const auto& [path, dev] : this->impl_->devices_by_path) {
        list.push_back(dev);
    }
    std::sort(list.begin(), list.end(),
              [](const BtDeviceInfo& a, const BtDeviceInfo& b) { return a.address < b.address; });
    return list;
}

std::optional<BtDeviceInfo> BtManager::connected_device() const {
    for (const auto& [path, dev] : this->impl_->devices_by_path) {
        if (dev.connected) {
            return dev;
        }
    }
    return std::nullopt;
}

BtMediaInfo BtManager::media() const {
    return this->impl_->media;
}

BtPendingPairing BtManager::pending_pairing() const {
    return this->impl_->pending_pairing;
}

bool BtManager::discoverable() const {
    if (const std::optional<BtAdapterInfo> a = this->adapter()) {
        return a->discoverable;
    }
    return this->impl_->discoverable_desired;
}

bool BtManager::connect_device(const std::string& mac, std::string& error) {
#ifdef BT_SPEAKER_CLI_HAVE_DBUS
    if (!this->adapter_ready()) {
        error = "Bluetooth adapter is not ready";
        return false;
    }
    const BtDeviceInfo* target = nullptr;
    std::string norm;
    if (!mac.empty()) {
        if (!normalize_mac_address(mac, norm)) {
            error = "invalid Bluetooth MAC address '" + mac + "'";
            return false;
        }
        target = this->impl_->find_device_by_mac(norm);
        if (target == nullptr) {
            error = "device '" + norm + "' is not known to BlueZ yet -- pair it first";
            return false;
        }
    } else {
        const std::vector<BtDeviceInfo> known = this->devices();
        target = select_reconnect_target(known, this->impl_->config.target_device,
                                         this->impl_->remembered_device, true);
        if (target == nullptr) {
            error = "no paired Bluetooth device available to connect to";
            return false;
        }
    }
    if (target->connected) {
        return true;
    }
    if (this->impl_->pending_connect != nullptr) {
        error = "a connection attempt is already in progress";
        return false;
    }
    this->impl_->pacer.reset();
    if (!this->impl_->start_async_connect(target->object_path, target->address)) {
        error = "failed to send Connect request to BlueZ";
        return false;
    }
    log_line(LogLevel::INFO, LOG_TAG, "Connecting to %s (%s)",
             target->name.empty() ? "device" : target->name.c_str(), target->address.c_str());
    return true;
#else
    static_cast<void>(mac);
    error = "built without D-Bus support";
    return false;
#endif
}

bool BtManager::disconnect_device(const std::string& mac, std::string& error) {
#ifdef BT_SPEAKER_CLI_HAVE_DBUS
    if (!this->adapter_ready()) {
        error = "Bluetooth adapter is not ready";
        return false;
    }
    std::optional<BtDeviceInfo> target;
    if (mac.empty()) {
        target = this->connected_device();
        if (!target.has_value()) {
            error = "no Bluetooth device is currently connected";
            return false;
        }
    } else {
        std::string norm;
        if (!normalize_mac_address(mac, norm)) {
            error = "invalid Bluetooth MAC address '" + mac + "'";
            return false;
        }
        const BtDeviceInfo* found = this->impl_->find_device_by_mac(norm);
        if (found == nullptr) {
            error = "unknown Bluetooth device '" + norm + "'";
            return false;
        }
        target = *found;
    }

    DBusMessage* msg =
        dbus_message_new_method_call(BLUEZ_SERVICE, target->object_path.c_str(), IFACE_DEVICE,
                                     "Disconnect");
    if (msg == nullptr) {
        error = "out of memory";
        return false;
    }
    dbus_connection_send(this->impl_->conn, msg, nullptr);
    dbus_connection_flush(this->impl_->conn);
    dbus_message_unref(msg);
    log_line(LogLevel::INFO, LOG_TAG, "Disconnecting %s (%s)",
             target->name.empty() ? "device" : target->name.c_str(), target->address.c_str());
    return true;
#else
    static_cast<void>(mac);
    error = "built without D-Bus support";
    return false;
#endif
}

bool BtManager::set_trusted(const std::string& mac, bool trusted, std::string& error) {
#ifdef BT_SPEAKER_CLI_HAVE_DBUS
    if (!this->adapter_ready()) {
        error = "Bluetooth adapter is not ready";
        return false;
    }
    std::string norm;
    if (!normalize_mac_address(mac, norm)) {
        error = "invalid Bluetooth MAC address '" + mac + "'";
        return false;
    }
    const BtDeviceInfo* dev = this->impl_->find_device_by_mac(norm);
    if (dev == nullptr) {
        error = "unknown Bluetooth device '" + norm + "'";
        return false;
    }
    if (!set_dbus_property_bool(this->impl_->conn, dev->object_path, IFACE_DEVICE, "Trusted",
                                trusted)) {
        error = "BlueZ refused Trusted update for " + norm;
        return false;
    }
    return true;
#else
    static_cast<void>(mac);
    static_cast<void>(trusted);
    error = "built without D-Bus support";
    return false;
#endif
}

bool BtManager::remove_device(const std::string& mac, std::string& error) {
#ifdef BT_SPEAKER_CLI_HAVE_DBUS
    if (!this->adapter_ready()) {
        error = "Bluetooth adapter is not ready";
        return false;
    }
    std::string norm;
    if (!normalize_mac_address(mac, norm)) {
        error = "invalid Bluetooth MAC address '" + mac + "'";
        return false;
    }
    const BtDeviceInfo* dev = this->impl_->find_device_by_mac(norm);
    if (dev == nullptr) {
        error = "unknown Bluetooth device '" + norm + "'";
        return false;
    }
    DBusMessage* msg =
        dbus_message_new_method_call(BLUEZ_SERVICE, this->impl_->active_adapter_path.c_str(),
                                     IFACE_ADAPTER, "RemoveDevice");
    if (msg == nullptr) {
        error = "out of memory";
        return false;
    }
    const char* dev_path = dev->object_path.c_str();
    dbus_message_append_args(msg, DBUS_TYPE_OBJECT_PATH, &dev_path, DBUS_TYPE_INVALID);
    DBusError err;
    dbus_error_init(&err);
    DBusMessage* reply = dbus_connection_send_with_reply_and_block(
        this->impl_->conn, msg, DBUS_SHORT_TIMEOUT_MS, &err);
    dbus_message_unref(msg);
    if (reply != nullptr) {
        dbus_message_unref(reply);
    }
    if (dbus_error_is_set(&err)) {
        error = std::string("RemoveDevice failed: ") + err.message;
        dbus_error_free(&err);
        return false;
    }
    log_line(LogLevel::INFO, LOG_TAG, "Removed paired device %s", norm.c_str());
    return true;
#else
    static_cast<void>(mac);
    error = "built without D-Bus support";
    return false;
#endif
}

bool BtManager::set_discoverable(bool enabled, std::string& error) {
    this->impl_->discoverable_desired = enabled;
#ifdef BT_SPEAKER_CLI_HAVE_DBUS
    if (!this->adapter_ready()) {
        error = "Bluetooth adapter is not ready";
        return false;
    }
    if (!set_dbus_property_bool(this->impl_->conn, this->impl_->active_adapter_path, IFACE_ADAPTER,
                                "Discoverable", enabled)) {
        error = "BlueZ refused Discoverable update";
        return false;
    }
    log_line(LogLevel::INFO, LOG_TAG, "Discoverable set to %s", enabled ? "on" : "off");
    return true;
#else
    error = "built without D-Bus support";
    return false;
#endif
}

bool BtManager::answer_pairing(bool confirm, std::string& error) {
#ifdef BT_SPEAKER_CLI_HAVE_DBUS
    if (this->impl_->pending_pairing_msg == nullptr) {
        error = "no pairing request is waiting";
        return false;
    }
    const std::string addr = this->impl_->pending_pairing.address;
    this->impl_->resolve_pending_pairing(confirm);
    log_line(LogLevel::INFO, LOG_TAG_AGENT, "Pairing with %s %s locally", addr.c_str(),
             confirm ? "confirmed" : "cancelled");
    return true;
#else
    static_cast<void>(confirm);
    error = "built without D-Bus support";
    return false;
#endif
}

bool BtManager::send_media_command(AvrcpCommand command, std::string& error) {
#ifdef BT_SPEAKER_CLI_HAVE_DBUS
    if (this->impl_->conn == nullptr || this->impl_->media_player_path.empty()) {
        error = "no active AVRCP media player on the connected device";
        return false;
    }
    const char* method = nullptr;
    switch (command) {
        case AvrcpCommand::Play:
            method = "Play";
            break;
        case AvrcpCommand::Pause:
            method = "Pause";
            break;
        case AvrcpCommand::Stop:
            method = "Stop";
            break;
        case AvrcpCommand::Next:
            method = "Next";
            break;
        case AvrcpCommand::Previous:
            method = "Previous";
            break;
    }
    DBusMessage* msg = dbus_message_new_method_call(
        BLUEZ_SERVICE, this->impl_->media_player_path.c_str(), IFACE_MEDIA_PLAYER, method);
    if (msg == nullptr) {
        error = "out of memory";
        return false;
    }
    const bool sent = dbus_connection_send(this->impl_->conn, msg, nullptr) != FALSE;
    dbus_connection_flush(this->impl_->conn);
    dbus_message_unref(msg);
    if (!sent) {
        error = "failed to send AVRCP command";
        return false;
    }
    return true;
#else
    static_cast<void>(command);
    error = "built without D-Bus support";
    return false;
#endif
}

bool BtManager::set_transport_volume(uint8_t volume) {
#ifdef BT_SPEAKER_CLI_HAVE_DBUS
    if (this->impl_->conn == nullptr || this->impl_->media_transport_path.empty()) {
        return false;
    }
    return set_dbus_property_uint16(this->impl_->conn, this->impl_->media_transport_path,
                                    IFACE_MEDIA_TRANSPORT, "Volume",
                                    percent_to_transport_vol(volume));
#else
    static_cast<void>(volume);
    return false;
#endif
}

}  // namespace bt_speaker_cli
