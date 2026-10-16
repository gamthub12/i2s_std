#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/unistd.h>
#include <sys/stat.h>
#include <dirent.h>
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_http_server.h"
#include "esp_netif.h"
#include "esp_netif_sntp.h"
#include "esp_heap_caps.h"
#include "alarm_manager.h"
#include "alarm_web.h"
#include "sdkconfig.h"
#include "web_server.h"
#include "audio_player.h"
#include "sd_card.h"
#include "firebase_client.h"

static const char *TAG = "web_server";

static esp_err_t pairing_get_handler(httpd_req_t *req)
{
    char id[13], code[7], body[96];
    if (!firebase_client_pairing(id,sizeof(id),code,sizeof(code))) {
        httpd_resp_set_status(req, "503 Service Unavailable");
        return httpd_resp_sendstr(req, "Firebase M2 disabled");
    }
    snprintf(body,sizeof(body),"{\"deviceId\":\"%s\",\"code\":\"%s\"}",id,code);
    httpd_resp_set_type(req,"application/json");
    httpd_resp_set_hdr(req,"Cache-Control","no-store");
    return httpd_resp_sendstr(req,body);
}
extern const char alarms_html_start[] asm("_binary_alarms_html_start");
static esp_err_t pair_post_handler(httpd_req_t *req)
{
    if (req->content_len<1 || req->content_len>4096)
        return httpd_resp_send_err(req,HTTPD_400_BAD_REQUEST,"Invalid pairing request");
    char *body=calloc(1,req->content_len+1);
    if (!body) return httpd_resp_send_err(req,HTTPD_500_INTERNAL_SERVER_ERROR,"Memory unavailable");
    int used=0;
    while (used<req->content_len) {
        int n=httpd_req_recv(req,body+used,req->content_len-used);
        if (n<=0) { free(body); return ESP_FAIL; }
        used+=n;
    }
    int status=firebase_client_pair_request(body);
    free(body);
    httpd_resp_set_type(req,"application/json");
    httpd_resp_set_hdr(req,"Cache-Control","no-store");
    const char *message;
    switch (status) {
    case 200: message="{\"state\":\"paired\"}"; break;
    case 400: httpd_resp_set_status(req,"400 Bad Request"); message="{\"error\":\"Invalid device ID or pairing request\"}"; break;
    case 401: httpd_resp_set_status(req,"401 Unauthorized"); message="{\"error\":\"Firebase account token invalid\"}"; break;
    case 403: httpd_resp_set_status(req,"403 Forbidden"); message="{\"error\":\"Wrong code or too many attempts; restart board to renew code\"}"; break;
    case 409: httpd_resp_set_status(req,"409 Conflict"); message="{\"error\":\"Device already belongs to another account\"}"; break;
    default: httpd_resp_set_status(req,"503 Service Unavailable"); message="{\"error\":\"Device cloud connection not ready; retry shortly\"}"; break;
    }
    return httpd_resp_sendstr(req,message);
}
extern const char alarms_html_end[] asm("_binary_alarms_html_end");
#define MOUNT_POINT "/sdcard"

