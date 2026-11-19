// Material marks the last table-of-contents entry as soon as the page cannot
// scroll any further. Sections near the end of a page could never reach the
// header, so a click on them highlighted a later entry, and the footer that
// came into view shrank the table of contents to a scroll box. Add just enough
// space below the article for the last entry's heading to reach the header
// while the footer stays below the viewport.

let observer;
let pendingHash = "";

function fit() {
  const article = document.querySelector(".md-content__inner");
  const toc = document.querySelector(".md-sidebar--secondary");
  const main = document.querySelector(".md-main");
  const header = document.querySelector(".md-header");
  if (!article || !toc || !main || !header) return;

  let spacer = article.querySelector(":scope > .ht-toc-end");
  const current = spacer ? spacer.offsetHeight : 0;
  // Instant navigation rewrites the TOC hrefs to absolute URLs.
  const last = [...toc.querySelectorAll("a.md-nav__link")].filter((link) => link.hash).pop();
  const target = last && document.getElementById(decodeURIComponent(last.hash.slice(1)));
  let need = 0;
  if (target && toc.offsetWidth > 0) {
    const top = target.getBoundingClientRect().top + scrollY;
    const mainBottom = main.getBoundingClientRect().bottom + scrollY - current;
    // Anchor jumps land 3px behind the header (readability.css); keep 8px spare.
    const landing = header.offsetHeight - 3;
    need = Math.max(0, Math.ceil(top - landing + innerHeight - mainBottom) + 8);
  }
  if (Math.abs(need - current) >= 2) {
    if (!spacer) {
      spacer = document.createElement("div");
      spacer.className = "ht-toc-end";
      spacer.setAttribute("aria-hidden", "true");
      article.append(spacer);
    }
    spacer.style.height = `${need}px`;
  }

  // A page opened with a hash was scrolled before the space existed.
  if (pendingHash) {
    const anchor = document.getElementById(pendingHash);
    pendingHash = "";
    if (anchor && need > 0) anchor.scrollIntoView();
  }
}

export function mountTocEndSpace() {
  observer?.disconnect();
  const article = document.querySelector(".md-content__inner");
  if (!article) return;
  pendingHash = decodeURIComponent(location.hash.slice(1));
  observer = new ResizeObserver(() => fit());
  observer.observe(article);
  fit();
}

addEventListener("resize", () => fit());
