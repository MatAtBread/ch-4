#include "web.h"
#include "config.h"
#include "mqtt.h"
#include "ota_http.h"
#include "wifi-captiveportal.h"
#include "cJSON.h"
#include "esp_log.h"
#include <sstream>
#include <string.h>
#include "esp_app_desc.h"

extern "C" const char *TAG;
#define MULTILINE_STRING(...) #__VA_ARGS__

static bool startsWith(const char *search, const char *match) {
    return strncmp(search, match, strlen(match)) == 0;
}

static uint8_t hexValue(const char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    return 0;
}

static void unencode(char *buf, const char *src, int size) {
    while (*src && size > 1) {
        if (*src == '%') {
            auto msn = hexValue(src[1]);
            auto lsn = hexValue(src[2]);
            src += 3;
            *buf++ = (char)(msn * 16 + lsn);
            size--;
        } else {
            *buf++ = *src++;
            size--;
        }
    }
    *buf = 0;
}

class Ch4ConfigPortal : public HttpGetHandler {
public:
    esp_err_t getHandler(httpd_req_t *req) override {
        const esp_app_desc_t *app_desc = esp_app_get_description();
        char versionDetail[110] = {0};
        snprintf((char *)versionDetail, sizeof versionDetail, "%s %s %s",
               app_desc->version, app_desc->date, app_desc->time);

        static char buffer[1024];

        if (startsWith(req->uri, "/process")) {
            const char *json = strchr(req->uri, '?');
            if (json) {
                unencode(buffer, json + 1, sizeof(buffer));

                // Parse the JSON. If it has connectivity fields, update NVS directly.
                cJSON *root = cJSON_Parse(buffer);
                if (root) {
                    ch4_config_t cfg;
                    config_load(&cfg);
                    bool net_changed = false;

                    cJSON *ssid = cJSON_GetObjectItem(root, "ssid");
                    if (ssid && cJSON_IsString(ssid)) {
                        strncpy(cfg.ssid, ssid->valuestring, sizeof(cfg.ssid) - 1);
                        net_changed = true;
                    }
                    cJSON *pwd = cJSON_GetObjectItem(root, "pwd");
                    if (pwd && cJSON_IsString(pwd)) {
                        strncpy(cfg.wifi_pwd, pwd->valuestring, sizeof(cfg.wifi_pwd) - 1);
                        net_changed = true;
                    }
                    cJSON *mqtt = cJSON_GetObjectItem(root, "mqtt");
                    if (mqtt && cJSON_IsString(mqtt)) {
                        strncpy(cfg.mqtt_url, mqtt->valuestring, sizeof(cfg.mqtt_url) - 1);
                        net_changed = true;
                    }
                    cJSON *dev = cJSON_GetObjectItem(root, "device_name");
                    if (dev && cJSON_IsString(dev)) {
                        strncpy(cfg.device_name, dev->valuestring, sizeof(cfg.device_name) - 1);
                        net_changed = true;
                    }

                    if (net_changed) {
                        config_save(&cfg);
                    }

                    cJSON_Delete(root);

                    // Pass the same buffer to the shared MQTT processor to handle mode/pause/ota
                    process_state_json(buffer);
                }
            }
        } else if (startsWith(req->uri, "/close")) {
            esp_restart();
        } else if (strcmp(req->uri, "/") != 0) {
            httpd_resp_set_status(req, "302 Found");
            httpd_resp_set_hdr(req, "Location", get_portal_redirect_url());
            const char *resp_str = "<html><body>Redirecting</body></html>";
            httpd_resp_send(req, resp_str, HTTPD_RESP_USE_STRLEN);
            return ESP_OK;
        }

        httpd_resp_set_type(req, "text/html");

        ch4_config_t current_cfg;
        config_load(&current_cfg);

        std::stringstream html;
        html << "<!DOCTYPE html>\n"
            "<html>\n"
            "<head>\n"
            "<meta charset=\"UTF-8\">\n"
            "<meta name=\"viewport\" content=\"width=device-width, initial-scale=1\">\n"
            "<title>FreeHouse CH-4 Config</title>\n"
            "<style>* { font-family: sans-serif; } button { display: block; margin: 0.5em; }</style>\n"
            "<script>\n"
            MULTILINE_STRING(
                function processMessage(e, k, v) {
                    if (e instanceof HTMLElement) {
                        if (k === undefined) k = e.name || e.id;
                        if (v === undefined) {
                            v = (e.type === 'checkbox') ? e.checked : e.value;
                        }
                    }
                    fetch("/process?" + JSON.stringify({[k]: v}));
                }

                function sendConfig() {
                    let ssid = document.getElementById('ssid').value;
                    let pwd = document.getElementById('pwd').value;
                    let mqtt = document.getElementById('mqtt').value;
                    let device_name = document.getElementById('device_name').value;
                    fetch("/process?" + JSON.stringify({"ssid": ssid, "pwd": pwd, "mqtt": mqtt, "device_name": device_name}));
                }

                function trigger_ota(btn) {
                    btn.disabled = true;
                    btn.innerText = 'Triggering...';
                    fetch("/process?" + JSON.stringify({"ota": true})).then((res) => {
                        if (!res.ok) throw new Error("HTTP " + res.status);
                        btn.innerText = 'Updating Device...';
                    }).catch((e) => {
                        btn.disabled = false;
                        btn.innerText = 'Failed. Retry?';
                    });
                }

                function showScanOverlay(nets) {
                    var existing = document.getElementById('scan_overlay');
                    if (existing) existing.remove();
                    var overlay = document.createElement('div');
                    overlay.id = 'scan_overlay';
                    overlay.style.cssText = 'position:fixed;top:0;left:0;right:0;bottom:0;background:rgba(0,0,0,0.5);display:flex;align-items:center;justify-content:center;z-index:100';
                    var box = document.createElement('div');
                    box.style.cssText = 'background:#fff;padding:1em;border-radius:8px;max-width:320px;width:90%;max-height:80vh;overflow-y:auto';
                    var title = document.createElement('h3');
                    title.textContent = 'Select Network';
                    title.style.margin = '0 0 0.5em';
                    box.appendChild(title);
                    if (nets.length === 0) {
                        var p = document.createElement('p');
                        p.textContent = 'No networks found';
                        box.appendChild(p);
                    }
                    nets.forEach(function(net) {
                        var row = document.createElement('div');
                        row.style.cssText = 'padding:0.5em;cursor:pointer;border-bottom:1px solid #eee;display:flex;justify-content:space-between;align-items:center';
                        var name = document.createElement('span');
                        name.textContent = net.ssid;
                        var rssi = document.createElement('span');
                        rssi.style.cssText = 'color:#888;font-size:0.85em;margin-left:1em;white-space:nowrap';
                        rssi.textContent = net.rssi + ' dBm';
                        row.appendChild(name);
                        row.appendChild(rssi);
                        row.onclick = function() {
                            document.getElementById('ssid').value = net.ssid;
                            overlay.remove();
                        };
                        box.appendChild(row);
                    });
                    var closeBtn = document.createElement('button');
                    closeBtn.textContent = 'Cancel';
                    closeBtn.style.cssText = 'display:block;margin-top:0.5em';
                    closeBtn.onclick = function() { overlay.remove(); };
                    box.appendChild(closeBtn);
                    overlay.appendChild(box);
                    overlay.onclick = function(e) { if (e.target === overlay) overlay.remove(); };
                    document.body.appendChild(overlay);
                }

                function scanWifi() {
                    var btn = document.getElementById('scan_btn');
                    btn.disabled = true;
                    btn.textContent = 'Scanning...';
                    function done() { btn.disabled = false; btn.textContent = 'Scan'; }
                    fetch('/scan').then(function(res) { return res.json(); }).then(function(nets) {
                        done();
                        showScanOverlay(nets);
                    }).catch(function() {
                        done();
                        alert('Scan failed');
                    });
                }
            )
            "\n</script>\n"
            "</head>\n"
            "<body>\n"
            "<h1>FreeHouse CH-4 Relay</h1>\n"

            "<h2>Relay Control</h2>\n"
            "<input name='mode' onclick='processMessage(this, \"mode\", \"on\")' type='radio' " << (strcmp(current_cfg.mode, "on") == 0 ? "checked" : "") << "/> ON<br>\n"
            "<input name='mode' onclick='processMessage(this, \"mode\", \"clock\")' type='radio' " << (strcmp(current_cfg.mode, "clock") == 0 ? "checked" : "") << "/> CLOCK<br>\n"
            "<input name='mode' onclick='processMessage(this, \"mode\", \"off\")' type='radio' " << (strcmp(current_cfg.mode, "off") == 0 ? "checked" : "") << "/> OFF<br>\n"
            "<br>\n"
            "<input name='pause' onchange='processMessage(this, \"pause\", this.checked)' type='checkbox' " << (current_cfg.pause ? "checked" : "") << "/> Pause<br>\n"

            "<h2>Networking Config</h2>"
            "<table>\n"
            "<tr><td>WiFi SSID</td><td><input id='ssid' value=\"" << current_cfg.ssid << "\"><button id='scan_btn' onclick='scanWifi()' style='display:inline;margin:0 0 0 0.5em'>Scan</button></td></tr>\n"
            "<tr><td>WiFi Password</td><td><input id='pwd' value=\"" << current_cfg.wifi_pwd << "\"></td></tr>\n"
            "<tr><td>MQTT URL</td><td><input id='mqtt' value=\"" << current_cfg.mqtt_url << "\"></td></tr>\n"
            "<tr><td>Device Name</td><td><input id='device_name' value=\"" << current_cfg.device_name << "\"></td></tr>\n"
            "</table>\n"
            "<button onclick='sendConfig()'>Save Network Config</button>\n"

            "<h2>Actions</h2>\n"
            "<button id='ota_btn' onclick='trigger_ota(this)'>Trigger HTTP OTA Update</button>\n"
            << versionDetail <<
            "<button onclick='window.location.href = \"/close\"'>Restart / Apply WiFi</button>\n"
            "</body></html>";

        httpd_resp_send(req, html.str().c_str(), HTTPD_RESP_USE_STRLEN);
        return ESP_OK;
    }
};

static Ch4ConfigPortal portal;

void web_config_start(void) {
    // Only starts the HTTP server parts (not the soft-AP). Soft-AP can use this too.
    start_web_server(&portal);
}

void captive_portal_start(void) {
    start_captive_portal(&portal, "FreeHouse-CH4");
}