/* Default Premium HTML/CSS/JS interface */
static const char index_html_content[] = 
"<!DOCTYPE html>\n"
"<html lang=\"vi\">\n"
"<head>\n"
"    <meta charset=\"UTF-8\">\n"
"    <meta name=\"viewport\" content=\"width=device-width, initial-scale=1.0\">\n"
"    <title>ESP32-S3 SD Card File Manager</title>\n"
"    <link href=\"https://fonts.googleapis.com/css2?family=Inter:wght@300;400;500;600;700&display=swap\" rel=\"stylesheet\">\n"
"    <style>\n"
"        :root {\n"
"            --bg-color: #0f172a;\n"
"            --card-bg: #1e293b;\n"
"            --accent-color: #3b82f6;\n"
"            --accent-hover: #2563eb;\n"
"            --text-primary: #f8fafc;\n"
"            --text-secondary: #94a3b8;\n"
"            --danger-color: #ef4444;\n"
"            --danger-hover: #dc2626;\n"
"            --border-color: #334155;\n"
"            --success-color: #10b981;\n"
"        }\n"
"        * {\n"
"            box-sizing: border-box;\n"
"            margin: 0;\n"
"            padding: 0;\n"
"        }\n"
"        body {\n"
"            font-family: 'Inter', sans-serif;\n"
"            background-color: var(--bg-color);\n"
"            color: var(--text-primary);\n"
"            display: flex;\n"
"            flex-direction: column;\n"
"            align-items: center;\n"
"            min-height: 100vh;\n"
"            padding: 2rem 1rem;\n"
"        }\n"
"        .container {\n"
"            width: 100%;\n"
"            max-width: 800px;\n"
"            display: flex;\n"
"            flex-direction: column;\n"
"            gap: 2rem;\n"
"        }\n"
"        header {\n"
"            text-align: center;\n"
"            margin-bottom: 1rem;\n"
"        }\n"
"        header h1 {\n"
"            font-size: 2.25rem;\n"
"            font-weight: 700;\n"
"            margin-bottom: 0.5rem;\n"
"            background: linear-gradient(to right, #3b82f6, #8b5cf6);\n"
"            -webkit-background-clip: text;\n"
"            -webkit-text-fill-color: transparent;\n"
"        }\n"
"        header p {\n"
"            color: var(--text-secondary);\n"
"            font-size: 1rem;\n"
"        }\n"
"        .card {\n"
"            background-color: var(--card-bg);\n"
"            border-radius: 16px;\n"
"            border: 1px solid var(--border-color);\n"
"            padding: 2rem;\n"
"            box-shadow: 0 10px 15px -3px rgba(0, 0, 0, 0.3), 0 4px 6px -4px rgba(0, 0, 0, 0.3);\n"
"        }\n"
"        .card-title {\n"
"            font-size: 1.25rem;\n"
"            font-weight: 600;\n"
"            margin-bottom: 1.5rem;\n"
"            display: flex;\n"
"            align-items: center;\n"
"            gap: 0.5rem;\n"
"        }\n"
"        #drop-zone {\n"
"            border: 2px dashed #475569;\n"
"            border-radius: 12px;\n"
"            padding: 3rem 1.5rem;\n"
"            text-align: center;\n"
"            cursor: pointer;\n"
"            transition: all 0.3s ease;\n"
"            background-color: rgba(15, 23, 42, 0.2);\n"
"            position: relative;\n"
"        }\n"
"        #drop-zone.dragover {\n"
"            border-color: var(--accent-color);\n"
"            background-color: rgba(59, 130, 246, 0.1);\n"
"        }\n"
"        #drop-zone svg {\n"
"            width: 48px;\n"
"            height: 48px;\n"
"            fill: var(--text-secondary);\n"
"            margin-bottom: 1rem;\n"
"            transition: transform 0.3s ease;\n"
"            display: inline-block;\n"
"        }\n"
"        #drop-zone:hover svg {\n"
"            transform: translateY(-5px);\n"
"            fill: var(--accent-color);\n"
"        }\n"
"        #drop-zone p {\n"
"            font-size: 1rem;\n"
"            font-weight: 500;\n"
"            margin-bottom: 0.25rem;\n"
"        }\n"
"        #drop-zone span {\n"
"            font-size: 0.875rem;\n"
"            color: var(--text-secondary);\n"
"        }\n"
"        #file-input {\n"
"            display: none;\n"
"        }\n"
"        .progress-container {\n"
"            margin-top: 1.5rem;\n"
"            display: none;\n"
"            flex-direction: column;\n"
"            gap: 0.5rem;\n"
"        }\n"
"        .progress-bar-bg {\n"
"            background-color: var(--bg-color);\n"
"            border-radius: 9999px;\n"
"            height: 8px;\n"
"            overflow: hidden;\n"
"            width: 100%;\n"
"        }\n"
"        .progress-bar-fill {\n"
"            background-color: var(--accent-color);\n"
"            height: 100%;\n"
"            width: 0%;\n"
"            transition: width 0.1s ease;\n"
"        }\n"
"        .progress-text {\n"
"            display: flex;\n"
"            justify-content: space-between;\n"
"            font-size: 0.875rem;\n"
"            color: var(--text-secondary);\n"
"        }\n"
"        table {\n"
"            width: 100%;\n"
"            border-collapse: collapse;\n"
"            text-align: left;\n"
"        }\n"
"        th, td {\n"
"            padding: 1rem;\n"
"            border-bottom: 1px solid var(--border-color);\n"
"        }\n"
"        th {\n"
"            color: var(--text-secondary);\n"
"            font-weight: 600;\n"
"            font-size: 0.875rem;\n"
"            text-transform: uppercase;\n"
"            letter-spacing: 0.05em;\n"
"        }\n"
"        td {\n"
"            font-size: 0.975rem;\n"
"        }\n"
"        tr:last-child td {\n"
"            border-bottom: none;\n"
"        }\n"
"        .btn {\n"
"            display: inline-flex;\n"
"            align-items: center;\n"
"            justify-content: center;\n"
"            padding: 0.5rem 1rem;\n"
"            border-radius: 8px;\n"
"            font-weight: 500;\n"
"            font-size: 0.875rem;\n"
"            cursor: pointer;\n"
"            transition: all 0.2s ease;\n"
"            border: none;\n"
"            text-decoration: none;\n"
"            gap: 0.25rem;\n"
"        }\n"
"        .btn-primary {\n"
"            background-color: var(--accent-color);\n"
"            color: white;\n"
"        }\n"
"        .btn-primary:hover {\n"
"            background-color: var(--accent-hover);\n"
"        }\n"
"        .btn-danger {\n"
"            background-color: rgba(239, 68, 68, 0.1);\n"
"            color: var(--danger-color);\n"
"        }\n"
"        .btn-danger:hover {\n"
"            background-color: var(--danger-color);\n"
"            color: white;\n"
"        }\n"
"        .btn-success {\n"
"            background-color: var(--success-color);\n"
"            color: white;\n"
"        }\n"
"        .btn-success:hover {\n"
"            background-color: #059669;\n"
"        }\n"
"        .action-cell {\n"
"            display: flex;\n"
"            gap: 0.5rem;\n"
"            justify-content: flex-end;\n"
"        }\n"
"        .empty-list {\n"
"            text-align: center;\n"
"            color: var(--text-secondary);\n"
"            padding: 3rem 0;\n"
"            font-size: 1rem;\n"
"        }\n"
"        #toast {\n"
"            position: fixed;\n"
"            bottom: 2rem;\n"
"            right: 2rem;\n"
"            background-color: var(--card-bg);\n"
"            border: 1px solid var(--border-color);\n"
"            border-left: 4px solid var(--accent-color);\n"
"            padding: 1rem 1.5rem;\n"
"            border-radius: 8px;\n"
"            box-shadow: 0 10px 15px -3px rgba(0, 0, 0, 0.5);\n"
"            display: none;\n"
"            animation: slideIn 0.3s ease forwards;\n"
"            font-weight: 500;\n"
"            z-index: 1000;\n"
"        }\n"
"        @keyframes slideIn {\n"
"            from { transform: translateX(100%); opacity: 0; }\n"
"            to { transform: translateX(0); opacity: 1; }\n"
"        }\n"
"    </style>\n"
"</head>\n"
"<body>\n"
"    <div class=\"container\">\n"
"        <header>\n"
"            <h1>ESP32-S3 File Server</h1>\n"
"            <p>Kết nối Wi-Fi • Quản lý tệp tin trực tiếp trên thẻ nhớ SD</p>\n"
"        </header>\n"
"        <p><a href=\"/alarms\" style=\"color:#93c5fd\">Mở giao diện báo thức</a></p>\n"
"\n"
"        <div class=\"card\">\n"
"            <div class=\"card-title\">\n"
"                <svg width=\"20\" height=\"20\" viewBox=\"0 0 24 24\" fill=\"none\" stroke=\"currentColor\" stroke-width=\"2\"><path d=\"M21 15v4a2 2 0 0 1-2 2H5a2 2 0 0 1-2-2v-4M17 8l-5-5-5 5M12 3v12\"/></svg>\n"
"                Tải tệp lên thẻ SD\n"
"            </div>\n"
"            <div id=\"drop-zone\">\n"
"                <input type=\"file\" id=\"file-input\">\n"
"                <svg viewBox=\"0 0 24 24\" fill=\"currentColor\"><path d=\"M19.35 10.04C18.67 6.59 15.64 4 12 4 9.11 4 6.6 5.64 5.35 8.04 2.34 8.36 0 10.91 0 14c0 3.31 2.69 6 6 6h13c2.76 0 5-2.24 5-5 0-2.64-2.05-4.78-4.65-4.96zM14 13v4h-4v-4H7l5-5 5 5h-3z\"/></svg>\n"
"                <p>Kéo thả tệp vào đây hoặc nhấn để chọn tệp</p>\n"
"                <span>Hỗ trợ mọi định dạng tệp tin</span>\n"
"            </div>\n"
"\n"
"            <div class=\"progress-container\" id=\"progress-container\">\n"
"                <div class=\"progress-text\">\n"
"                    <span id=\"progress-filename\">Uploading file...</span>\n"
"                    <span id=\"progress-percent\">0%</span>\n"
"                </div>\n"
"                <div class=\"progress-bar-bg\">\n"
"                    <div class=\"progress-bar-fill\" id=\"progress-bar-fill\"></div>\n"
"                </div>\n"
"            </div>\n"
"        </div>\n"
"\n"
"        <div class=\"card\">\n"
"            <div class=\"card-title\">\n"
"                <svg width=\"20\" height=\"20\" viewBox=\"0 0 24 24\" fill=\"none\" stroke=\"currentColor\" stroke-width=\"2\"><path d=\"M9 18V5l12-2v13\"/></svg>\n"
"                🎵 Audio Player\n"
"            </div>\n"
"            <div id=\"player-status\">\n"
"                <p style=\"color: var(--text-secondary); margin-bottom: 1rem;\">\n"
"                    <span id=\"now-playing-label\">⏹ Đã dừng</span>\n"
"                </p>\n"
"                <div style=\"display: flex; gap: 0.5rem;\">\n"
"                    <button class=\"btn btn-danger\" onclick=\"stopPlayback()\" id=\"stop-btn\" style=\"display: none;\">⏹ Dừng</button>\n"
"                </div>\n"
"            </div>\n"
"        </div>\n"
"\n"
"        <div class=\"card\">\n"
"            <div class=\"card-title\">\n"
"                <svg width=\"20\" height=\"20\" viewBox=\"0 0 24 24\" fill=\"none\" stroke=\"currentColor\" stroke-width=\"2\"><path d=\"M22 19a2 2 0 0 1-2 2H4a2 2 0 0 1-2-2V5a2 2 0 0 1 2-2h5l2 3h9a2 2 0 0 1 2 2z\"/></svg>\n"
"                Tệp tin trên thẻ nhớ SD\n"
"            </div>\n"
"            <div id=\"file-list-container\">\n"
"                <table id=\"file-table\" style=\"display: none;\">\n"
"                    <thead>\n"
"                        <tr>\n"
"                            <th>Tên tệp</th>\n"
"                            <th>Kích thước</th>\n"
"                            <th style=\"text-align: right;\">Thao tác</th>\n"
"                        </tr>\n"
"                    </thead>\n"
"                    <tbody id=\"file-list-body\"></tbody>\n"
"                </table>\n"
"                <div id=\"empty-message\" class=\"empty-list\">Đang quét danh sách tệp hoặc không có tệp nào trên thẻ nhớ...</div>\n"
"            </div>\n"
"        </div>\n"
"    </div>\n"
"\n"
"    <div id=\"toast\">Thông báo</div>\n"
"\n"
"    <script>\n"
"        const dropZone = document.getElementById('drop-zone');\n"
"        const fileInput = document.getElementById('file-input');\n"
"        const progressContainer = document.getElementById('progress-container');\n"
"        const progressBarFill = document.getElementById('progress-bar-fill');\n"
"        const progressFilename = document.getElementById('progress-filename');\n"
"        const progressPercent = document.getElementById('progress-percent');\n"
"        const fileTable = document.getElementById('file-table');\n"
"        const fileListBody = document.getElementById('file-list-body');\n"
"        const emptyMessage = document.getElementById('empty-message');\n"
"        const toast = document.getElementById('toast');\n"
"        const nowPlayingLabel = document.getElementById('now-playing-label');\n"
"        const stopBtn = document.getElementById('stop-btn');\n"
"        let statusRequestId = 0;\n"
"        let lastAppliedStatusId = 0;\n"
"\n"
"        function showToast(message, isError = false) {\n"
"            toast.innerText = message;\n"
"            toast.style.borderLeftColor = isError ? 'var(--danger-color)' : 'var(--success-color)';\n"
"            toast.style.display = 'block';\n"
"            setTimeout(() => { toast.style.display = 'none'; }, 3000);\n"
"        }\n"
"\n"
"        function formatBytes(bytes) {\n"
"            if (bytes === 0) return '0 Bytes';\n"
"            const k = 1024;\n"
"            const sizes = ['Bytes', 'KB', 'MB', 'GB'];\n"
"            const i = Math.floor(Math.log(bytes) / Math.log(k));\n"
"            return parseFloat((bytes / Math.pow(k, i)).toFixed(2)) + ' ' + sizes[i];\n"
"        }\n"
"\n"
"        async function updatePlayerStatus() {\n"
"            const requestId = ++statusRequestId;\n"
"            try {\n"
"                const resp = await fetch('/status', { cache: 'no-store' });\n"
"                const data = await resp.json();\n"
"                if (requestId < lastAppliedStatusId) return;\n"
"                lastAppliedStatusId = requestId;\n"
"                if (data.playing) {\n"
"                    const fname = data.file.split('/').pop();\n"
"                    nowPlayingLabel.innerText = '▶ Đang phát: ' + fname;\n"
"                    stopBtn.style.display = 'inline-flex';\n"
"                } else {\n"
"                    nowPlayingLabel.innerText = '⏹ Đã dừng';\n"
"                    stopBtn.style.display = 'none';\n"
"                }\n"
"            } catch(e) {}\n"
"        }\n"
"\n"
"        async function loadFileList() {\n"
"            try {\n"
"                const response = await fetch('/list');\n"
"                const files = await response.json();\n"
"                fileListBody.innerHTML = '';\n"
"                \n"
"                if (files.length === 0) {\n"
"                    fileTable.style.display = 'none';\n"
"                    emptyMessage.style.display = 'block';\n"
"                    emptyMessage.innerText = 'Thẻ nhớ trống.';\n"
"                    return;\n"
"                }\n"
"\n"
"                emptyMessage.style.display = 'none';\n"
"                fileTable.style.display = 'table';\n"
"\n"
"                files.forEach(file => {\n"
"                    const isWav = file.name.toLowerCase().endsWith('.wav');\n"
"                    const tr = document.createElement('tr');\n"
"                    tr.innerHTML = `\n"
"                        <td style=\"font-weight: 500;\">${file.name}</td>\n"
"                        <td style=\"color: var(--text-secondary);\">${formatBytes(file.size)}</td>\n"
"                        <td class=\"action-cell\">\n"
"                            ${isWav ? `<button class=\"btn btn-success\" onclick=\"playFile('${file.name}')\">▶ Phát</button>` : ''}\n"
"                            <a href=\"/download?filename=${encodeURIComponent(file.name)}\" class=\"btn btn-primary\" download>\n"
"                                Tải xuống\n"
"                            </a>\n"
"                            <button class=\"btn btn-danger\" onclick=\"deleteFile('${file.name}')\">Xóa</button>\n"
"                        </td>\n"
"                    `;\n"
"                    fileListBody.appendChild(tr);\n"
"                });\n"
"                updatePlayerStatus();\n"
"            } catch (error) {\n"
"                console.error('Error loading files:', error);\n"
"                emptyMessage.innerText = 'Lỗi tải danh sách tệp. Thẻ nhớ đã được cắm chưa?';\n"
"            }\n"
"        }\n"
"\n"
"        async function playFile(filename) {\n"
"            try {\n"
"                const resp = await fetch(`/play?filename=${encodeURIComponent(filename)}`, { method: 'POST' });\n"
"                if (resp.ok) {\n"
"                    showToast('▶ Đang phát: ' + filename);\n"
"                    updatePlayerStatus();\n"
"                } else {\n"
"                    showToast('Lỗi phát tệp', true);\n"
"                }\n"
"            } catch(e) {\n"
"                showToast('Lỗi kết nối server', true);\n"
"            }\n"
"        }\n"
"\n"
"        async function stopPlayback() {\n"
"            if (stopBtn.disabled) return;\n"
"            stopBtn.disabled = true;\n"
"            try {\n"
"                const resp = await fetch('/stop', { method: 'POST' });\n"
"                if (resp.ok) {\n"
"                    showToast('⏹ Đã dừng');\n"
"                    updatePlayerStatus();\n"
"                }\n"
"            } catch(e) {\n"
"                showToast('Cannot reach server', true);\n"
"            } finally {\n"
"                stopBtn.disabled = false;\n"
"            }\n"
"        }\n"
"\n"
"        async function deleteFile(filename) {\n"
"            if (!confirm(`Bạn có chắc chắn muốn xóa tệp \"${filename}\" không?`)) return;\n"
"            try {\n"
"                const response = await fetch(`/delete?filename=${encodeURIComponent(filename)}`, { method: 'POST' });\n"
"                if (response.ok) {\n"
"                    showToast('Đã xóa tệp thành công');\n"
"                    loadFileList();\n"
"                } else {\n"
"                    showToast('Lỗi xóa tệp', true);\n"
"                }\n"
"            } catch (error) {\n"
"                showToast('Lỗi kết nối server', true);\n"
"            }\n"
"        }\n"
"\n"
"        dropZone.addEventListener('click', () => fileInput.click());\n"
"        dropZone.addEventListener('dragover', (e) => {\n"
"            e.preventDefault();\n"
"            dropZone.classList.add('dragover');\n"
"        });\n"
"        dropZone.addEventListener('dragleave', () => dropZone.classList.remove('dragover'));\n"
"        dropZone.addEventListener('drop', (e) => {\n"
"            e.preventDefault();\n"
"            dropZone.classList.remove('dragover');\n"
"            if (e.dataTransfer.files.length > 0) {\n"
"                uploadFile(e.dataTransfer.files[0]);\n"
"            }\n"
"        });\n"
"\n"
"        fileInput.addEventListener('change', () => {\n"
"            if (fileInput.files.length > 0) {\n"
"                uploadFile(fileInput.files[0]);\n"
"            }\n"
"        });\n"
"\n"
"        function uploadFile(file) {\n"
"            const xhr = new XMLHttpRequest();\n"
"            progressContainer.style.display = 'flex';\n"
"            progressFilename.innerText = `Đang tải lên: ${file.name}`;\n"
"            \n"
"            xhr.upload.addEventListener('progress', (e) => {\n"
"                if (e.lengthComputable) {\n"
"                    const percent = Math.round((e.loaded / e.total) * 100);\n"
"                    progressBarFill.style.width = percent + '%';\n"
"                    progressPercent.innerText = percent + '%';\n"
"                }\n"
"            });\n"
"\n"
"            xhr.addEventListener('load', () => {\n"
"                progressContainer.style.display = 'none';\n"
"                if (xhr.status === 200) {\n"
"                    showToast('Tải lên tệp thành công!');\n"
"                    loadFileList();\n"
"                } else {\n"
"                    showToast('Lỗi tải tệp lên server', true);\n"
"                }\n"
"            });\n"
"\n"
"            xhr.addEventListener('error', () => {\n"
"                progressContainer.style.display = 'none';\n"
"                showToast('Lỗi kết nối mạng khi tải lên', true);\n"
"            });\n"
"\n"
"            const uploadUrl = `/upload?filename=${encodeURIComponent(file.name)}`;\n"
"            xhr.open('POST', uploadUrl, true);\n"
"            xhr.setRequestHeader('Content-Type', 'application/octet-stream');\n"
"            xhr.send(file);\n"
"        }\n"
"\n"
"        window.addEventListener('DOMContentLoaded', () => {\n"
"            updatePlayerStatus();\n"
"            loadFileList();\n"
"            setInterval(updatePlayerStatus, 1000);\n"
"        });\n"
"    </script>\n"
"</body>\n"
"</html>";

