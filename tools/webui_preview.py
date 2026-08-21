"""Assemble mitsubishi2MQTT web UI pages from the C header templates for local preview.

Mimics what main.cpp does: parse the C string literals out of the headers,
concatenate header + page + footer, and replace the _PLACEHOLDER_ tokens
with sample values (German texts, like the firmware would with de-DE).
"""
import re
import sys
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent
HTMLS = REPO / "main" / "htmls"
OUT = Path(__file__).parent / "preview"
OUT.mkdir(exist_ok=True)


def parse_c_strings(path):
    """Return dict var_name -> concatenated string content."""
    text = path.read_text(encoding="utf-8")
    # strip /* */ comments
    text = re.sub(r"/\*.*?\*/", "", text, flags=re.S)
    # strip preprocessor lines (#if / #endif) keeping content
    text = "\n".join(l for l in text.splitlines() if not l.strip().startswith("#"))
    result = {}
    for m in re.finditer(r"const char (\w+)\[\] PROGMEM\s*=", text):
        name = m.group(1)
        i = m.end()
        parts = []
        in_str = False
        cur = []
        while i < len(text):
            c = text[i]
            if in_str:
                if c == "\\":
                    nxt = text[i + 1]
                    cur.append('"' if nxt == '"' else "\\" if nxt == "\\" else "\\" + nxt)
                    i += 2
                    continue
                if c == '"':
                    in_str = False
                    parts.append("".join(cur))
                    cur = []
                else:
                    cur.append(c)
            elif c == '"':
                in_str = True
            elif c == ";":
                break
            i += 1
        result[name] = "".join(parts)
    return result


tpl = {}
for f in ["html_common.h", "html_menu.h", "html_pages.h", "html_init.h", "javascript_common.h"]:
    tpl.update(parse_c_strings(HTMLS / f))

COMMON = {
    "_APP_NAME_": "Mitsubishi2MQTT",
    "_UNIT_NAME_": "HVAC Wohnzimmer",
    "_VERSION_": "2025.07.1 (ESP32)",
    "_KS_VERSION_": "1.11.3",
}

def wrap(content, repl):
    page = tpl["html_common_header"] + content + tpl["html_common_footer"]
    # firmware serves the stylesheet as /style.css; inline it for standalone previews
    page = page.replace("<link rel='stylesheet' href='/style.css?v=_KS_VERSION_'>",
                        "<style>" + tpl["html_css"] + "</style>")
    for k, v in sorted({**COMMON, **repl}.items(), key=lambda kv: -len(kv[0])):
        page = page.replace(k, v)
    # any leftover tokens -> highlight
    return page


pages = {}

pages["menu_setup.html"] = wrap(tpl["html_menu_setup"], {
    "_SHOW_LOGOUT_": "1",
    "_TXT_SETUP_PAGE_": "Einstellungen",
    "_TXT_MQTT_": "MQTT",
    "_TXT_WIFI_": "WLAN",
    "_TXT_UNIT_": "Einheit",
    "_TXT_OTHERS_": "Sonstiges",
    "_TXT_STATUS_": "Status",
    "_TXT_FW_UPGRADE_": "Firmware-Aktualisierung",
    "_TXT_REBOOT_": "Neustart",
    "_TXT_LOGOUT_": "Abmelden",
    "_TXT_RESET_CONFIRM_": "Wirklich alle Einstellungen zur\u00fccksetzen?",
    "_TXT_RESET_": "Einstellungen zur\u00fccksetzen",
    "_TXT_BACK_": "Zur\u00fcck",
})

control = (tpl["control_script_events"] + tpl["html_page_control"]
           + tpl["html_page_control_mode"] + tpl["html_page_control_fan"]
           + tpl["html_page_control_vane"] + tpl["html_page_control_widevane"]
           + tpl["html_page_control_footer"])
