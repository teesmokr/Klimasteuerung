/*
  mitsubishi2mqtt - Mitsubishi Heat Pump to MQTT control for Home Assistant.
  Copyright (c) 2023 gysmo38, dzungpv, shampeon, endeavour, jascdk, chrdavis, alekslyse.  All right reserved.
  This library is free software; you can redistribute it and/or
  modify it under the terms of the GNU Lesser General Public
  License as published by the Free Software Foundation; either
  version 2.1 of the License, or (at your option) any later version.
  This library is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
  Lesser General Public License for more details.
  You should have received a copy of the GNU Lesser General Public
  License along with this library; if not, write to the Free Software
  Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
*/
namespace de
{
  // Breadcum
  const char txt_setup_page[] PROGMEM = "Einstellungen";
  const char txt_upload_fw_page[] PROGMEM = "Firmware hochladen";
  const char txt_fw_update_page[] PROGMEM = "Firmware-Aktualisierung";
  const char txt_check_fw_page[] PROGMEM = "Neue Firmware prüfen";
  const char txt_home_page[] PROGMEM = "Startseite";
  // Main Menu
  const char txt_control[] PROGMEM = "Steuerung";
  const char txt_setup[] PROGMEM = "Einstellungen";
  const char txt_status[] PROGMEM = "Status";
  const char txt_firmware_upgrade[] PROGMEM = "Firmware-Aktualisierung";
  const char txt_reboot[] PROGMEM = "Neustart";

  // Setup Menu
  const char txt_mqtt[] PROGMEM = "MQTT";
  const char txt_wifi[] PROGMEM = "WLAN";
  const char txt_unit[] PROGMEM = "Allgemein";
  const char txt_others[] PROGMEM = "Sonstiges";
  const char txt_reset[] PROGMEM = "Einstellungen zurücksetzen";
  const char txt_reset_confirm[] PROGMEM = "Wirklich alle Einstellungen zurücksetzen?";
  const char txt_mqtt_fn_desc[] PROGMEM = "(keine Leer- oder Sonderzeichen)";
  const char txt_mqtt_port_desc[] PROGMEM = "(Standard 1883; ESP32: 8883 mit Zertifikat)";
  const char txt_mqtt_ph_topic[] PROGMEM = "MQTT-Topic eingeben";
  const char txt_mqtt_ph_user[] PROGMEM = "MQTT-Benutzer eingeben";
  const char txt_mqtt_ph_pwd[] PROGMEM = "MQTT-Passwort eingeben";

  // Buttons
  const char txt_back[] PROGMEM = "Zurück";
  const char txt_save[] PROGMEM = "Speichern & neu starten";
  const char txt_logout[] PROGMEM = "Abmelden";
  const char txt_upgrade[] PROGMEM = "Aktualisierung starten";
  const char txt_login[] PROGMEM = "Anmelden";

  // Form choices
  const char txt_f_on[] PROGMEM = "An";
  const char txt_f_off[] PROGMEM = "Aus";
  const char txt_f_auto[] PROGMEM = "Auto";
  const char txt_f_heat[] PROGMEM = "Heizen";
  const char txt_f_dry[] PROGMEM = "Entfeuchten";
  const char txt_f_cool[] PROGMEM = "Kühlen";
  const char txt_f_fan[] PROGMEM = "Lüften";
  const char txt_f_quiet[] PROGMEM = "Leise";
  const char txt_f_speed[] PROGMEM = "Geschwindigkeit";
  const char txt_f_swing[] PROGMEM = "Schwenken";
  const char txt_f_pos[] PROGMEM = "Position";
  const char txt_f_celsius[] PROGMEM = "Celsius";
  const char txt_f_fh[] PROGMEM = "Fahrenheit";
  const char txt_f_allmodes[] PROGMEM = "Alle Modi";
  const char txt_f_noheat[] PROGMEM = "Alle außer Heizen";
  const char txt_f_noquiet[] PROGMEM = "Alle außer Leise";
  const char txt_f_low[] PROGMEM = "Niedrig";
  const char txt_f_medium[] PROGMEM = "Mittel";
  const char txt_f_middle[] PROGMEM = "Erhöht";
  const char txt_f_high[] PROGMEM = "Hoch";

