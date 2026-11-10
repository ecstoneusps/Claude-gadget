#include "webui.h"

#include <mutex>
#include <string.h>

#include "BuddyConfig.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_timer.h"
#include "sdcard.h"
#include "wifi.h"

static const char* TAG = "webui";
static constexpr const char* kMask = "********";
static constexpr uint32_t kConfirmMs = 60000;
static constexpr size_t kMaxBody = 32 * 1024;

static const char* s_file = "settings.txt";
static std::mutex s_lock;
static std::string s_pending;       // accepted text waits here for the tap
static uint32_t s_pendingSince = 0;
static bool s_hasPending = false;
static const char* s_result = "none";  // what became of the last change: none/waiting/accepted/declined

static uint32_t nowMs() { return (uint32_t)(esp_timer_get_time() / 1000); }

static bool isSecret(const std::string& key) {
    return key == "ha_token" || key.rfind("wifi_password", 0) == 0;
}

// One line of settings.txt: the setting's key and where its value is
// (quotes included). Comments and blank lines have no key.
struct LineValue {
    std::string key;
    size_t start = 0, end = 0;  // value is line[start, end)
};

static std::string lowerTrim(const std::string& s) {
    size_t a = s.find_first_not_of(" \t"), b = s.find_last_not_of(" \t\r");
    std::string out = a == std::string::npos ? "" : s.substr(a, b - a + 1);
    for (auto& c : out) c = (char)tolower((unsigned char)c);
    return out;
}

static LineValue parseLine(const std::string& line) {
    LineValue v;
    const size_t first = line.find_first_not_of(" \t");
    if (first == std::string::npos || line[first] == '#') return v;
    const size_t eq = line.find('=');
    if (eq == std::string::npos) return v;
    const size_t hash = line.find('#');
    if (hash != std::string::npos && hash < eq) return v;
    v.key = lowerTrim(line.substr(0, eq));
    size_t s = line.find_first_not_of(" \t", eq + 1);
    if (s == std::string::npos) {
        v.start = v.end = line.size();
        return v;
    }
    v.start = s;
    if (line[s] == '"') {
        const size_t q = line.find('"', s + 1);
        v.end = q == std::string::npos ? line.size() : q + 1;
    } else {
        size_t e = line.find('#', s);
        if (e == std::string::npos) e = line.size();
        while (e > s && (line[e - 1] == ' ' || line[e - 1] == '\t' || line[e - 1] == '\r')) --e;
        v.end = e;
    }
    if (line[v.start] == '#') v.end = v.start;  // "key = # comment": empty value
    return v;
}

// Calls fn(line, parsed) for each line and joins what it returns.
template <typename F>
static std::string mapLines(const std::string& text, F fn) {
    std::string out;
    size_t pos = 0;
    while (pos <= text.size()) {
        size_t nl = text.find('\n', pos);
        const bool last = nl == std::string::npos;
        if (last) nl = text.size();
        const std::string line = text.substr(pos, nl - pos);
        out += fn(line, parseLine(line));
        if (last) break;
        out += '\n';
        pos = nl + 1;
    }
    return out;
}

static std::string masked(const std::string& text) {
    return mapLines(text, [](const std::string& line, const LineValue& v) {
        if (v.key.empty() || !isSecret(v.key) || v.end <= v.start) return line;
        return line.substr(0, v.start) + kMask + line.substr(v.end);
    });
}

// The edited text with each ******** put back to the value it stands for.
static std::string unmasked(const std::string& edited, const std::string& current) {
    std::string originals[8][2];
    int n = 0;
    mapLines(current, [&](const std::string& line, const LineValue& v) {
        if (!v.key.empty() && isSecret(v.key) && n < 8) {
            originals[n][0] = v.key;
            originals[n][1] = line.substr(v.start, v.end - v.start);
            ++n;
        }
        return std::string();
    });
    return mapLines(edited, [&](const std::string& line, const LineValue& v) {
        if (v.key.empty() || !isSecret(v.key) || line.substr(v.start, v.end - v.start) != kMask) return line;
        for (int i = 0; i < n; ++i)
            if (originals[i][0] == v.key) return line.substr(0, v.start) + originals[i][1] + line.substr(v.end);
        return line.substr(0, v.start) + line.substr(v.end);  // nothing to put back
    });
}

static std::string jsonEscape(const std::string& s) {
    std::string out;
    for (char c : s) {
        if (c == '"' || c == '\\') out += '\\', out += c;
        else if (c == '\n') out += "\\n";
        else if ((unsigned char)c < 0x20) continue;
        else out += c;
    }
    return out;
}