pages["control.html"] = wrap(control, {
    "_MIN_TEMP_": "16", "_MAX_TEMP_": "31", "_TEMP_STEP_": "0.5",
    "_HEAT_MODE_SUPPORT_": "1", "_QUIET_MODE_SUPPORT_": "1",
    "_TXT_CTRL_CTEMP_": "Raumtemperatur",
    "_TXT_CTRL_TITLE_": "Steuerung",
    "_TXT_CTRL_TEMP_": "Zieltemperatur",
    "_TXT_CTRL_POWER_": "Betrieb",
    "_TXT_CTRL_MODE_": "Modus",
    "_TXT_CTRL_FAN_": "L\u00fcfter",
    "_TXT_CTRL_VANE_": "Lamelle",
    "_TXT_CTRL_WVANE_": "Lamelle (breit)",
    "_TXT_F_AUTO_": "Auto", "_TXT_F_DRY_": "Entfeuchten", "_TXT_F_COOL_": "K\u00fchlen",
    "_TXT_F_HEAT_": "Heizen", "_TXT_F_FAN_": "L\u00fcften", "_TXT_F_QUIET_": "Leise",
    "_TXT_F_LOW_": "Niedrig", "_TXT_F_MEDIUM_": "Mittel", "_TXT_F_MIDDLE_": "H\u00f6her",
    "_TXT_F_HIGH_": "Hoch", "_TXT_F_SWING_": "Schwenken", "_TXT_F_POS_": "Position",
    "_TXT_BACK_": "Zur\u00fcck", "_TXT_SETUP_": "Einstellungen",
    "_ROOMTEMP_": "22.5", "_TEMP_SCALE_": "C", "_TEMP_": "21.5",
    "_POWER_": "checked",
    "_MODE_A_": "", "_MODE_D_": "", "_MODE_C_": "selected", "_MODE_H_": "", "_MODE_F_": "",
    "_HEAT_HIDDEN_": "", "_QUIET_HIDDEN_": "",
    "_FAN_A_": "selected", "_FAN_Q_": "", "_FAN_1_": "", "_FAN_2_": "", "_FAN_3_": "", "_FAN_4_": "",
    "_VANE_STYLE_": "", "_VANE_A_": "selected", "_VANE_S_": "",
    "_VANE_1_": "", "_VANE_2_": "", "_VANE_3_": "", "_VANE_4_": "", "_VANE_5_": "",
    "_WIDE_VANE_STYLE_": "", "_WVANE_S_": "selected",
    "_WVANE_1_": "", "_WVANE_2_": "", "_WVANE_3_": "", "_WVANE_4_": "", "_WVANE_5_": "", "_WVANE_6_": "",
})

pages["status.html"] = wrap(tpl["html_page_status"], {
    "_TXT_STATUS_TITLE_": "Status",
    "_TXT_STATUS_HVAC_": "HVAC-Verbindung",
    "_TXT_RETRIES_HVAC_": "Verbindungsversuche",
    "_TXT_STATUS_MQTT_": "MQTT-Verbindung",
    "_TXT_STATUS_WIFI_IP_": "IP-Adresse",
    "_TXT_STATUS_WIFI_": "WLAN-Signal",
    "_TXT_BUILD_VERSION_": "Firmware-Version",
    "_TXT_BUILD_DATE_": "Build-Datum",
    "_TXT_STATUS_FREEHEAP_": "Freier Speicher",
    "_TXT_CURRENT_TIME_": "Aktuelle Zeit",
    "_TXT_BOOT_TIME": "Laufzeit",
    "_TXT_BACK_": "Zur\u00fcck",
    "_HVAC_STATUS_": "<font color='green'><b>verbunden</b></font>",
    "_HVAC_RETRIES_": "0",
    "_MQTT_STATUS_": "<font color='green'><b>verbunden</b></font>",
    "_WIFI_IP_": "<font color='blue'><b>192.168.1.42</b></font>",
    "_WIFI_BSSID_": "A4:2B:B0:C3:11:22",
    "_WIFI_MAC_": "24:6F:28:AA:BB:CC",
    "_WIFI_STATUS_": "-52",
    "_BUILD_VERSION_": "2025.07.1",
    "_BUILD_DATE_": "Jul 12 2025 10:33:01",
    "_FREE_HEAP_": "156232 (61.4% )",
    "_CURRENT_TIME_": "<font color='blue'><b>2026-08-20 14:22:10</b></font>",
    "_BOOT_TIME_": "<font color='orange'><b>3 days 04:12:55</b></font>",
})

pages["mqtt.html"] = wrap(tpl["html_page_mqtt"], {
    "_TXT_MQTT_TITLE_": "MQTT-Parameter",
    "_TXT_MQTT_FN_": "Anzeigename", "_TXT_MQTT_FN_DESC_": "(Name in Home Assistant)",
    "_TXT_MQTT_HOST_": "Host", "_TXT_MQTT_PORT_": "Port", "_TXT_MQTT_PORT_DESC_": "(Standard 1883)",
    "_TXT_MQTT_USER_": "Benutzer", "_TXT_MQTT_PASSWORD_": "Passwort",
    "_TXT_MQTT_TOPIC_": "Topic", "_TXT_MQTT_ROOT_CA_CERT_": "Root-CA-Zertifikat",
    "_TXT_MQTT_PH_USER_": "mqtt-benutzer", "_TXT_MQTT_PH_PWD_": "passwort",
    "_TXT_MQTT_PH_TOPIC_": "topic",
    "_MQTT_FN_": "Klima Wohnzimmer", "_MQTT_HOST_": "192.168.1.10", "_MQTT_PORT_": "1883",
    "_MQTT_USER_": "homeassistant", "_MQTT_PASSWORD_": "geheim", "_MQTT_TOPIC_": "mitsubishi2mqtt",
    "_MQTT_ROOT_CA_CERT_": "",
    "_TXT_SAVE_": "Speichern", "_TXT_BACK_": "Zur\u00fcck",
})