/* API: GET / -> Serves index.html directly from embedded memory (no SD card needed) */
static esp_err_t root_get_handler(httpd_req_t *req)
{
    httpd_resp_set_type(req, "text/html; charset=utf-8");
    /* Force browser to never cache this page */
    httpd_resp_set_hdr(req, "Cache-Control", "no-cache, no-store, must-revalidate");
    httpd_resp_set_hdr(req, "Pragma", "no-cache");
    httpd_resp_set_hdr(req, "Expires", "0");

    ESP_LOGI(TAG, "Serving index.html from embedded memory, total: %d bytes", (int)strlen(index_html_content));

    /* Use sendstr to send the entire HTML in one shot (most reliable method) */
    httpd_resp_sendstr(req, index_html_content);
    return ESP_OK;
}

/* Only accept a single safe path component, even after URL decoding. */
static bool get_safe_filename(httpd_req_t *req, char *filename, size_t filename_size)
{
    char query[256];
    char encoded[160];
    if (filename_size == 0 ||
        httpd_req_get_url_query_str(req, query, sizeof(query)) != ESP_OK ||
        httpd_query_key_value(query, "filename", encoded, sizeof(encoded)) != ESP_OK) {
        return false;
    }

    size_t out = 0;
    for (size_t i = 0; encoded[i] != '\0';) {
        unsigned char ch = (unsigned char)encoded[i++];
        if (ch == '%') {
            if (encoded[i] == '\0' || encoded[i + 1] == '\0') return false;
            char hex[3] = { encoded[i], encoded[i + 1], '\0' };
            char *end = NULL;
            long value = strtol(hex, &end, 16);
            if (end == hex || *end != '\0' || value == 0) return false;
            ch = (unsigned char)value;
            i += 2;
        } else if (ch == '+') {
            ch = ' ';
        }

        if (ch < 32 || ch == 127 || ch == '/' || ch == '\\' || ch == ':') return false;
        if (out + 1 >= filename_size) return false;
        filename[out++] = (char)ch;
    }
    filename[out] = '\0';
    return out > 0 && strcmp(filename, ".") != 0 && strcmp(filename, "..") != 0;
}

