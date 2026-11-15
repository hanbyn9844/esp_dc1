#include <WiFiClient.h>
#include <ESP8266WiFi.h>
#include <DNSServer.h>
#include <Ticker.h>
#include "Wifi.h"
#include "Debug.h"

WiFiClient Wifi::wifiClient;
WiFiEventHandler Wifi::STAGotIP;
WiFiEventHandler Wifi::STADisconnected;
bool Wifi::isDHCP = true;
unsigned long Wifi::lastWifiCheck = 0;
unsigned long Wifi::lastDisconnectTime = 0;
bool Wifi::wasConnected = false;

unsigned long Wifi::configPortalStart = 0;
#ifdef WIFI_CONNECT_TIMEOUT
unsigned long Wifi::connectStart = 0;
#endif
bool Wifi::connect = false;
String Wifi::_ssid = "";
String Wifi::_pass = "";
DNSServer *Wifi::dnsServer;

void Wifi::connectWifi()
{
    delay(50);
    if (globalConfig.wifi.ssid[0] != '\0')
    {
        setupWifi();
    }
    else
    {
        setupWifiManager(true);
    }
}

void Wifi::setupWifi()
{
    WiFi.persistent(false); // Solve possible wifi init errors (re-add at 6.2.1.16 #4044, #4083)
    WiFi.disconnect(true);  // Delete SDK wifi config
    delay(100);
    WiFi.mode(WIFI_STA);
    WiFi.setSleepMode(WIFI_NONE_SLEEP); // Disable sleep for maximum stability and responsiveness
    WiFi.setAutoConnect(true);
    WiFi.setAutoReconnect(true);
    WiFi.hostname(UID);
    Debug::AddInfo(PSTR("Connecting to %s %s Wifi"), globalConfig.wifi.ssid, globalConfig.wifi.pass);
    STAGotIP = WiFi.onStationModeGotIP([](const WiFiEventStationModeGotIP &event) {
        wasConnected = true;
        lastDisconnectTime = 0;
        Debug::AddInfo(PSTR("WiFi connected. SSID: %s IP: %s"), WiFi.SSID().c_str(), WiFi.localIP().toString().c_str());
        if (globalConfig.wifi.is_static && String(globalConfig.wifi.ip).equals(WiFi.localIP().toString()))
        {
            isDHCP = false;
        }
    });

    STADisconnected = WiFi.onStationModeDisconnected([](const WiFiEventStationModeDisconnected &event) {
        if (wasConnected)
        {
            Debug::AddInfo(PSTR("WiFi disconnected: reason=%d"), event.reason);
            wasConnected = false;
            lastDisconnectTime = millis();
        }
    });
    if (globalConfig.wifi.is_static)
    {
        isDHCP = false;
        IPAddress static_ip;
        IPAddress static_sn;
        IPAddress static_gw;
        static_ip.fromString(globalConfig.wifi.ip);
        static_sn.fromString(globalConfig.wifi.sn);
        static_gw.fromString(globalConfig.wifi.gw);
        Debug::AddInfo(PSTR("Custom STA IP/GW/Subnet: %s %s %s"), globalConfig.wifi.ip, globalConfig.wifi.sn, globalConfig.wifi.gw);
        WiFi.config(static_ip, static_gw, static_sn);
    }

#ifdef WIFI_CONNECT_TIMEOUT
    connectStart = millis();
#endif
    WiFi.begin(globalConfig.wifi.ssid, globalConfig.wifi.pass);
}

void Wifi::setupWifiManager(bool resetSettings)
{
    if (resetSettings)
    {
        Debug::AddInfo(PSTR("WifiManager ResetSettings"));
        Config::resetConfig();

        Debug::AddInfo(PSTR("settings invalidated"));
        Debug::AddInfo(PSTR("THIS MAY CAUSE AP NOT TO START UP PROPERLY. YOU NEED TO COMMENT IT OUT AFTER ERASING THE DATA."));
        WiFi.disconnect(true);
    }
    //WiFi.setAutoConnect(true);
    //WiFi.setAutoReconnect(true);
    WiFi.hostname(UID);
    STAGotIP = WiFi.onStationModeGotIP([](const WiFiEventStationModeGotIP &event) {
        Debug::AddInfo(PSTR("WiFi2 connected. SSID: %s IP address: %s"), WiFi.SSID().c_str(), WiFi.localIP().toString().c_str());
    });

    configPortalStart = millis();
    if (WiFi.isConnected())
    {
        WiFi.mode(WIFI_AP_STA);
        Debug::AddInfo(PSTR("SET AP STA Mode"));
    }
    else
    {
        WiFi.persistent(false);
        WiFi.disconnect();
        WiFi.mode(WIFI_AP_STA);
        WiFi.persistent(true);
        Debug::AddInfo(PSTR("SET AP Mode"));
    }

    connect = false;
    WiFi.softAP(UID);
    delay(500);
    Debug::AddInfo(PSTR("AP IP address: %s"), WiFi.softAPIP().toString().c_str());

    dnsServer = new DNSServer();
    dnsServer->setErrorReplyCode(DNSReplyCode::NoError);
    dnsServer->start(53, "*", WiFi.softAPIP());
}