pages["login.html"] = wrap(tpl["html_page_login"], {
    "_LOGIN_SUCCESS_": "0",
    "_TXT_LOGIN_TITLE_": "Anmeldung",
    "_TXT_LOGIN_USERNAME_": "Benutzername",
    "_TXT_LOGIN_PASSWORD_": "Passwort",
    "_TXT_LOGIN_PH_USER_": "Benutzername eingeben",
    "_TXT_LOGIN_PH_PWD_": "Passwort eingeben",
    "_TXT_LOGIN_": "Anmelden",
    "_TXT_LOGIN_OPEN_STATUS_": "Status-Seite \u00f6ffnen (ohne Anmeldung)",
    "_LOGIN_MSG_": ">",
})

pages["timers.html"] = wrap(tpl["timers_script"] + tpl["html_page_timers"], {
    "_TXT_BACK_": "Zurück",
})

pages["devices.html"] = wrap(tpl["devices_script"] + tpl["html_page_devices"], {
    "_TXT_BACK_": "Zurück",
})

pages["backup.html"] = wrap(tpl["backup_script"] + tpl["html_page_backup"], {
    "_TXT_BACK_": "Zurück",
})

pages["init.html"] = wrap(tpl["html_init_setup"], {
    "_TXT_INIT_TITLE_": "Ersteinrichtung",
    "_TXT_UNIT_LANGUAGE_": "Sprache",
    "_LANGUAGE_OPTIONS_": "<option selected>Deutsch</option><option>English</option>",
    "_TXT_WIFI_HOST_DESC_": "(keine Leer- oder Sonderzeichen)",
    "_TXT_WIFI_HOST_": "Gerätename (Hostname)",
    "_TXT_WIFI_TITLE_": "WLAN-Einstellungen",
    "_TXT_WIFI_SSID_": "WLAN-Name (SSID)",
    "_TXT_WIFI_SSID_ENTER_": "(eintippen)",
    "_TXT_WIFI_SSID_SELECT_": "oder ein Netzwerk auswählen:",
    "_WIFI_OPTIONS_": "<option>MeinWLAN</option><option>Nachbars-WLAN</option>",
    "_TXT_WIFI_PSK_": "WLAN-Passwort",
    "_TXT_WIFI_STATIC_IP_": "Feste IP-Adresse",
    "_TXT_WIFI_STATIC_GW_": "Gateway",
    "_TXT_WIFI_STATIC_MASK_": "Subnetzmaske",
    "_TXT_WIFI_STATIC_DNS_": "DNS-Server",
    "_WIFI_STATIC_IP_": "", "_WIFI_STATIC_GW_": "", "_WIFI_STATIC_MASK_": "", "_WIFI_STATIC_DNS_": "",
    "_TXT_MQTT_TITLE_": "MQTT-Einstellungen",
    "_TXT_MQTT_HOST_": "Server (Host)",
    "_TXT_MQTT_PORT_DESC": "(Standard 1883)",
    "_TXT_MQTT_PORT_": "Port",
    "_TXT_MQTT_USER_": "Benutzername",
    "_TXT_MQTT_PASSWORD_": "Passwort",
    "_TXT_MQTT_PH_USER_": "MQTT-Benutzer eingeben",
    "_TXT_MQTT_PH_PWD_": "MQTT-Passwort eingeben",
    "_MQTT_HOST_": "", "_MQTT_PORT_": "1883", "_MQTT_USER_": "", "_MQTT_PASSWORD_": "",
    "_TXT_SAVE_": "Speichern & neu starten",
    "_TXT_FIRMWARE_UPGRADE_": "Firmware-Aktualisierung",
    "_FIRMWARE_UPLOAD_": "",
    "_UNIT_NAME_": "Klimaanlage-A1B2C3",
})

pages["upgrade.html"] = wrap(tpl["html_page_upgrade"], {
    "_TXT_FW_UPDATE_PAGE_": "Firmware-Aktualisierung",
    "_TXT_UPGRADE_INFO_": "Upgrade per Datei-Upload",
    "_TXT_B_UPGRADE_": "Aktualisierung starten",
    "_TXT_UPGRADE_START_": "Aktualisierung gestartet",
    "_TXT_BACK_": "Zur\u00fcck",
    "_FIRMWARE_UPLOAD_": "",
})

# static assets served by the firmware from flash
(OUT / "control.js").write_text(tpl["control_js"], encoding="utf-8")
(OUT / "timers.js").write_text(tpl["timers_js"], encoding="utf-8")
(OUT / "devices.js").write_text(tpl["devices_js"], encoding="utf-8")

leftover = {}
for name, html in pages.items():
    (OUT / name).write_text(html, encoding="utf-8")
    toks = set(re.findall(r"_[A-Z][A-Z0-9_]+_", html))
    if toks:
        leftover[name] = sorted(toks)

print("written to", OUT)
for n, t in leftover.items():
    print("LEFTOVER", n, t)