static const char kPage[] = R"HTML(<!doctype html>
<html><head><meta charset="utf-8"><meta name="viewport" content="width=device-width, initial-scale=1">
<title>Buddy's settings</title>
<style>
:root{--bg:#f6f7fb;--fg:#1d2230;--muted:#5d6577;--card:#fff;--line:#d9dde7;--accent:#2f6fde;--ok:#1d8a4e;--warn:#b5651d}
@media (prefers-color-scheme:dark){:root{--bg:#14161c;--fg:#e8eaf0;--muted:#a0a7b8;--card:#1d2029;--line:#333847;--accent:#6b9bff;--ok:#4cc38a;--warn:#e9a15b}}
body{margin:0;background:var(--bg);color:var(--fg);font:16px/1.45 system-ui,sans-serif}
main{max-width:900px;margin:0 auto;padding:16px}
h1{font-size:1.4em;margin:.2em 0}
p{color:var(--muted);margin:.4em 0 1em}
textarea{width:100%;box-sizing:border-box;height:65vh;font:13px/1.4 ui-monospace,Menlo,Consolas,monospace;
 background:var(--card);color:var(--fg);border:1px solid var(--line);border-radius:8px;padding:10px;white-space:pre;overflow:auto}
button{font:inherit;padding:.6em 1.4em;border:0;border-radius:8px;background:var(--accent);color:#fff;cursor:pointer;margin-top:10px}
button:disabled{opacity:.5}
#msg{margin-top:12px;white-space:pre-wrap}
.ok{color:var(--ok)}.warn{color:var(--warn)}
</style></head><body><main>
<h1>Buddy's settings</h1>
<p>This is <code>settings.txt</code> from Buddy's SD card. Passwords and the token show as ********: leave them like that to keep them.
After saving, <b>tap Buddy's screen</b> within a minute to accept; he then restarts with the new settings.</p>
<textarea id="t" spellcheck="false" autocomplete="off">Loading...</textarea>
<button id="save" disabled>Save</button>
<div id="msg"></div>
<script>
const t=document.getElementById('t'),save=document.getElementById('save'),msg=document.getElementById('msg');
function show(text,cls){msg.textContent=text;msg.className=cls||'';}
fetch('/settings').then(r=>r.ok?r.text():Promise.reject(r.status)).then(x=>{t.value=x;save.disabled=false;})
 .catch(e=>show('Could not load the settings ('+e+').','warn'));
save.onclick=async()=>{
 save.disabled=true;show('Sending...');
 try{
  const r=await fetch('/settings',{method:'POST',body:t.value});const j=await r.json();
  if(!r.ok){show(j.error||'Not saved.','warn');save.disabled=false;return;}
  let s='Now tap Buddy\'s screen to accept the new settings (within a minute).';
  if(j.warnings.length)s+='\n\nProblems found (those lines will be ignored):\n- '+j.warnings.join('\n- ');
  show(s,j.warnings.length?'warn':'');
  for(let i=0;i<70;i++){
   await new Promise(ok=>setTimeout(ok,1000));
   const st=await (await fetch('/status')).json();
   if(st.state==='accepted'){show('Accepted. Buddy is restarting with the new settings.','ok');return;}
   if(st.state==='declined'){show('Not accepted (nobody tapped the screen in time). Nothing was changed.','warn');save.disabled=false;return;}
  }
 }catch(e){show('Lost contact with Buddy. If he accepted, he is restarting; reload this page in a moment.','warn');}
 save.disabled=false;
};
</script></main></body></html>)HTML";

static esp_err_t getPage(httpd_req_t* req) {
    httpd_resp_set_type(req, "text/html; charset=utf-8");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    return httpd_resp_send(req, kPage, HTTPD_RESP_USE_STRLEN);
}

static esp_err_t getSettings(httpd_req_t* req) {
    std::string text;
    if (!sdcardReadFile(s_file, text)) {
        httpd_resp_set_status(req, "503 Service Unavailable");
        return httpd_resp_sendstr(req, "settings.txt couldn't be read from the SD card");
    }
    const std::string out = masked(text);
    httpd_resp_set_type(req, "text/plain; charset=utf-8");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    return httpd_resp_send(req, out.data(), out.size());
}

static esp_err_t sendJson(httpd_req_t* req, const char* status, const std::string& json) {
    httpd_resp_set_status(req, status);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    return httpd_resp_send(req, json.data(), json.size());
}

static esp_err_t postSettings(httpd_req_t* req) {
    if (req->content_len == 0 || req->content_len > kMaxBody)
        return sendJson(req, "400 Bad Request", "{\"error\":\"That's empty or too big.\"}");
    std::string body(req->content_len, '\0');
    size_t got = 0;
    while (got < body.size()) {
        const int n = httpd_req_recv(req, &body[got], body.size() - got);
        if (n == HTTPD_SOCK_ERR_TIMEOUT) continue;
        if (n <= 0) return ESP_FAIL;
        got += n;
    }
    std::string current;
    sdcardReadFile(s_file, current);
    std::string text = unmasked(body, current);
    if (text.empty() || text.back() != '\n') text += '\n';
    BuddyConfig cfg;
    std::vector<std::string> warnings;
    parseConfig(text, cfg, warnings);
    {
        std::lock_guard<std::mutex> g(s_lock);
        s_pending = text;
        s_pendingSince = nowMs();
        s_hasPending = true;
        s_result = "waiting";
    }
    ESP_LOGI(TAG, "new settings from the web page (%u bytes, %u problem%s); waiting for a tap on the screen",
             (unsigned)text.size(), (unsigned)warnings.size(), warnings.size() == 1 ? "" : "s");
    std::string json = "{\"warnings\":[";
    for (size_t i = 0; i < warnings.size(); ++i) json += (i ? ",\"" : "\"") + jsonEscape(warnings[i]) + "\"";
    json += "]}";
    return sendJson(req, "200 OK", json);
}

static esp_err_t getStatus(httpd_req_t* req) {
    const char* state;
    {
        std::lock_guard<std::mutex> g(s_lock);
        state = s_result;
    }
    return sendJson(req, "200 OK", std::string("{\"state\":\"") + state + "\"}");
}

void webuiStart(const char* settingsFile) {
    s_file = settingsFile;
    httpd_config_t cfg = HTTPD_DEFAULT_CONFIG();
    cfg.stack_size = 6144;
    cfg.core_id = 0;
    cfg.max_open_sockets = 3;
    cfg.lru_purge_enable = true;
    httpd_handle_t server = nullptr;
    if (httpd_start(&server, &cfg) != ESP_OK) {
        ESP_LOGW(TAG, "couldn't start the settings page");
        return;
    }
    const httpd_uri_t uris[] = {
        {.uri = "/", .method = HTTP_GET, .handler = getPage, .user_ctx = nullptr},
        {.uri = "/settings", .method = HTTP_GET, .handler = getSettings, .user_ctx = nullptr},
        {.uri = "/settings", .method = HTTP_POST, .handler = postSettings, .user_ctx = nullptr},
        {.uri = "/status", .method = HTTP_GET, .handler = getStatus, .user_ctx = nullptr},
    };
    for (const auto& u : uris) httpd_register_uri_handler(server, &u);
    ESP_LOGI(TAG, "settings page ready (opens once Wi-Fi is connected)");
}

bool webuiWaiting(uint32_t now) {
    std::lock_guard<std::mutex> g(s_lock);
    if (!s_hasPending) return false;
    if (now - s_pendingSince > kConfirmMs) {
        s_hasPending = false;
        s_pending.clear();
        s_result = "declined";
        ESP_LOGI(TAG, "new settings not accepted (no tap within a minute); nothing changed");
        return false;
    }
    return true;
}

void webuiAccept() {
    std::string text;
    {
        std::lock_guard<std::mutex> g(s_lock);
        if (!s_hasPending) return;
        text.swap(s_pending);
        s_hasPending = false;
    }
    const bool ok = sdcardWriteFile(s_file, text);
    ESP_LOGI(TAG, "new settings %s", ok ? "accepted and saved" : "accepted, but COULDN'T be saved to the SD card");
    std::lock_guard<std::mutex> g(s_lock);
    s_result = ok ? "accepted" : "declined";
}

void webuiDecline() {
    std::lock_guard<std::mutex> g(s_lock);
    s_hasPending = false;
    s_pending.clear();
    s_result = "declined";
}

std::string webuiAddress() {
    if (!wifiConnected()) return "";
    esp_netif_t* n = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
    esp_netif_ip_info_t ip;
    if (!n || esp_netif_get_ip_info(n, &ip) != ESP_OK || ip.ip.addr == 0) return "";
    char buf[40];
    snprintf(buf, sizeof buf, "http://" IPSTR "/", IP2STR(&ip.ip));
    return buf;
}
