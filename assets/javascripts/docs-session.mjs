import { mountSerialStatus } from "./serial-status.mjs?v=serial-navigation-3";
import { serialActivity } from "./serial-activity.mjs?v=serial-navigation-3";

const baseUrl = new URL("../../", import.meta.url);
mountSerialStatus(document, baseUrl);

// Load each interactive page once, without making documentation navigation
// wait for esptool's CDN. The page modules retain their controls and sessions.
const components = [
  { selector: "[data-device-logs]", status: "[data-log-status]", source: "logs",
    module: "./device-logs.mjs?v=device-logs-6" },
  { selector: "[data-hometiles-installer]", status: "#installer-status", source: "installer",
    module: "./installer.mjs?v=installer-ui-15" },
];

function update() {
  const redirect = document.querySelector("[data-docs-redirect]");
  // Legacy documentation URLs use the same navigation lifecycle as normal
  // links, so following a redirect cannot tear down an active USB session.
  if (redirect) setTimeout(() => { if (redirect.isConnected) redirect.click(); }, 0);
  for (const component of components) {
    const root = document.querySelector(component.selector);
    if (!root) continue;
    if (!component.loading) {
      component.loading = import(component.module).catch(() => {
        component.failed = true;
        serialActivity.set(component.source, component.source === "logs" ? "Logger unavailable" : "Installer unavailable", "error");
        update();
      });
    }
    if (component.failed) {
      const status = root.querySelector(component.status);
      status.textContent = "Unable to load the USB tool. Check your connection, then reload this tab when no USB operation is running.";
      status.dataset.kind = "error";
    }
  }
}

if (window.document$) window.document$.subscribe(update);
else update();
