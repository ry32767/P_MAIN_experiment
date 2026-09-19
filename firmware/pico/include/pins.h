#pragma once
// AquaBeacon build_parent_main.py; BRINGUP_BOARDS.md actual J_SPR wiring.
namespace pins {
constexpr int led = 2, ina_alert = 3, sda = 4, scl = 5;
constexpr int pps = 6, spresense_rx = 7; // INPUT ONLY. Old EVENT_MARK label.
constexpr int sd_sck = 10, sd_mosi = 11, sd_miso = 12, sd_cs = 13, sd_cd = 15;
constexpr int tx_pwm = 16, buttons = 26, tx_temp = 27;
}
