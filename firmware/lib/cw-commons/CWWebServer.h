#pragma once

#include <WiFi.h>
#include <CWPreferences.h>
#include "StatusController.h"
#include "SettingsWebPage.h"
#include "PushController.h"

#ifndef CLOCKFACE_NAME
  #define CLOCKFACE_NAME "UNKNOWN"
#endif

WiFiServer server(80);

// 极简推流页：图片 / 视频文件 / 摄像头，浏览器解码缩放到 64x64 后 POST /push
static const char PUSH_PAGE[] PROGMEM = R"HTML(<!doctype html><html><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>Clockwise Push</title>
<style>body{margin:0;background:#0b0d10;color:#c9d1d9;font:14px system-ui}canvas{image-rendering:pixelated;border:1px solid #333}a{color:#58a6ff}.row{display:flex;gap:8px;flex-wrap:wrap;align-items:center;margin:8px 0}</style>
</head><body>
<h2>推送到 64x64</h2>
<p><a href="/">设置</a> · <a href="/geo">geo</a></p>
<canvas id="c" width="64" height="64"></canvas>
<div class="row">
  <label>图片 <input type="file" id="img" accept="image/*"></label>
  <label>视频 <input type="file" id="vid" accept="video/*"></label>
  <button id="cam">摄像头</button>
  <button id="stop">停止</button>
  <span id="st"></span>
</div>
<video id="v" muted loop playsinline style="display:none"></video>
<script>
const c=document.getElementById('c'),x=c.getContext('2d',{willReadFrequently:true});
const v=document.getElementById('v');
let sending=false,last=0,raf=0,srcMode='';
function draw(src,w,h){if(!w||!h)return;
 const s=Math.max(64/w,64/h),dw=w*s,dh=h*s;
 x.fillStyle='#000';x.fillRect(0,0,64,64);
 x.drawImage(src,(64-dw)/2,(64-dh)/2,dw,dh);
 push();}
function push(){if(sending)return;sending=true;
 const d=x.getImageData(0,0,64,64).data,b=new Uint8Array(64*64*3);
 for(let i=0,j=0;i<d.length;i+=4,j+=3){b[j]=d[i];b[j+1]=d[i+1];b[j+2]=d[i+2];}
 fetch('/push',{method:'POST',headers:{'Content-Type':'application/octet-stream'},body:b})
  .then(r=>{document.getElementById('st').textContent=r.ok?'已发送':'失败 '+r.status})
  .catch(()=>{document.getElementById('st').textContent='网络错误'})
  .finally(()=>{sending=false;});
}
function tick(t){raf=requestAnimationFrame(tick);
 if(!playing())return;
 if(t-last<66)return; last=t;
 if(v.videoWidth)draw(v,v.videoWidth,v.videoHeight);}
function playing(){return !!(srcMode==='video'&&!v.paused&&!v.ended)}
document.getElementById('img').onchange=e=>{const f=e.target.files[0];if(!f)return;
 stop();srcMode='image';const im=new Image();
 im.onload=()=>draw(im,im.width,im.height);im.src=URL.createObjectURL(f);};
document.getElementById('vid').onchange=e=>{const f=e.target.files[0];if(!f)return;
 stop();srcMode='video';v.srcObject=null;v.src=URL.createObjectURL(f);v.loop=true;
 v.play().then(()=>{document.getElementById('st').textContent='视频播放中'});};
document.getElementById('cam').onclick=async()=>{stop();
 try{const s=await navigator.mediaDevices.getUserMedia({video:{width:{ideal:320},height:{ideal:320}}});
  srcMode='video';v.src='';v.srcObject=s;v.muted=true;
  await v.play();document.getElementById('st').textContent='摄像头';}catch(e){alert(e)}};
document.getElementById('stop').onclick=()=>{stop();document.getElementById('st').textContent='已停止';};
function stop(){cancelAnimationFrame(raf);
 if(v.srcObject){for(const t of v.srcObject.getTracks())t.stop();v.srcObject=null;}
 if(v.src&&v.src.startsWith('blob:')){URL.revokeObjectURL(v.src);v.src='';}
 v.pause();srcMode='';}
raf=requestAnimationFrame(tick);
</script></body></html>)HTML";

