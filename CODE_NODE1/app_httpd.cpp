#include "esp_http_server.h"
#include "esp_timer.h"
#include "esp_camera.h"
#include "img_converters.h"
#include "fb_gfx.h"
#include "esp32-hal-ledc.h"
#include "sdkconfig.h"
#include "HardwareSerial.h"
#include "esp_heap_caps.h"

extern HardwareSerial Serial2; 

#define CONFIG_ESP_FACE_DETECT_ENABLED 1

#if CONFIG_ESP_FACE_DETECT_ENABLED
#include <vector>
#include "human_face_detect_msr01.hpp"
#include "human_face_detect_mnp01.hpp"

#define TWO_STAGE 0 

#define FACE_COLOR_WHITE  0x00FFFFFF
#define FACE_COLOR_BLACK  0x00000000
#define FACE_COLOR_RED    0x000000FF
#define FACE_COLOR_GREEN  0x0000FF00
#define FACE_COLOR_BLUE   0x00FF0000
#define FACE_COLOR_YELLOW (FACE_COLOR_RED | FACE_COLOR_GREEN)
#endif

#define PART_BOUNDARY "123456789000000000000987654321"
static const char *_STREAM_CONTENT_TYPE = "multipart/x-mixed-replace;boundary=" PART_BOUNDARY;
static const char *_STREAM_BOUNDARY = "\r\n--" PART_BOUNDARY "\r\n";
static const char *_STREAM_PART = "Content-Type: image/jpeg\r\nContent-Length: %u\r\nX-Timestamp: %d.%06d\r\n\r\n";

httpd_handle_t stream_httpd = NULL;
httpd_handle_t camera_httpd = NULL;

#if CONFIG_ESP_FACE_DETECT_ENABLED
static int8_t detection_enabled = 1; 
#endif

extern bool seat_state[16]; 
extern SemaphoreHandle_t serialMutex;
bool face_detected_global = false;
bool last_face_state = false;
extern void queueSerialMessage(const char* msg, uint16_t len);

static uint32_t frame_count = 0;
#define FACE_DETECT_SKIP 3

#if CONFIG_ESP_FACE_DETECT_ENABLED
static void draw_face_boxes(fb_data_t *fb, std::list<dl::detect::result_t> *results) {
    int x, y, w, h;
    uint32_t color = FACE_COLOR_YELLOW;
    if(fb->bytes_per_pixel == 2){
        color = ((color >> 16) & 0x001F) | ((color >> 3) & 0x07E0) | ((color << 8) & 0xF800);
    }
    for (std::list<dl::detect::result_t>::iterator prediction = results->begin(); prediction != results->end(); prediction++) {
        x = (int)prediction->box[0]; y = (int)prediction->box[1]; w = (int)prediction->box[2] - x + 1; h = (int)prediction->box[3] - y + 1;
        if((x + w) > fb->width) w = fb->width - x; if((y + h) > fb->height) h = fb->height - y;
        fb_gfx_drawFastHLine(fb, x, y, w, color); fb_gfx_drawFastHLine(fb, x, y + h - 1, w, color);
        fb_gfx_drawFastVLine(fb, x, y, h, color); fb_gfx_drawFastVLine(fb, x + w - 1, y, h, color);
    }
}
#endif

