(() => {
  // Markdown scripts run again after instant navigation. Release the previous
  // page's global listeners before binding the replacement table.
  globalThis.hometilesDeviceStatusCleanup?.();
  let active = null;
  let pinned = false;
  let closeTimer;
  const cancelClose = () => clearTimeout(closeTimer);
  const hide = () => {
    cancelClose();
    if (active?.note.matches(':popover-open')) active.note.hidePopover();
  };
  const position = ({ button, note, cell }) => {
    const anchor = button.getBoundingClientRect();
    const column = cell.getBoundingClientRect();
    const margin = 12;
    const gap = 8;
    const available = {
      right: innerWidth - column.right - gap - margin,
      left: column.left - gap - margin,
    };
    // Keep the whole status column clear, including rows with wider labels.
    const side = available.right >= 350 ? 'right' : available.left >= 350 ? 'left'
      : available.right >= 240 ? 'right' : available.left >= 240 ? 'left' : null;
    note.style.width = `${side ? Math.min(350, available[side]) : Math.min(350, innerWidth - 2 * margin)}px`;
    const box = note.getBoundingClientRect();
    let left;
    let top;
    if (side) {
      left = side === 'right' ? column.right + gap : column.left - gap - box.width;
      top = anchor.top + (anchor.height - box.height) / 2;
    } else {
      left = Math.max(margin, Math.min(anchor.right - box.width, innerWidth - box.width - margin));
      top = anchor.top - box.height - gap;
      if (top < margin) top = anchor.bottom + gap;
    }
    note.style.left = `${left}px`;
    note.style.top = `${Math.max(margin, Math.min(top, innerHeight - box.height - margin))}px`;
  };
  const show = (entry, pin = false) => {
    cancelClose();
    if (active && active !== entry) hide();
    active = entry;
    pinned = pin;
    if (!entry.note.matches(':popover-open')) entry.note.showPopover();
    entry.button.setAttribute('aria-expanded', 'true');
    position(entry);
  };
  const scheduleClose = entry => {
    cancelClose();
    closeTimer = setTimeout(() => {
      if (active !== entry || pinned) return;
      if (entry.cell.matches(':hover') || entry.note.matches(':hover')) return;
      if (entry.button === document.activeElement || entry.note.contains(document.activeElement)) return;
      hide();
    }, 180);
  };
  document.querySelectorAll('.ht-device-info').forEach(button => {
    const note = document.getElementById(button.getAttribute('popovertarget'));
    const entry = { button, note, cell: button.closest('td') };
    button.addEventListener('pointerenter', event => {
      if (event.pointerType === 'mouse' && !pinned) show(entry);
    });
    button.addEventListener('pointerleave', () => scheduleClose(entry));
    entry.cell.addEventListener('pointerleave', () => scheduleClose(entry));
    button.addEventListener('click', event => {
      event.preventDefault();
      if (active === entry && pinned && note.matches(':popover-open')) hide();
      else show(entry, true);
    });
    button.addEventListener('keydown', event => {
      if (event.key === 'ArrowDown' || (event.key === 'Tab' && !event.shiftKey && active === entry && note.matches(':popover-open'))) {
        event.preventDefault();
        show(entry, true);
        note.querySelector('a, button').focus();
      }
    });
    note.addEventListener('pointerenter', cancelClose);
    note.addEventListener('pointerleave', () => scheduleClose(entry));
    note.addEventListener('focusout', () => scheduleClose(entry));
    note.addEventListener('keydown', event => {
      if (event.key === 'Escape') {
        event.preventDefault();
        hide();
        button.focus();
      }
    });
    note.querySelector('.ht-device-note-close').addEventListener('click', () => {
      hide();
      button.focus();
    });
    note.addEventListener('toggle', event => {
      if (event.newState === 'closed') {
        button.setAttribute('aria-expanded', 'false');
        if (active === entry) { active = null; pinned = false; }
      }
    });
  });
  // A popup follows its trigger while the page or a table is scrolled.
  const reposition = () => {
    if (!active?.note.matches(':popover-open')) return;
    const anchor = active.button.getBoundingClientRect();
    if (anchor.bottom < 0 || anchor.top > innerHeight) hide();
    else position(active);
  };
  addEventListener('resize', reposition);
  addEventListener('scroll', reposition, true);
  globalThis.hometilesDeviceStatusCleanup = () => {
    hide();
    removeEventListener('resize', reposition);
    removeEventListener('scroll', reposition, true);
  };
})();