static esp_err_t require_safe_filename(httpd_req_t *req, char *filename, size_t filename_size)
{
    if (get_safe_filename(req, filename, filename_size)) return ESP_OK;
    httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid or missing filename");
    return ESP_FAIL;
}

static bool json_escape(const char *src, char *dst, size_t dst_size)
{
    size_t out = 0;
    for (size_t i = 0; src[i] != '\0'; ++i) {
        unsigned char ch = (unsigned char)src[i];
        if (ch == '"' || ch == '\\') {
            if (out + 2 >= dst_size) return false;
            dst[out++] = '\\';
            dst[out++] = (char)ch;
        } else if (ch < 32) {
            if (out + 6 >= dst_size) return false;
            int n = snprintf(dst + out, dst_size - out, "\\u%04x", ch);
            if (n != 6) return false;
            out += 6;
        } else {
            if (out + 1 >= dst_size) return false;
            dst[out++] = (char)ch;
        }
    }
    dst[out] = '\0';
    return true;
}

static esp_err_t send_sd_unavailable(httpd_req_t *req)
{
    httpd_resp_set_status(req, "503 Service Unavailable");
    httpd_resp_set_type(req, "text/plain; charset=utf-8");
    httpd_resp_sendstr(req, "SD card is not mounted");
    return ESP_FAIL;
}

