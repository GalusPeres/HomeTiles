import { serialActivity } from "./serial-activity.mjs?v=serial-navigation-3";

export function mountSerialStatus(document, baseUrl) {
  const title = document.querySelector(".md-header__title");
  if (!title) return;
  const link = document.createElement("a");
  link.className = "ht-serial-status";
  const dot = document.createElement("span");
  dot.className = "ht-serial-status-dot";
  dot.setAttribute("aria-hidden", "true");
  const text = document.createElement("span");
  link.append(dot, text);
  title.after(link);
  serialActivity.subscribe(({ source, label, kind, active }) => {
    link.hidden = !active;
    const page = source === "installer" ? "installer/" : "device-logs/";
    link.href = new URL(page, baseUrl).href;
    link.dataset.kind = kind;
    text.textContent = label;
    link.title = `${label} — Open ${source === "installer" ? "firmware installer" : "device logs"}`;
    link.setAttribute("aria-label", link.title);
  });
}