static esp_err_t stream_handler(httpd_req_t *req) {
    camera_fb_t *fb = NULL;
    struct timeval _timestamp;
    esp_err_t res = ESP_OK;
    char *part_buf[128];

#if CONFIG_ESP_FACE_DETECT_ENABLED
    HumanFaceDetectMSR01 s1(0.1F, 0.5F, 10, 0.2F);
#endif

    res = httpd_resp_set_type(req, _STREAM_CONTENT_TYPE);
    if (res != ESP_OK) return res;
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");

    while (true) {
        bool current_frame_has_face = false;
        size_t _jpg_buf_len = 0;
        uint8_t *_jpg_buf = NULL;
        bool is_reencoded = false;

        fb = esp_camera_fb_get();
        if (!fb) {
            res = ESP_FAIL;
        } else {
            _timestamp.tv_sec = fb->timestamp.tv_sec;
            _timestamp.tv_usec = fb->timestamp.tv_usec;
            _jpg_buf_len = fb->len;
            _jpg_buf = fb->buf;

#if CONFIG_ESP_FACE_DETECT_ENABLED
            bool do_face_detection = (frame_count % FACE_DETECT_SKIP == 0);
            
            if (do_face_detection && fb->width <= 400 && fb->format == PIXFORMAT_JPEG) {
                size_t out_len = fb->width * fb->height * 3;
                uint8_t *out_buf = (uint8_t*)heap_caps_malloc(out_len, MALLOC_CAP_SPIRAM);

                if (out_buf != NULL) {
                    if (fmt2rgb888(fb->buf, fb->len, fb->format, out_buf)) {
                        std::list<dl::detect::result_t> &results = s1.infer((uint8_t *)out_buf, {(int)fb->height, (int)fb->width, 3});
                        
                        if (results.size() > 0) {
                            current_frame_has_face = true;
                            fb_data_t rfb;
                            rfb.width = fb->width; rfb.height = fb->height; rfb.data = out_buf;
                            rfb.bytes_per_pixel = 3; rfb.format = FB_BGR888;
                            draw_face_boxes(&rfb, &results);

                            uint8_t *temp_jpg = NULL;
                            size_t temp_jpg_len = 0;
                            if (fmt2jpg(out_buf, out_len, fb->width, fb->height, PIXFORMAT_RGB888, 80, &temp_jpg, &temp_jpg_len)) {
                                _jpg_buf = temp_jpg;
                                _jpg_buf_len = temp_jpg_len;
                                is_reencoded = true;
                            }
                        }
                    }
                    free(out_buf);
                }
            }

            if (do_face_detection) {
                face_detected_global = current_frame_has_face;
                if (current_frame_has_face && !last_face_state) { queueSerialMessage("1", 1); } 
                else if (!current_frame_has_face && last_face_state) { queueSerialMessage("0", 1); }
                last_face_state = current_frame_has_face;
            }
#endif
            frame_count++;

            if (res == ESP_OK) { res = httpd_resp_send_chunk(req, _STREAM_BOUNDARY, strlen(_STREAM_BOUNDARY)); }
            if (res == ESP_OK) { size_t hlen = snprintf((char *)part_buf, 128, _STREAM_PART, _jpg_buf_len, _timestamp.tv_sec, _timestamp.tv_usec); res = httpd_resp_send_chunk(req, (const char *)part_buf, hlen); }
            if (res == ESP_OK) { res = httpd_resp_send_chunk(req, (const char *)_jpg_buf, _jpg_buf_len); }

            if (fb) { esp_camera_fb_return(fb); fb = NULL; }
            if (is_reencoded && _jpg_buf != NULL) { free(_jpg_buf); _jpg_buf = NULL; }
        }
        
        if (res != ESP_OK) { break; }
    }
    return res;
}

static esp_err_t data_handler(httpd_req_t *req) {
    char json_response[256];
    
    if (xSemaphoreTake(serialMutex, 10)) {
        int offset = snprintf(json_response, sizeof(json_response), "{\"face\":%s,\"seats\":[", face_detected_global ? "true" : "false");
        for (int i = 0; i < 16; i++) {
            offset += snprintf(json_response + offset, sizeof(json_response) - offset, "%s%s", seat_state[i] ? "true" : "false", i < 15 ? "," : "");
        }
        snprintf(json_response + offset, sizeof(json_response) - offset, "]}");
        xSemaphoreGive(serialMutex);
    } else {
        snprintf(json_response, sizeof(json_response), "{\"face\":false,\"seats\":[]}");
    }

    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
    return httpd_resp_send(req, json_response, HTTPD_RESP_USE_STRLEN);
}

