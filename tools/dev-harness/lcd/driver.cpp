// SPDX-FileCopyrightText: 2026 Paweł Kwiatkowski
// SPDX-License-Identifier: GPL-3.0-only

#include <cstdio>
#include "display/display_controller.h"
using namespace inventatory_scan;
#define SCREEN(name) std::printf("screen %s\n", name); DisplayController d; d.begin(); d.setBatteryStatus(76, false, false, String()); d.setConnectivity(DisplayConnectivityState::Online, 0);
static SyncResult res(const char* st, const char* item, const char* msg, const char* purpose = "", int delta = 0, int qty = 0, const char* code = "") {
  SyncResult r; r.status = st; r.itemName = item; r.message = msg; r.purposeLabel = purpose; r.appliedDelta = delta; r.quantity = qty; r.code = code; return r;
}
static void tick(DisplayController& d, unsigned long ms = 0) { if (ms) delay(ms); d.tick(); }
int main(int argc, char** argv) {
  { SCREEN("boot splash"); d.playBootSplash(1200); }
  { SCREEN("ready"); d.showReady(); tick(d); }
  { SCREEN("ready update"); d.setUpdateAvailable(true); d.showReady(); tick(d); }
  { SCREEN("ready queued"); d.setConnectivity(DisplayConnectivityState::Syncing, 3); d.showReady(); tick(d); }
  { SCREEN("ready problem"); d.setScannerFault(true); d.showReady(); tick(d); }
  { SCREEN("qty receive 4dig"); d.showQuantity("Checking inventory", "Part RC0603FR-0710KL", "9999", 3, true, QuantityLookupState::Pending); tick(d); }
  { SCREEN("qty change 4dig"); d.showQuantity("Checking inventory", "Code 1234567", "9999", 3, false, QuantityLookupState::Pending); tick(d); }
  { SCREEN("qty found"); d.showQuantity("RES 10K OHM 1%", "RC0603FR-0710KL", "12", 1, false, QuantityLookupState::Found); tick(d); }
  { SCREEN("qty long name"); d.showQuantity("RES 10K OHM 1% 0603 THICK FILM", "RC0603", "100", 2, true, QuantityLookupState::NotFound); tick(d); }
  { SCREEN("sending"); d.showSending(); tick(d); }
  { SCREEN("done"); d.showResult(res("completed", "RES 10K OHM 1% 0603", "", "Stock", 100, 100)); tick(d); }
  { SCREEN("check result"); d.showResult(res("completed_with_warning", "RES 10K OHM", "Below minimum stock", "Review on your PC")); tick(d); }
  { SCREEN("needs attention"); d.showResult(res("failed", "", "Could not update inventory", "", 0, 0, "SYN-01")); tick(d); }
  { SCREEN("menu"); d.showMenu(); tick(d); }
  { SCREEN("menu blocked"); d.showMenu(true, false); tick(d); }
  { String labels[5] = {"Resistors", "Capacitors", "Connectors", "ICs", "Cables"}; SCREEN("quick labels"); d.showQuickLabels(labels, 5, 0, true, true); tick(d); }
  { SCREEN("quick settings"); d.showQuickSettings(); tick(d); }
  { SCREEN("display settings"); d.showDisplaySettings(45, "Auto 5m"); tick(d); }
  { SCREEN("contrast"); d.showContrast(45); tick(d); }
  { SCREEN("backlight"); d.showBacklight("Auto 5 min", "Off after 5 min idle", 4, 6); tick(d); }
  { SCREEN("energy saving"); d.showEnergySaving(true, "5m", "30m", true, 0, "1300 mAh"); tick(d); }
  { SCREEN("confirm reset"); d.showResetConfirm(); tick(d); }
  { SCREEN("confirm discard"); d.showDiscardConfirm(); tick(d); }
  { SCREEN("confirm update"); d.showUpdateConfirm("0.2.0"); tick(d); }
  { SCREEN("confirm repair"); d.showRepairConfirm(true); tick(d); }
  { SCREEN("fault detail"); d.setScannerFault(true); d.showFaultDetail(); tick(d); }
  { SCREEN("setup ble"); d.showSetup(123456); tick(d); }
  { SCREEN("setup locked"); d.showSetup(123456, "PAIRING LOCKED"); tick(d); }
  { SCREEN("setup wifi"); d.showSetup(123456, "CONNECTING WIFI"); tick(d); }
  { SCREEN("setup failed"); d.showSetup(123456, "WIFI FAILED"); tick(d); }
  { SCREEN("ota"); d.showOta("UPDATING", 42); tick(d, 100); }
  { SCREEN("ota restarting"); d.showOta("RESTARTING", 100); tick(d, 100); }
  { SCREEN("boot error"); d.showBoot("Scanner error", "SCN-01"); tick(d); }
  { SCREEN("battery card"); d.showBatteryCard(true, false, 76, "Full: 1h 05m", 2500); tick(d, 800); }
  { SCREEN("low battery"); d.showCountdownWarning("Battery low", "Dock to charge", "Powering off in 42 s", 42, 70); tick(d); }
  { SCREEN("card done"); d.showBatteryCard(true, true, 96, "Fully charged", 2500); tick(d, 800); }
  { SCREEN("battery empty"); d.showCountdownWarning("Battery empty", "Charge to power on", "Powering off", -1, 0); tick(d); }
  { SCREEN("sleep warning"); d.showCountdownWarning("Going to sleep", "Any key stays on", "Sleeping in 12 s", 12, 40); tick(d); }
  { SCREEN("printing"); d.showPrinting("Resistors"); tick(d); }
  { SCREEN("header estimate"); d.setBatteryStatus(60, false, false, "~1.8 d left"); d.showReady(); delay(6000); tick(d); }
  { SCREEN("header estimate hours"); d.setBatteryStatus(30, false, false, "14 h left"); d.showReady(); delay(6000); tick(d); }
  { SCREEN("header estimate charging"); d.setBatteryStatus(45, true, false, "Full: ~2h 15m"); d.showReady(); delay(6000); tick(d); }
  { SCREEN("card on battery"); d.showBatteryCard(false, false, 60, "1.8 d left", 2500); tick(d, 800); }
  { SCREEN("*power warning"); d.showReady(); tick(d); d.showPowerWarning(); tick(d, 100); tick(d, 1400); tick(d, 1400); }
  { SCREEN("*power off"); d.showReady(); tick(d); d.playPowerOffSink(); }
  return 0;
}