  // Page Reboot, save & Resseting
  const char txt_m_reboot[] PROGMEM = "Neustart läuft – die Seite lädt neu in";
  const char txt_m_reset[] PROGMEM = "Wird zurückgesetzt – Einrichtungs-WLAN erscheint in";
  const char txt_m_reset_1[] PROGMEM = "Verbinde dich danach mit dem WLAN";
  const char txt_m_save[] PROGMEM = "Einstellungen gespeichert, Neustart – die Seite lädt neu in";

  // Page MQTT
  const char txt_mqtt_title[] PROGMEM = "MQTT-Einstellungen";
  const char txt_mqtt_fn[] PROGMEM = "Anzeigename";
  const char txt_mqtt_host[] PROGMEM = "Server (Host)";
  const char txt_mqtt_port[] PROGMEM = "Port";
  const char txt_mqtt_user[] PROGMEM = "Benutzername";
  const char txt_mqtt_password[] PROGMEM = "Passwort";
  const char txt_mqtt_topic[] PROGMEM = "Topic";
  const char txt_mqtt_root_ca_cert[] PROGMEM = "CA-Root-Zertifikat (Standard: Let's Encrypt)";

  // Page Others
  const char txt_others_title[] PROGMEM = "Sonstige Einstellungen";
  const char txt_others_haauto[] PROGMEM = "Home-Assistant-Autodiscovery";
  const char txt_others_hatopic[] PROGMEM = "Autodiscovery-Topic";
  const char txt_others_debug_packets[] PROGMEM = "Debug-Pakete per MQTT";
  const char txt_others_debug_log[] PROGMEM = "Debug-Logs per MQTT";
  const char txt_others_tx_pin[] PROGMEM = "TX-Pin (ESP32, 0 = UART0)";
  const char txt_others_rx_pin[] PROGMEM = "RX-Pin (ESP32, 0 = UART0)";
  const char txt_others_tz[] PROGMEM = "Zeitzone";
  const char txt_others_tz_list[] PROGMEM = "Liste ansehen";
  const char txt_others_ntp_server[] PROGMEM = "NTP-Server";
  const char txt_others_web_panel[] PROGMEM = "Weboberfläche";

  // Page Status
  const char txt_status_title[] PROGMEM = "Status";
  const char txt_status_hvac[] PROGMEM = "Klimaanlage";
  const char txt_retries_hvac[] PROGMEM = "Verbindungsversuche";
  const char txt_status_mqtt[] PROGMEM = "MQTT-Verbindung";
  const char txt_status_wifi[] PROGMEM = "WLAN-Signal";
  const char txt_status_connect[] PROGMEM = "Verbunden";
  const char txt_status_disconnect[] PROGMEM = "Getrennt";
  const char txt_status_wifi_ip[] PROGMEM = "IP-Adresse";
  const char txt_failed_get_wifi_ip[] PROGMEM = "Keine IP-Adresse erhalten";
  const char txt_build_version[] PROGMEM = "Firmware-Version";
  const char txt_build_date[] PROGMEM = "Build-Datum";
  const char txt_status_freeheap[] PROGMEM = "Freier Arbeitsspeicher";
  const char txt_current_time[] PROGMEM = "Aktuelle Zeit";
  const char txt_boot_time[] PROGMEM = "Laufzeit";

  // Page WIFI
  const char txt_wifi_title[] PROGMEM = "WLAN-Einstellungen";
  const char txt_wifi_hostname[] PROGMEM = "Gerätename (Hostname)";
  const char txt_wifi_ssid[] PROGMEM = "WLAN-Name (SSID)";
  const char txt_wifi_psk[] PROGMEM = "WLAN-Passwort";
  const char txt_wifi_otap[] PROGMEM = "OTA-Passwort";
  const char txt_wifi_hostname_desc[] PROGMEM = "(keine Leer- oder Sonderzeichen)";
  const char txt_wifi_ssid_enter[] PROGMEM = "(eintippen)";
  const char txt_wifi_ssid_select[] PROGMEM = "oder ein Netzwerk auswählen:";
  const char txt_wifi_static_ip[] PROGMEM = "Feste IP-Adresse";
  const char txt_wifi_static_gw[] PROGMEM = "Gateway";
  const char txt_wifi_static_mask[] PROGMEM = "Subnetzmaske";
  const char txt_wifi_static_dns[] PROGMEM = "DNS-Server";