/* API: GET /list -> Returns files on SD Card in JSON format */
static esp_err_t list_get_handler(httpd_req_t *req)
{
    if (!sd_card_lock()) {
        return send_sd_unavailable(req);
    }
    DIR *dir = opendir(MOUNT_POINT);
    if (dir == NULL) {
        sd_card_unlock();
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed to open directory (SD card not inserted?)");
        return ESP_FAIL;
    }

    httpd_resp_set_type(req, "application/json; charset=utf-8");
    httpd_resp_send_chunk(req, "[", 1);

    struct dirent *entry;
    bool first = true;
    char item_buf[640];

    while ((entry = readdir(dir)) != NULL) {
        // Skip hidden files and index.html (optional, we show it or skip it, let's skip index.html for cleaner look)
        if (strcmp(entry->d_name, "index.html") == 0 || entry->d_name[0] == '.') {
            continue;
        }

        // Get file size
        char path[300];
        snprintf(path, sizeof(path), "%s/%s", MOUNT_POINT, entry->d_name);
        struct stat st;
        long long size = 0;
        if (stat(path, &st) == 0) {
            size = st.st_size;
        }

        if (!first) {
            httpd_resp_send_chunk(req, ",", 1);
        }
        first = false;

        char escaped_name[512];
        if (!json_escape(entry->d_name, escaped_name, sizeof(escaped_name))) continue;
        int len = snprintf(item_buf, sizeof(item_buf), "{\"name\":\"%s\",\"size\":%lld}", escaped_name, size);
        if (httpd_resp_send_chunk(req, item_buf, len) != ESP_OK) {
            closedir(dir);
            sd_card_unlock();
            return ESP_FAIL;
        }
    }
    closedir(dir);
    sd_card_unlock();

    httpd_resp_send_chunk(req, "]", 1);
    httpd_resp_send_chunk(req, NULL, 0);
    return ESP_OK;
}