void Wifi::tryConnect(String ssid, String pass)
{
    _ssid = ssid;
    _pass = pass;
    connect = true;
}

void Wifi::loop()
{
    // Active WiFi connection watchdog when not in Config Portal
    if (configPortalStart == 0)
    {
        if (globalConfig.wifi.ssid[0] != '\0')
        {
            unsigned long now = millis();
            if (WiFi.status() == WL_CONNECTED)
            {
                if (!wasConnected)
                {
                    wasConnected = true;
                    lastDisconnectTime = 0;
                    Debug::AddInfo(PSTR("WiFi connection restored. IP: %s"), WiFi.localIP().toString().c_str());
                }
            }
            else
            {
                if (wasConnected)
                {
                    wasConnected = false;
                    lastDisconnectTime = now;
                    lastWifiCheck = now;
                    Debug::AddInfo(PSTR("WiFi dropped, initiating auto-reconnect watchdog..."));
                }

                // Check every 8 seconds
                if (now - lastWifiCheck >= 8000)
                {
                    lastWifiCheck = now;
                    unsigned long disDuration = lastDisconnectTime > 0 ? (now - lastDisconnectTime) : 0;
                    Debug::AddInfo(PSTR("WiFi reconnecting (status=%d, dis=%lus)..."), WiFi.status(), disDuration / 1000);

                    if (disDuration > 45000)
                    {
                        // Disconnected for more than 45 seconds: hard reset association
                        WiFi.disconnect(false);
                        delay(20);
                        WiFi.begin(globalConfig.wifi.ssid, globalConfig.wifi.pass);
                    }
                    else
                    {
                        WiFi.reconnect();
                    }
                }
            }
        }
        return;
    }
    dnsServer->processNextRequest();

    if (connect)
    {
        connect = false;
        Debug::AddInfo(PSTR("Connecting to new AP"));

        WiFi.begin(_ssid.c_str(), _pass.c_str());
        Debug::AddInfo(PSTR("Waiting for connection result with time out"));
    }

    if (_ssid.length() > 0 && WiFi.isConnected())
    {
        strcpy(globalConfig.wifi.ssid, _ssid.c_str());
        strcpy(globalConfig.wifi.pass, _pass.c_str());
        Config::saveConfig();

        //	为了使WEB获取到IP 2秒后才关闭AP
        Ticker *ticker = new Ticker();
        ticker->attach(3, []() {
            WiFi.mode(WIFI_STA);
            Debug::AddInfo(PSTR("SET STA Mode"));
            ESP.reset();
        });

        Debug::AddInfo(PSTR("WiFi connected. SSID: %s IP address: %s"), WiFi.SSID().c_str(), WiFi.localIP().toString().c_str());

        dnsServer->stop();
        configPortalStart = 0;
        _ssid = "";
        _pass = "";
        return;
    }

    // 检查是否超时
    if (millis() > configPortalStart + (WIFI_PORTAL_TIMEOUT * 1000))
    {
        dnsServer->stop();
        configPortalStart = 0;
        _ssid = "";
        _pass = "";
        Debug::AddInfo(PSTR("startConfigPortal TimeOut"));
        if (WiFi.isConnected())
        {
            //	为了使WEB获取到IP 2秒后才关闭AP
            Ticker *ticker = new Ticker();
            ticker->attach(3, []() {
                WiFi.mode(WIFI_STA);
                Debug::AddInfo(PSTR("SET STA Mode"));
                ESP.reset();
            });
        }
        else
        {
            Debug::AddInfo(PSTR("Wifi failed to connect and hit timeout. Rebooting..."));
            delay(3000);
            ESP.reset(); // 重置，可能进入深度睡眠状态
            delay(5000);
        }
    }
}

bool Wifi::isIp(String str)
{
    int a, b, c, d;
    if ((sscanf(str.c_str(), "%d.%d.%d.%d", &a, &b, &c, &d) == 4) && (a >= 0 && a <= 255) && (b >= 0 && b <= 255) && (c >= 0 && c <= 255) && (d >= 0 && d <= 255))
    {
        return true;
    }
    return false;
}