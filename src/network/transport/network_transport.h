#pragma once

#include <Arduino.h>
#include <IPAddress.h>
#include <atomic>

// The Waveshare Touch LCD X devices only have USB host Ethernet, so the RTL8156
// path has to be part of their CI and release builds as well. Local development
// builds keep it on every USB-host-capable profile for hardware tests.
#if !defined(HOMETILES_CI_TARGET) || defined(DEVICE_WAVESHARE_TOUCH_LCD_X) || \
    defined(DEVICE_ELECROW_CROWPANEL_10_1)
#define HOMETILES_USB_ETHERNET_DEV 1
#endif

// Shared network view for every HomeTiles device.
//
// Application code must use this facade for connectivity, addressing and
// transport-sensitive workarounds. Backend-specific operations such as WiFi
// scans, SoftAP configuration or PHY setup remain inside their backend.
enum class NetworkTransportKind : uint8_t {
  None = 0,
  Wifi,
  UsbEthernet,
  NativeEthernet,
};

class NetworkTransportManager {
public:
  void begin();
  void update();

  // Can this build do Ethernet on this device at all? Native Ethernet always
  // counts, and on the 8-inch device USB Ethernet counts in release builds too.
  // This also decides whether the mode switch appears in Settings and Web
  // Admin.
  static bool deviceSupportsEthernet();

  // Fixed network mode of this boot session, taken from the configuration in
  // begin(): true = Ethernet, and WLAN/ESP-Hosted never starts.
  bool isEthernetMode() const { return ethernet_mode_; }

  bool isConnected() const;
  bool isWifiConnected() const;
  bool isWifiDriverActive() const { return wifi_driver_active_.load(); }
  bool isUsbEthernetLinkUp() const;
  bool isNativeEthernetLinkUp() const;
  bool isUsbEthernetConnected() const;
  bool isNativeEthernetConnected() const;
  bool isSdioWifiActive() const;
  void setWifiDriverActive(bool active);

  NetworkTransportKind activeKind() const { return active_kind_.load(); }
  const char* activeName() const;
  uint32_t generation() const { return generation_.load(); }

  IPAddress localIP() const;
  IPAddress gatewayIP() const;
  IPAddress dnsIP(uint8_t index = 0) const;

  // Backend hooks. A backend reports link/IP state here; the facade selects
  // the highest-priority usable transport and exposes it to the application.
  void setUsbEthernetState(bool link_up, bool has_ip, const IPAddress& local_ip,
                           const IPAddress& gateway, const IPAddress& dns);
  void setNativeEthernetState(bool link_up, bool has_ip,
                              const IPAddress& local_ip,
                              const IPAddress& gateway,
                              const IPAddress& dns);

private:
  struct EthernetState {
    std::atomic<bool> link_up{false};
    std::atomic<bool> has_ip{false};
    std::atomic<uint32_t> local_ip{0};
    std::atomic<uint32_t> gateway{0};
    std::atomic<uint32_t> dns{0};
  };

  void refreshActiveTransport();
  const EthernetState* activeEthernetState() const;

  bool begun_ = false;
  bool ethernet_mode_ = false;
  uint32_t wifi_poll_at_ = 0;
  uint32_t usb_poll_at_ = 0;
  uint32_t native_poll_at_ = 0;
  std::atomic<NetworkTransportKind> active_kind_{NetworkTransportKind::None};
  std::atomic<uint32_t> generation_{0};
  std::atomic<bool> wifi_driver_active_{false};
  std::atomic<bool> wifi_connected_{false};
  std::atomic<uint32_t> wifi_local_ip_{0};
  std::atomic<uint32_t> wifi_gateway_{0};
  std::atomic<uint32_t> wifi_dns_{0};
  EthernetState usb_;
  EthernetState native_;
};

extern NetworkTransportManager networkTransport;