// --- GIAO DIỆN HTML/JS TÍCH HỢP (PHIÊN BẢN ĐÃ SỬA LỖI IMG) ---
static esp_err_t index_handler(httpd_req_t *req) {
    const char* html = R"rawliteral(
<!DOCTYPE html>
<html lang="vi">
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>KMA - Smart School Bus Dashboard</title>
<link href="https://fonts.googleapis.com/css2?family=Inter:wght@400;500;600;700;800&display=swap" rel="stylesheet">
<link rel="stylesheet" href="https://cdnjs.cloudflare.com/ajax/libs/font-awesome/6.4.0/css/all.min.css">
<style>
:root { 
    --bg-main: #F4F1EA;
    --card-bg: #FFFFFF;
    --text-dark: #1F2937;
    --text-muted: #6B7280;
    --kma-red: #B91C1C;
    --accent-blue: #0284C7;
    --success-green: #15803D;
    --warning-gold: #D97706;
    --seat-bg: linear-gradient(135deg, rgba(255,255,255,0.9) 0%, rgba(240,240,240,0.4) 100%);
}

body { 
    margin: 0; 
    background-color: var(--bg-main);
    color: var(--text-dark); 
    font-family: 'Inter', sans-serif; 
    min-height: 100vh;
    padding: 20px;
    box-sizing: border-box;
}

/* --- HEADER --- */
.header-container {
    background: transparent;
    margin-bottom: 20px;
}
.header-top {
    display: flex;
    align-items: center;
    gap: 20px;
    padding-bottom: 15px;
    border-bottom: 2px solid #E5E0D8;
}
.logo { width: 90px; height: auto; filter: drop-shadow(0 4px 6px rgba(0,0,0,0.1)); }
.title {
    font-size: 1.6rem;
    font-weight: 800;
    color: var(--text-dark);
    text-transform: uppercase;
    margin: 0;
    line-height: 1.3;
}
.header-bottom {
    display: flex;
    align-items: center;
    gap: 15px;
    flex-wrap: wrap;
    margin-top: 15px;
}
.gvhd { font-weight: 700; color: #92400E; display: flex; align-items: center; gap: 8px; font-size: 1.05rem; }
.badge-red { background: var(--kma-red); color: white; padding: 5px 15px; border-radius: 6px; font-weight: 700; font-size: 0.95rem; box-shadow: 0 2px 4px rgba(185, 28, 28, 0.3); }
.student-tag { background: var(--card-bg); border: 1px solid #E5E7EB; padding: 6px 12px; border-radius: 20px; font-size: 0.9rem; font-weight: 600; color: var(--text-dark); display: flex; align-items: center; gap: 6px; box-shadow: 0 1px 3px rgba(0,0,0,0.05); }

/* --- MAIN LAYOUT --- */
.main { display: grid; grid-template-columns: 340px 1fr; gap: 30px; }

/* --- BUS LAYOUT (NEUMORPHISM) --- */
.bus-wrapper {
    background: #EAE6DF;
    border-radius: 25px;
    padding: 20px;
    box-shadow: inset 5px 5px 10px #D5D0C6, inset -5px -5px 10px #FFFFFF;
    border: 1px solid #FFF;
    display: flex;
    justify-content: center;
}
.bus-frame {
    width: 100%;
    max-width: 260px;
    background: #F8F6F0;
    border-radius: 40px 40px 20px 20px;
    border: 4px solid #D4D0C5;
    padding: 50px 15px 20px;
    position: relative;
    box-shadow: 0 15px 25px rgba(0,0,0,0.1);
}
.wind { position: absolute; top: 15px; left: 50%; transform: translateX(-50%); width: 75%; height: 35px; background: rgba(255,255,255,0.6); border-radius: 20px; box-shadow: inset 0 2px 4px rgba(0,0,0,0.05); }
.row { display: grid; grid-template-columns: 1fr 1fr 1fr; gap: 12px; margin-bottom: 15px; }
.last { grid-template-columns: repeat(4, 1fr); }

/* Glassmorphism Seats */
.seat { 
    aspect-ratio: 1/1.1; 
    border-radius: 12px; 
    background: var(--seat-bg);
    border: 1px solid rgba(255,255,255,0.8);
    display: flex; justify-content: center; align-items: center; 
    font-weight: 700; font-size: 14px; color: var(--text-dark); 
    box-shadow: 
        4px 4px 8px rgba(0,0,0,0.08), 
        -4px -4px 8px rgba(255,255,255,0.9),
        inset 1px 1px 2px rgba(255,255,255,0.8);
    transition: all 0.3s ease; 
}
.driver { border: 2px dashed var(--warning-gold); color: var(--warning-gold); background: transparent; box-shadow: none; }
.occupied { 
    background: var(--kma-red); 
    color: #fff; 
    border-color: #991B1B; 
    box-shadow: 0 4px 10px rgba(185, 28, 28, 0.4), inset 0 2px 4px rgba(255,255,255,0.3); 
}

/* --- RIGHT PANEL --- */
.right-panel { display: flex; flex-direction: column; gap: 20px; }

/* Top Controls */
.top-controls { display: flex; justify-content: space-between; align-items: center; }
.time-box {
    background: var(--card-bg);
    padding: 12px 20px;
    border-radius: 12px;
    border: 1px solid #E5E7EB;
    font-weight: 600;
    color: var(--accent-blue);
    display: flex; align-items: center; gap: 10px;
    box-shadow: 0 2px 6px rgba(0,0,0,0.03);
}
.btn-excel { 
    background: var(--success-green); color: white; 
    padding: 12px 20px; border-radius: 10px; text-decoration: none; 
    font-weight: 700; display: flex; align-items: center; gap: 8px; 
    box-shadow: 0 4px 10px rgba(21, 128, 61, 0.3); border: none; transition: 0.2s;
}
.btn-excel:hover { background: #166534; transform: translateY(-2px); }

/* Stats Cards */
.stats-grid { display: grid; grid-template-columns: repeat(4, 1fr); gap: 15px; }
.card { 
    background: var(--card-bg); padding: 15px 20px; border-radius: 15px; 
    border: 1px solid #E5E7EB; box-shadow: 0 4px 10px rgba(0,0,0,0.03);
    text-align: left;
}
.card-title { font-size: 12px; color: var(--text-muted); text-transform: uppercase; font-weight: 700; display: flex; align-items: center; gap: 8px; margin-bottom: 8px;}
.card-title i { font-size: 14px; }
.c-blue { color: var(--accent-blue); }
.c-red { color: var(--kma-red); }
.c-gold { color: var(--warning-gold); }
.c-brown { color: #8B4513; }

.value { font-size: 2.2rem; font-weight: 800; color: var(--accent-blue); display: flex; align-items:baseline; gap: 4px; }
.unit { font-size: 1.2rem; color: var(--text-muted); font-weight: 600; }

/* Camera View */
.camera-container { 
    position: relative; 
    background: #1a1a2e; 
    border-radius: 16px; 
    width: 100%;
    max-width: 640px;        /* Giới hạn chiều ngang */
    height: 420px;           /* Tăng chiều cao lên */
    aspect-ratio: 4/3;       /* Giữ tỉ lệ chuẩn camera */
    margin: 0 auto;          /* Căn giữa */
    overflow: hidden; 
    border: 2px solid #67E8F9;
    box-shadow: 0 0 20px rgba(103, 232, 249, 0.3), 0 8px 20px rgba(0,0,0,0.15);
    display: flex; 
    justify-content: center; 
    align-items: center;
}
.camera-container img { 
    width: 100%; 
    height: 100%; 
    object-fit: contain;     /* contain thay vì cover để không bị crop */
    z-index: 1;
}
.btn-stream { 
    position: absolute; z-index: 10; padding: 10px 20px; font-size: 0.95rem; font-weight: 700; 
    color: white; background: var(--kma-red); border: none; border-radius: 8px; 
    cursor: pointer; box-shadow: 0 4px 10px rgba(185, 28, 28, 0.4); 
    display: flex; align-items: center; gap: 8px; top: 15px; right: 15px;
}
.btn-stream.start { background: var(--accent-blue); box-shadow: 0 4px 10px rgba(2, 132, 199, 0.4); top: auto; right: auto; font-size: 1.2rem; padding: 15px 30px; border-radius: 30px;}
.stream-overlay { position: absolute; width: 100%; height: 100%; background: rgba(0,0,0,0.05); display: flex; justify-content: center; align-items: center; }

canvas { display: none; } /* Hide chart to match new UI clean look */
</style>
</head>
<body>

<div class="header-container">
    <div class="header-top">
        <img src="https://encrypted-tbn0.gstatic.com/images?q=tbn:ANd9GcQTxtaj31pJmPYii2bL7E7Y8TP47Pr0BcANZQ&s" alt="KMA Logo" class="logo">
        <h1 class="title">NGHIÊN CỨU TRIỂN KHAI HỆ THỐNG NHẬN DIỆN VÀ<br>GIÁM SÁT HỌC SINH TIỂU HỌC TRÊN XE ĐƯA ĐÓN</h1>
    </div>
    <div class="header-bottom">
        <div class="gvhd"><i class="fas fa-chalkboard-teacher"></i> GVHD: ThS Tô Thị Tuyết Nhung</div>
        <div class="badge-red">NHÓM 8 - SVTH</div>
        <div class="student-tag"><i class="fas fa-user-graduate"></i> Hà Văn Đạt (DT060209)</div>
        <div class="student-tag"><i class="fas fa-user-graduate"></i> Dương Hải Đăng (DT060206)</div>
        <div class="student-tag"><i class="fas fa-user-graduate"></i> Nguyễn Mạnh Lân (DT060231)</div>
        <div class="student-tag"><i class="fas fa-user-graduate"></i> Đặng Chí Tuệ (DT060248)</div>
    </div>
</div>

<div class="main">
    <div class="bus-wrapper">
        <div class="bus-frame">
            <div class="wind"></div>
            <div class="row"><div class="seat driver" id="seat0">TX</div><div class="seat" id="seat2">02</div><div class="seat" id="seat1">01</div></div>
            <div class="row"><div class="seat" id="seat5">05</div><div class="seat" id="seat4">04</div><div class="seat" id="seat3">03</div></div>
            <div class="row"><div class="seat" id="seat8">08</div><div class="seat" id="seat7">07</div><div class="seat" id="seat6">06</div></div>
            <div class="row"><div class="seat" id="seat11">11</div><div class="seat" id="seat10">10</div><div class="seat" id="seat9">09</div></div>
            <div class="row last"><div class="seat" id="seat12">12</div><div class="seat" id="seat13">13</div><div class="seat" id="seat14">14</div><div class="seat" id="seat15">15</div></div>
        </div>
    </div>

    <div class="right-panel">
        <div class="top-controls">
            <div class="time-box" id="time"><i class="far fa-calendar-alt"></i> Đang tải... | <i class="far fa-clock"></i> --:--:--</div>
            <a href="https://docs.google.com/spreadsheets/d/1pUllY81pjnAav5oK4usJxWI9V1wErOPNGfRhkntnnLE/edit?gid=0#gid=0" target="_blank" class="btn-excel"><i class="fas fa-file-excel"></i> Xuất Dữ Liệu Excel</a>
        </div>

        <div class="stats-grid">
            <div class="card">
                <div class="card-title c-blue"><i class="fas fa-users"></i> HÀNH KHÁCH</div>
                <div class="value"><span id="count">0</span><span class="unit">/15</span></div>
            </div>
            <div class="card">
                <div class="card-title c-red"><i class="fas fa-user-shield"></i> NHẬN DIỆN</div>
                <div class="value" id="face_status" style="color: var(--text-muted); font-size: 1.8rem;">Tắt</div>
            </div>
            <div class="card">
                <div class="card-title c-gold"><i class="fas fa-temperature-half"></i> NHIỆT ĐỘ</div>
                <div class="value"><span id="temp">25.4</span><span class="unit">°C</span></div>
            </div>
            <div class="card">
                <div class="card-title c-brown"><i class="fas fa-droplet"></i> ĐỘ ẨM</div>
                <div class="value"><span id="light">53</span><span class="unit">%</span></div>
            </div>
        </div>
        
        <div class="camera-container" id="camera-container">
            <div id="overlay" class="stream-overlay">
                <button id="stream-btn" class="btn-stream start" onclick="toggleStream()">
                    <i class="fas fa-play"></i> BẬT KẾT NỐI CAMERA
                </button>
            </div>
            <img id="stream">
        </div>
        <canvas id="chart"></canvas>
    </div>
</div>

<script>
// BIẾN QUẢN LÝ TRẠNG THÁI STREAM
let isStreaming = false;
const streamImg = document.getElementById('stream');
const streamBtn = document.getElementById('stream-btn');
const overlay = document.getElementById('overlay');

// HÀM BẬT/TẮT STREAM
function toggleStream() {
    if (!isStreaming) {
        streamImg.src = window.location.protocol + '//' + window.location.hostname + ':81/stream';
        streamBtn.innerHTML = '<i class="fas fa-video-slash"></i> NGẮT KẾT NỐI CAMERA';
        streamBtn.className = 'btn-stream'; // Đổi về nút đỏ góc phải
        overlay.style.background = 'transparent';
        isStreaming = true;
    } else {
        streamImg.src = ""; 
        streamBtn.innerHTML = '<i class="fas fa-play"></i> BẬT KẾT NỐI CAMERA';
        streamBtn.className = 'btn-stream start'; // Đổi về nút xanh to giữa màn hình
        overlay.style.background = 'rgba(0,0,0,0.05)';
        isStreaming = false;
    }
}

function updateTime() {
    const now = new Date(); const days =['Chủ Nhật', 'Thứ 2', 'Thứ 3', 'Thứ 4', 'Thứ 5', 'Thứ 6', 'Thứ 7'];
    document.getElementById("time").innerHTML = `<i class="far fa-calendar-alt"></i> ${days[now.getDay()]}, ${now.toLocaleDateString('vi-VN')} | <i class="far fa-clock"></i> ${now.toLocaleTimeString('vi-VN')}`;
}
setInterval(updateTime, 1000); updateTime(); 

let dataChart = Array(20).fill(0);

async function fetchData(){
    try {
        let response = await fetch('/data'); let json = await response.json(); let peopleCount = 0;
        for(let i=0; i<=15; i++) {
            let s = document.getElementById("seat" + i);
            if(s) {
                if(json.seats[i]) { 
                    s.classList.add("occupied"); 
                    if(i > 0) peopleCount++; 
                } 
                else { 
                    s.classList.remove("occupied"); 
                }
            }
        }
        document.getElementById("count").innerText = peopleCount;
        let faceDom = document.getElementById("face_status");
        
        if(!isStreaming) {
            faceDom.innerText = "Tắt"; 
            faceDom.style.color = "var(--text-muted)";
        } else {
            if(json.face) { 
                faceDom.innerText = "An Toàn"; 
                faceDom.style.color = "var(--success-green)"; 
            } 
            else { 
                
                faceDom.innerText = "Cảnh Báo"; 
                faceDom.style.color = "var(--kma-red)"; 
            }
        }

        document.getElementById("temp").innerText = (25 + Math.random()*1).toFixed(1);
        document.getElementById("light").innerText = Math.floor(50 + Math.random()*5);
    } catch(e) {}
}

setInterval(fetchData, 1000);
</script>
</body>
</html>
)rawliteral";
    
    httpd_resp_set_type(req, "text/html");
    return httpd_resp_send(req, html, HTTPD_RESP_USE_STRLEN);
}
    
void startCameraServer() {
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.max_uri_handlers = 16;

    httpd_uri_t index_uri = { .uri = "/", .method = HTTP_GET, .handler = index_handler, .user_ctx = NULL };
    httpd_uri_t stream_uri = { .uri = "/stream", .method = HTTP_GET, .handler = stream_handler, .user_ctx = NULL };
    httpd_uri_t data_uri = { .uri = "/data", .method = HTTP_GET, .handler = data_handler, .user_ctx = NULL };

    if (httpd_start(&camera_httpd, &config) == ESP_OK) {
        httpd_register_uri_handler(camera_httpd, &index_uri);
        httpd_register_uri_handler(camera_httpd, &data_uri); 
    }

    config.server_port += 1;
    config.ctrl_port += 1;
    if (httpd_start(&stream_httpd, &config) == ESP_OK) {
        httpd_register_uri_handler(stream_httpd, &stream_uri);
    }
}