struct ClockwiseWebServer
{
  String httpBuffer;
  bool force_restart;
  const char* HEADER_TEMPLATE_D = "X-%s: %d\r\n";
  const char* HEADER_TEMPLATE_S = "X-%s: %s\r\n";
  PushController *push = PushController::getInstance();

  static ClockwiseWebServer *getInstance()
  {
    static ClockwiseWebServer base;
    return &base;
  }

  void startWebServer()
  {
    server.begin();
    StatusController::getInstance()->blink_led(100, 3);
  }

  void stopWebServer()
  {
    server.stop();
  }

  void handleHttpRequest()
  {
    if (force_restart)
      StatusController::getInstance()->forceRestart();


    WiFiClient client = server.available();
    if (client)
    {
      StatusController::getInstance()->blink_led(100, 1);

      while (client.connected())
      {
        if (client.available())
        {
          char c = client.read();
          httpBuffer.concat(c);

          if (c == '\n')
          {
            uint8_t method_pos = httpBuffer.indexOf(' ');
            uint8_t path_pos = httpBuffer.indexOf(' ', method_pos + 1);

            String method = httpBuffer.substring(0, method_pos);
            String path = httpBuffer.substring(method_pos + 1, path_pos);
            String key = "";
            String value = "";

            if (path.indexOf('?') > 0)
            {
              key = path.substring(path.indexOf('?') + 1, path.indexOf('='));
              value = path.substring(path.indexOf('=') + 1);
              path = path.substring(0, path.indexOf('?'));
            }

            processRequest(client, method, path, key, value);
            httpBuffer = "";
            break;
          }
        }
      }
      delay(1);
      client.stop();
    }
  }

  // 读完请求头后收 Content-Length 字节 body（用于 POST /push）
  size_t readBody(WiFiClient &client, uint8_t *dest, size_t maxn)
  {
    // 先扫完剩余请求头，取 Content-Length
    uint32_t contentLength = 0;
    String line;
    uint32_t t0 = millis();
    while (millis() - t0 < 2000)
    {
      if (!client.available()) { delay(1); continue; }
      char c = client.read();
      if (c == '\n')
      {
        line.toLowerCase();
        if (line.startsWith("content-length:"))
        {
          contentLength = line.substring(15).toInt();
        }
        if (line.length() <= 1) break; // 空行：头结束
        line = "";
      }
      else if (c != '\r')
      {
        line += c;
      }
    }
    if (contentLength == 0) return 0;
    size_t want = contentLength < maxn ? contentLength : maxn;
    size_t got = 0;
    t0 = millis();
    while (got < want && millis() - t0 < 4000)
    {
      if (!client.available()) { delay(1); continue; }
      int n = client.read(dest + got, want - got);
      if (n > 0) got += n;
    }
    return got;
  }