/* API: POST /upload?filename=... -> Upload file to SD Card */
static esp_err_t upload_post_handler(httpd_req_t *req)
{
    char filename[128] = {0};
    if (require_safe_filename(req, filename, sizeof(filename)) != ESP_OK) return ESP_FAIL;

    ESP_LOGI(TAG, "Uploading file: %s", filename);

    if (!sd_card_lock()) {
        return send_sd_unavailable(req);
    }

    char filepath[256];
    snprintf(filepath, sizeof(filepath), "%s/%s", MOUNT_POINT, filename);

    char *buf = malloc(2048);
    if (buf == NULL) {
        sd_card_unlock();
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Out of memory");
        return ESP_FAIL;
    }

    FILE *f = fopen(filepath, "wb");
    if (f == NULL) {
        free(buf);
        sd_card_unlock();
        ESP_LOGE(TAG, "Failed to open file for writing: %s", filepath);
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed to create file on SD Card");
        return ESP_FAIL;
    }

    size_t remaining = req->content_len;
    size_t received = 0;

    while (remaining > 0) {
        int want = (remaining < 2048) ? (int)remaining : 2048;
        int read_len = httpd_req_recv(req, buf, want);
        if (read_len <= 0) {
            if (read_len == HTTPD_SOCK_ERR_TIMEOUT) {
                continue; // Retry
            }
            ESP_LOGE(TAG, "Socket error or timeout during upload");
            fclose(f);
            free(buf);
            unlink(filepath); // Delete incomplete file
            sd_card_unlock();
            httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Connection lost during upload");
            return ESP_FAIL;
        }

        if (fwrite(buf, 1, read_len, f) != (size_t)read_len) {
            fclose(f);
            free(buf);
            unlink(filepath);
            sd_card_unlock();
            httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed to write file");
            return ESP_FAIL;
        }
        remaining -= (size_t)read_len;
        received += (size_t)read_len;
    }

    bool close_ok = fclose(f) == 0;
    if (!close_ok) unlink(filepath);
    free(buf);
    sd_card_unlock();
    if (!close_ok) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed to finish writing file");
        return ESP_FAIL;
    }
    ESP_LOGI(TAG, "Upload completed successfully. Total bytes: %u", (unsigned)received);

    httpd_resp_sendstr(req, "Upload successful");
    return ESP_OK;
}

/* API: GET /download?filename=... -> Download file from SD Card */
static esp_err_t download_get_handler(httpd_req_t *req)
{
    char filename[128] = {0};
    if (require_safe_filename(req, filename, sizeof(filename)) != ESP_OK) return ESP_FAIL;

    char filepath[256];
    snprintf(filepath, sizeof(filepath), "%s/%s", MOUNT_POINT, filename);

    if (!sd_card_lock()) {
        return send_sd_unavailable(req);
    }

    FILE *f = fopen(filepath, "rb");
    if (f == NULL) {
        sd_card_unlock();
        httpd_resp_send_err(req, HTTPD_404_NOT_FOUND, "File not found");
        return ESP_FAIL;
    }

    // Set download headers
    httpd_resp_set_type(req, "application/octet-stream");
    char disposition[256];
    snprintf(disposition, sizeof(disposition), "attachment; filename=\"%s\"", filename);
    httpd_resp_set_hdr(req, "Content-Disposition", disposition);

    char *chunk = malloc(2048);
    if (chunk == NULL) {
        fclose(f);
        sd_card_unlock();
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Out of memory");
        return ESP_FAIL;
    }
    size_t read_bytes;

    while ((read_bytes = fread(chunk, 1, 2048, f)) > 0) {
        if (httpd_resp_send_chunk(req, chunk, read_bytes) != ESP_OK) {
            free(chunk);
            fclose(f);
            sd_card_unlock();
            return ESP_FAIL;
        }
    }
    free(chunk);
    fclose(f);
    sd_card_unlock();
    httpd_resp_send_chunk(req, NULL, 0);
    return ESP_OK;
}