  // Page Control
  const char txt_ctrl_title[] PROGMEM = "Steuerung";
  const char txt_ctrl_temp[] PROGMEM = "Zieltemperatur";
  const char txt_ctrl_power[] PROGMEM = "Betrieb";
  const char txt_ctrl_mode[] PROGMEM = "Modus";
  const char txt_ctrl_fan[] PROGMEM = "Lüfter";
  const char txt_ctrl_vane[] PROGMEM = "Lamelle";
  const char txt_ctrl_wvane[] PROGMEM = "Lamelle (breit)";
  const char txt_ctrl_ctemp[] PROGMEM = "Raumtemperatur";

  // Page Unit
  const char txt_unit_title[] PROGMEM = "Allgemeine Einstellungen";
  const char txt_unit_temp[] PROGMEM = "Temperatureinheit";
  const char txt_unit_maxtemp[] PROGMEM = "Maximale Temperatur";
  const char txt_unit_mintemp[] PROGMEM = "Minimale Temperatur";
  const char txt_unit_steptemp[] PROGMEM = "Temperaturschritt";
  const char txt_unit_modes[] PROGMEM = "Unterstützte Modi";
  const char txt_unit_language[] PROGMEM = "Sprache";
  const char txt_unit_fan_modes[] PROGMEM = "Unterstützte Lüfterstufen";
  const char txt_unit_password[] PROGMEM = "Login-Passwort (optional)";
  const char txt_unit_password_confirm[] PROGMEM = "Passwort wiederholen";
  const char txt_unit_password_not_match[] PROGMEM = "Die Passwörter stimmen nicht überein";
  const char txt_unit_login_username[] PROGMEM = "Hinweis: Der Benutzername ist";

  // Page Login
  const char txt_login_title[] PROGMEM = "Anmeldung";
  const char txt_login_password[] PROGMEM = "Passwort";
  const char txt_login_sucess[] PROGMEM = "Anmeldung erfolgreich – du wirst gleich weitergeleitet.";
  const char txt_login_fail[] PROGMEM = "Benutzername oder Passwort falsch – bitte erneut versuchen.";
  const char txt_login_username[] PROGMEM = "Benutzername";
  const char txt_login_open_status[] PROGMEM = "Status-Seite öffnen (ohne Anmeldung)";
  const char txt_login_ph_user[] PROGMEM = "Benutzername eingeben";
  const char txt_login_ph_pwd[] PROGMEM = "Passwort eingeben";

  // Page Upgrade
  const char txt_upgrade_title[] PROGMEM = "Aktualisierung";
  const char txt_upgrade_info[] PROGMEM = "Firmware-Datei (.bin) hochladen";
  const char txt_upgrade_start[] PROGMEM = "Hochladen gestartet";

  // Page Upload
  const char txt_upload_nofile[] PROGMEM = "Keine Datei ausgewählt";
  const char txt_upload_filetoolarge[] PROGMEM = "Die Datei ist größer als der verfügbare Speicher";
  const char txt_upload_fileheader[] PROGMEM = "Keine gültige Firmware-Datei (Magic-Header fehlt)";
  const char txt_upload_flashsize[] PROGMEM = "Die Firmware passt nicht in den Flash dieses Geräts";
  const char txt_upload_buffer[] PROGMEM = "Übertragungsfehler beim Hochladen (Puffer)";
  const char txt_upload_failed[] PROGMEM = "Hochladen fehlgeschlagen – bitte erneut versuchen";
  const char txt_upload_aborted[] PROGMEM = "Hochladen abgebrochen";
  const char txt_upload_code[] PROGMEM = "Upload-Fehlercode ";
  const char txt_upload_error[] PROGMEM = "Update-Fehlercode (siehe Updater.cpp) ";
  const char txt_upload_success[] PROGMEM = "Erfolgreich";
  const char txt_upload_refresh[] PROGMEM = "Die Seite lädt neu in";
  const char txt_upload[] PROGMEM = "Hochladen";

  // Page Init
  const char txt_init_title[] PROGMEM = "Ersteinrichtung";
  const char txt_init_reboot_mes[] PROGMEM = "Das Gerät verbindet sich mit deinem WLAN und ist danach erreichbar unter";
  const char txt_init_reboot_mes_1[] PROGMEM = "Diese Adresse danach im Browser öffnen. Neustart in";
  const char txt_init_reboot[] PROGMEM = "Neustart …";
}