  void processRequest(WiFiClient client, String method, String path, String key, String value)
  {
    if (method == "GET" && path == "/") {
      client.println("HTTP/1.0 200 OK");
      client.println("Content-Type: text/html");
      client.println();
      client.println(SETTINGS_PAGE);
    } else if (method == "GET" && path == "/push") {
      client.println("HTTP/1.0 200 OK");
      client.println("Content-Type: text/html; charset=utf-8");
      client.println();
      client.print(PUSH_PAGE);
    } else if (method == "GET" && path == "/geo") {
      client.println("HTTP/1.0 200 OK");
      client.println("Content-Type: text/plain");
      client.println();
      client.print(PushController::W);
      client.print(' ');
      client.println(PushController::H);
    } else if (method == "POST" && path == "/push") {
      // 不再 malloc 临时帧：body 直接写入 PushController 的 12KB 缓冲
      uint8_t *dest = push->lockBuf();
      if (!dest) {
        client.println("HTTP/1.0 500 Internal Server Error");
        return;
      }
      size_t n = readBody(client, dest, PushController::FRAME_LEN);
      if (n >= PushController::FRAME_LEN) {
        push->finishFrame(n);
        client.println("HTTP/1.0 204 No Content");
      } else {
        client.println("HTTP/1.0 400 Bad Request");
      }
    } else if (method == "GET" && path == "/get") {
      getCurrentSettings(client);
    } else if (method == "GET" && path == "/read") {
      if (key == "pin") {
        readPin(client, key, value.toInt());
      }
    } else if (method == "POST" && path == "/restart") {
      client.println("HTTP/1.0 204 No Content");
      force_restart = true;
    } else if (method == "POST" && path == "/set") {
      ClockwiseParams::getInstance()->load();
      //a baby seal has died due this ifs
      if (key == ClockwiseParams::getInstance()->PREF_DISPLAY_BRIGHT) {
        ClockwiseParams::getInstance()->displayBright = value.toInt();
      } else if (key == ClockwiseParams::getInstance()->PREF_WIFI_SSID) {
        ClockwiseParams::getInstance()->wifiSsid = value;
      } else if (key == ClockwiseParams::getInstance()->PREF_WIFI_PASSWORD) {
        ClockwiseParams::getInstance()->wifiPwd = value;
      } else if (key == "autoBright") {   //autoBright=0010,0800
        ClockwiseParams::getInstance()->autoBrightMin = value.substring(0,4).toInt();
        ClockwiseParams::getInstance()->autoBrightMax = value.substring(5,9).toInt();
      } else if (key == ClockwiseParams::getInstance()->PREF_SWAP_BLUE_GREEN) {
        ClockwiseParams::getInstance()->swapBlueGreen = (value == "1");
      } else if (key == ClockwiseParams::getInstance()->PREF_SWAP_BLUE_RED) {
        ClockwiseParams::getInstance()->swapBlueRed = (value == "1");
      } else if (key == ClockwiseParams::getInstance()->PREF_USE_24H_FORMAT) {
        ClockwiseParams::getInstance()->use24hFormat = (value == "1");
      } else if (key == ClockwiseParams::getInstance()->PREF_LDR_PIN) {
        ClockwiseParams::getInstance()->ldrPin = value.toInt();
      } else if (key == ClockwiseParams::getInstance()->PREF_TIME_ZONE) {
        ClockwiseParams::getInstance()->timeZone = value;
      } else if (key == ClockwiseParams::getInstance()->PREF_NTP_SERVER) {
        ClockwiseParams::getInstance()->ntpServer = value;
      } else if (key == ClockwiseParams::getInstance()->PREF_CANVAS_FILE) {
        ClockwiseParams::getInstance()->canvasFile = value;
      } else if (key == ClockwiseParams::getInstance()->PREF_CANVAS_SERVER) {
        ClockwiseParams::getInstance()->canvasServer = value;
      } else if (key == ClockwiseParams::getInstance()->PREF_MANUAL_POSIX) {
        ClockwiseParams::getInstance()->manualPosix = value;
      } else if (key == ClockwiseParams::getInstance()->PREF_DISPLAY_ROTATION) {
        ClockwiseParams::getInstance()->displayRotation = value.toInt();
      } else if (key == ClockwiseParams::getInstance()->PREF_DRIVER) {
        ClockwiseParams::getInstance()->driver = value.toInt();
      }  else if (key == ClockwiseParams::getInstance()->PREF_I2CSPEED) {
        ClockwiseParams::getInstance()->i2cSpeed = value.toInt();
      }  else if (key == ClockwiseParams::getInstance()->PREF_E_PIN) {
        ClockwiseParams::getInstance()->E_pin = value.toInt();
      }
      ClockwiseParams::getInstance()->save();
      client.println("HTTP/1.0 204 No Content");
    }
  }



