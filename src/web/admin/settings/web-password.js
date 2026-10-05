  // Optional Web Admin password section (Settings tab). The password never
  // leaves the browser: auth.js derives a PBKDF2-HMAC-SHA256 key and the
  // panel stores only salt, iteration count and key.
  function initWebAdminPasswordSettings() {
    const section = document.getElementById('web_auth_section');
    const auth = window.HomeTilesAuth;
    if (!section || !auth) return;
    const input = document.getElementById('web_auth_password');
    const repeat = document.getElementById('web_auth_password_repeat');
    const setButton = document.getElementById('web_auth_set');
    const removeButton = document.getElementById('web_auth_remove');
    const logoutButton = document.getElementById('web_auth_logout');
    const busy = value => {
      [setButton, removeButton, logoutButton].forEach(button => {
        if (button) button.disabled = value;
      });
    };

    setButton?.addEventListener('click', async () => {
      const password = String(input?.value || '');
      if (password.length < 8) {
        showNotification(t('webAuthTooShort'), false);
        input?.focus();
        return;
      }
      if (password !== String(repeat?.value || '')) {
        showNotification(t('webAuthMismatch'), false);
        repeat?.focus();
        return;
      }
      busy(true);
      try {
        if (!await auth.setPassword(password)) throw new Error('set');
        // Setting a password ends every session, including this one. Sign in
        // again right away so the page stays usable.
        const login = await auth.login(password);
        if (input) input.value = '';
        if (repeat) repeat.value = '';
        showNotification(t('webAuthSaved'), true);
        window.setTimeout(() => window.location.reload(), login.ok ? 400 : 1200);
      } catch (error) {
        // The first password needs a tap on the display first.
        showNotification(t(auth.passwordError?.() === 'panel_tap_required' ? 'webAuthPanelTap' : 'webAuthChangeFailed'), false);
      } finally {
        busy(false);
      }
    });

    removeButton?.addEventListener('click', async () => {
      if (!window.confirm(t('webAuthRemoveConfirm'))) return;
      busy(true);
      try {
        if (!await auth.removePassword()) throw new Error('remove');
        showNotification(t('webAuthRemoved'), true);
        window.setTimeout(() => window.location.reload(), 400);
      } catch (error) {
        showNotification(t('webAuthChangeFailed'), false);
      } finally {
        busy(false);
      }
    });

    logoutButton?.addEventListener('click', async () => {
      busy(true);
      await auth.logout();
      window.location.replace('/');
    });
  }