/* API: POST /delete?filename=... -> Delete file from SD Card */
static esp_err_t delete_post_handler(httpd_req_t *req)
{
    char filename[128] = {0};
    if (require_safe_filename(req, filename, sizeof(filename)) != ESP_OK) return ESP_FAIL;

    char filepath[256];
    snprintf(filepath, sizeof(filepath), "%s/%s", MOUNT_POINT, filename);

    if (!sd_card_lock()) {
        return send_sd_unavailable(req);
    }

    struct stat st;
    if (stat(filepath, &st) != 0) {
        sd_card_unlock();
        httpd_resp_send_err(req, HTTPD_404_NOT_FOUND, "File not found");
        return ESP_FAIL;
    }

    if (unlink(filepath) != 0) {
        sd_card_unlock();
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed to delete file");
        return ESP_FAIL;
    }

    sd_card_unlock();
    ESP_LOGI(TAG, "File deleted: %s", filepath);
    httpd_resp_sendstr(req, "File deleted successfully");
    return ESP_OK;
}

/* API: POST /play?filename=... -> Play a WAV file from SD Card */
static esp_err_t play_post_handler(httpd_req_t *req)
{
    char filename[128] = {0};
    if (require_safe_filename(req, filename, sizeof(filename)) != ESP_OK) return ESP_FAIL;

    /* Queue the new path without taking the SD lock here. Playback owns that
     * lock while reading the current file; waiting for it here would make the
     * browser wait for the whole current track before switching. The player
     * task opens the requested file as soon as it stops the current track. */
    char filepath[300];
    snprintf(filepath, sizeof(filepath), "%s/%s", MOUNT_POINT, filename);

    ESP_LOGI(TAG, "Play file: %s", filepath);
    alarm_manager_play_file(filepath);
    httpd_resp_set_status(req, "202 Accepted");
    httpd_resp_sendstr(req, "Playback switch queued");
    return ESP_OK;
}

/* API: POST /stop -> Stop audio playback */
static esp_err_t stop_post_handler(httpd_req_t *req)
{
    ESP_LOGI(TAG, "Stop playback");
    alarm_manager_stop();
    httpd_resp_sendstr(req, "Stopped");
    return ESP_OK;
}

/* API: GET /status -> Returns audio playback status as JSON */
static esp_err_t status_get_handler(httpd_req_t *req)
{
    char buf[512];
    char escaped_file[384];
    bool playing;
    char current[192];
    audio_player_get_status(&playing, current, sizeof(current));
    if (!json_escape(current, escaped_file, sizeof(escaped_file))) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Playback state too long");
        return ESP_FAIL;
    }
    snprintf(buf, sizeof(buf), "{\"playing\":%s,\"file\":\"%s\"}", playing ? "true" : "false", escaped_file);
    httpd_resp_set_type(req, "application/json; charset=utf-8");
    httpd_resp_sendstr(req, buf);
    return ESP_OK;
}

/* API: GET /health -> M0 device and memory diagnostics. */
static esp_err_t health_get_handler(httpd_req_t *req)
{
    bool sd_mounted = sd_card_lock();
    if (sd_mounted) sd_card_unlock();

    const uint32_t internal_caps = MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT;
    char buf[512], firebase[240];
    firebase_client_metrics(firebase,sizeof(firebase));
    snprintf(buf, sizeof(buf),
             "{\"sdMounted\":%s,\"internalFree\":%u,\"internalMinimum\":%u,"
             "\"internalLargest\":%u,\"psramFree\":%u,\"firebase\":%s}",
             sd_mounted ? "true" : "false",
             (unsigned)heap_caps_get_free_size(internal_caps),
             (unsigned)heap_caps_get_minimum_free_size(internal_caps),
             (unsigned)heap_caps_get_largest_free_block(internal_caps),
             (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM),firebase);
    httpd_resp_set_type(req, "application/json; charset=utf-8");
    httpd_resp_sendstr(req, buf);
    return ESP_OK;
}

static esp_err_t alarms_page_handler(httpd_req_t *req)
{
    httpd_resp_set_type(req, "text/html; charset=utf-8");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    return httpd_resp_send(req, alarms_html_start, alarms_html_end - alarms_html_start);
}

/* Event handler for Wi-Fi events */
static void wifi_event_handler(void* arg, esp_event_base_t event_base,
                               int32_t event_id, void* event_data)
{
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        ESP_LOGI(TAG, "Retrying connection to AP...");
        esp_wifi_connect();
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t* event = (ip_event_got_ip_t*) event_data;
        ESP_LOGI(TAG, "===============================================");
        ESP_LOGI(TAG, "KẾT NỐI WIFI THÀNH CÔNG!");
        ESP_LOGI(TAG, "Địa chỉ IP của ESP32-S3: " IPSTR, IP2STR(&event->ip_info.ip));
        ESP_LOGI(TAG, "Vui lòng mở trình duyệt trên máy tính/điện thoại và truy cập:");
        ESP_LOGI(TAG, "http://" IPSTR, IP2STR(&event->ip_info.ip));
        ESP_LOGI(TAG, "===============================================");
    }
}

