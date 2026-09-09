// Keep one interactive component per document while MkDocs replaces its pages.
// Reuse the original controls and listeners instead of recreating USB owners.
export function retainPageComponent(selector, mount, browser = window) {
  let retained;
  let instance;
  const update = () => {
    const next = browser.document.querySelector(selector);
    if (next) {
      if (!retained) {
        retained = next;
        instance = mount(retained);
      } else if (next !== retained) next.replaceWith(retained);
    }
    instance?.pageChanged?.(!!next);
  };
  if (browser.document$) browser.document$.subscribe(update);
  else update();
}