  void readPin(WiFiClient client, String key, uint16_t pin) {
    ClockwiseParams::getInstance()->load();

    client.println("HTTP/1.0 204 No Content");
    client.printf(HEADER_TEMPLATE_D, key, analogRead(pin));
    
    client.println();
  }


  void getCurrentSettings(WiFiClient client) {
    ClockwiseParams::getInstance()->load();

    client.println("HTTP/1.0 204 No Content");

    client.printf(HEADER_TEMPLATE_D, ClockwiseParams::getInstance()->PREF_DISPLAY_BRIGHT, ClockwiseParams::getInstance()->displayBright);
    client.printf(HEADER_TEMPLATE_D, ClockwiseParams::getInstance()->PREF_DISPLAY_ABC_MIN, ClockwiseParams::getInstance()->autoBrightMin);
    client.printf(HEADER_TEMPLATE_D, ClockwiseParams::getInstance()->PREF_DISPLAY_ABC_MAX, ClockwiseParams::getInstance()->autoBrightMax);
    client.printf(HEADER_TEMPLATE_D, ClockwiseParams::getInstance()->PREF_SWAP_BLUE_GREEN, ClockwiseParams::getInstance()->swapBlueGreen);
    client.printf(HEADER_TEMPLATE_D, ClockwiseParams::getInstance()->PREF_SWAP_BLUE_RED, ClockwiseParams::getInstance()->swapBlueRed);
    client.printf(HEADER_TEMPLATE_D, ClockwiseParams::getInstance()->PREF_USE_24H_FORMAT, ClockwiseParams::getInstance()->use24hFormat);
    client.printf(HEADER_TEMPLATE_D, ClockwiseParams::getInstance()->PREF_LDR_PIN, ClockwiseParams::getInstance()->ldrPin);    
    client.printf(HEADER_TEMPLATE_S, ClockwiseParams::getInstance()->PREF_TIME_ZONE, ClockwiseParams::getInstance()->timeZone.c_str());
    client.printf(HEADER_TEMPLATE_S, ClockwiseParams::getInstance()->PREF_WIFI_SSID, ClockwiseParams::getInstance()->wifiSsid.c_str());
    client.printf(HEADER_TEMPLATE_S, ClockwiseParams::getInstance()->PREF_NTP_SERVER, ClockwiseParams::getInstance()->ntpServer.c_str());
    client.printf(HEADER_TEMPLATE_S, ClockwiseParams::getInstance()->PREF_CANVAS_FILE, ClockwiseParams::getInstance()->canvasFile.c_str());
    client.printf(HEADER_TEMPLATE_S, ClockwiseParams::getInstance()->PREF_CANVAS_SERVER, ClockwiseParams::getInstance()->canvasServer.c_str());
    client.printf(HEADER_TEMPLATE_S, ClockwiseParams::getInstance()->PREF_MANUAL_POSIX, ClockwiseParams::getInstance()->manualPosix.c_str());
    client.printf(HEADER_TEMPLATE_D, ClockwiseParams::getInstance()->PREF_DISPLAY_ROTATION, ClockwiseParams::getInstance()->displayRotation);
    client.printf(HEADER_TEMPLATE_D, ClockwiseParams::getInstance()->PREF_DRIVER, ClockwiseParams::getInstance()->driver);
    client.printf(HEADER_TEMPLATE_D, ClockwiseParams::getInstance()->PREF_I2CSPEED, ClockwiseParams::getInstance()->i2cSpeed);
    client.printf(HEADER_TEMPLATE_D, ClockwiseParams::getInstance()->PREF_E_PIN, ClockwiseParams::getInstance()->E_pin);

    client.printf(HEADER_TEMPLATE_S, "CW_FW_VERSION", CW_FW_VERSION);
    client.printf(HEADER_TEMPLATE_S, "CW_FW_NAME", CW_FW_NAME);
    client.printf(HEADER_TEMPLATE_S, "CLOCKFACE_NAME", CLOCKFACE_NAME);
    client.println();
  }
  
};