/* Start Wi-Fi STA connection */
static void wifi_init_sta(void)
{
    size_t ssid_len = strlen(CONFIG_WIFI_SSID);
    size_t password_len = strlen(CONFIG_WIFI_PASSWORD);
    if (ssid_len == 0) {
        ESP_LOGE(TAG, "Wi-Fi SSID is not configured. Set it in menuconfig under Audio device configuration.");
        return;
    }
    if (ssid_len > sizeof(((wifi_config_t *)0)->sta.ssid) ||
        password_len > sizeof(((wifi_config_t *)0)->sta.password)) {
        ESP_LOGE(TAG, "Wi-Fi SSID or password exceeds the supported length.");
        return;
    }

    ESP_LOGI(TAG, "Starting Wi-Fi in station mode...");
    
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();

    esp_sntp_config_t time_config = ESP_NETIF_SNTP_DEFAULT_CONFIG("pool.ntp.org");
    ESP_ERROR_CHECK(esp_netif_sntp_init(&time_config));

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    esp_event_handler_instance_t instance_any_id;
    esp_event_handler_instance_t instance_got_ip;
    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT,
                                                        ESP_EVENT_ANY_ID,
                                                        &wifi_event_handler,
                                                        NULL,
                                                        &instance_any_id));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT,
                                                        IP_EVENT_STA_GOT_IP,
                                                        &wifi_event_handler,
                                                        NULL,
                                                        &instance_got_ip));

    wifi_config_t wifi_config = {0};
    memcpy(wifi_config.sta.ssid, CONFIG_WIFI_SSID, ssid_len);
    memcpy(wifi_config.sta.password, CONFIG_WIFI_PASSWORD, password_len);
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());

    ESP_LOGI(TAG, "Connecting to configured Wi-Fi network...");
}

/* Start HTTP Server */
static void start_http_server(void)
{
    httpd_handle_t server = NULL;
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    /* FATFS calls and HTTP handlers need more than the 4 KB default stack. */
    // Pairing calls Firebase Auth and RTDB over TLS from this task.
    config.stack_size = 16384;
    config.max_uri_handlers = 20;
    config.uri_match_fn = httpd_uri_match_wildcard;

    ESP_LOGI(TAG, "Starting HTTP Server on port: %d", config.server_port);

    if (httpd_start(&server, &config) == ESP_OK) {
        // Register GET /
        httpd_uri_t root_uri = {
            .uri       = "/",
            .method    = HTTP_GET,
            .handler   = root_get_handler,
            .user_ctx  = NULL
        };
        httpd_register_uri_handler(server, &root_uri);

        // Register GET /list
        httpd_uri_t list_uri = {
            .uri       = "/list",
            .method    = HTTP_GET,
            .handler   = list_get_handler,
            .user_ctx  = NULL
        };
        httpd_register_uri_handler(server, &list_uri);

        // Register POST /upload
        httpd_uri_t upload_uri = {
            .uri       = "/upload",
            .method    = HTTP_POST,
            .handler   = upload_post_handler,
            .user_ctx  = NULL
        };
        httpd_register_uri_handler(server, &upload_uri);

        // Register GET /download
        httpd_uri_t download_uri = {
            .uri       = "/download",
            .method    = HTTP_GET,
            .handler   = download_get_handler,
            .user_ctx  = NULL
        };
        httpd_register_uri_handler(server, &download_uri);

        // Register POST /delete
        httpd_uri_t delete_uri = {
            .uri       = "/delete",
            .method    = HTTP_POST,
            .handler   = delete_post_handler,
            .user_ctx  = NULL
        };
        httpd_register_uri_handler(server, &delete_uri);

        // Register POST /play
        httpd_uri_t play_uri = {
            .uri       = "/play",
            .method    = HTTP_POST,
            .handler   = play_post_handler,
            .user_ctx  = NULL
        };
        httpd_register_uri_handler(server, &play_uri);

        // Register POST /stop
        httpd_uri_t stop_uri = {
            .uri       = "/stop",
            .method    = HTTP_POST,
            .handler   = stop_post_handler,
            .user_ctx  = NULL
        };
        httpd_register_uri_handler(server, &stop_uri);

        // Register GET /status
        httpd_uri_t status_uri = {
            .uri       = "/status",
            .method    = HTTP_GET,
            .handler   = status_get_handler,
            .user_ctx  = NULL
        };
        httpd_register_uri_handler(server, &status_uri);

        httpd_uri_t health_uri = {
            .uri       = "/health",
            .method    = HTTP_GET,
            .handler   = health_get_handler,
            .user_ctx  = NULL
        };
        httpd_register_uri_handler(server, &health_uri);

        httpd_uri_t pairing_uri = {.uri = "/api/pairing", .method = HTTP_GET,
            .handler = pairing_get_handler};
        httpd_register_uri_handler(server, &pairing_uri);
        httpd_uri_t pair_uri = {.uri="/api/pair", .method=HTTP_POST, .handler=pair_post_handler};
        httpd_register_uri_handler(server,&pair_uri);

        httpd_uri_t alarms_page = {.uri = "/alarms", .method = HTTP_GET,
            .handler = alarms_page_handler};
        httpd_register_uri_handler(server, &alarms_page);
        alarm_web_register(server);
    } else {
        ESP_LOGE(TAG, "Failed to start HTTP Server");
    }
}

void web_server_start(void)
{
    // Initialize WiFi Station
    wifi_init_sta();

    // Start Web Server
    start_http_server();
}

