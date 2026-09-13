#!/usr/bin/env node
// Deterministic browser tests for UI behavior during input and transport failures.
const assert = require('node:assert/strict');
const fs = require('node:fs');
const os = require('node:os');
const path = require('node:path');
const { chromium } = require('playwright');

const staticRoot = path.resolve(__dirname, '../../wwwroot');
const html = fs.readFileSync(path.join(staticRoot, 'index.html'), 'utf8');
const artifacts = process.env.HTTPSERVER_UI_ARTIFACTS || path.join(os.tmpdir(), 'httpserver-ui-acceptance');
const origin = 'http://chat.test';
const securityHeaders = {
  'Content-Security-Policy': "default-src 'self'; connect-src 'self' wss:; frame-ancestors 'none'; base-uri 'none'",
  'X-Content-Type-Options': 'nosniff',
};
const staticContentTypes = {
  '.js': 'application/javascript', '.css': 'text/css', '.svg': 'image/svg+xml', '.png': 'image/png', '.webp': 'image/webp',
  '.woff2': 'font/woff2',
};
const isStaticAsset = pathname => pathname === '/chat.js' || pathname === '/chat.css' || pathname.startsWith('/assets/');
const pause = ms => new Promise(resolve => setTimeout(resolve, ms));
async function until(predicate, message) {
  const deadline = Date.now() + 8000;
  while (Date.now() < deadline) {
    if (await predicate()) return;
    await pause(30);
  }
  throw new Error(message);
}

async function fixture(browser, viewport, options = {}) {
  const context = await browser.newContext({ viewport });
  const page = await context.newPage();
  if (options.disableLocalFonts) {
    const cdp = await context.newCDPSession(page);
    await cdp.send('DOM.enable');
    await cdp.send('CSS.enable');
    await cdp.send('CSS.setLocalFontsEnabled', { enabled: false });
  }
  const errors = [];
  const fontResponses = [];
  page.on('pageerror', error => errors.push(error.message));
  page.on('console', message => {
    if (message.type() === 'error' && /Content Security Policy|Refused to/.test(message.text())) {
      errors.push(message.text());
    }
  });
  page.on('response', response => {
    const pathname = new URL(response.url()).pathname;
    if (isStaticAsset(pathname)) {
      const contentType = (response.headers()['content-type'] || '').split(';')[0].trim().toLowerCase();
      if (response.status() !== 200) errors.push(`Static asset failed: ${pathname} (${response.status()})`);
      if (contentType !== staticContentTypes[path.extname(pathname)]) {
        errors.push(`Static asset MIME mismatch: ${pathname} (${contentType})`);
      }
      if (pathname.endsWith('.woff2')) fontResponses.push(pathname);
    }
  });
  const messages = options.initialMessages || Array.from({ length: 35 }, (_, index) => ({
    messageId: String(index + 1), roomId: '1', clientMessageId: `history-${index}`,
    senderId: 'bob', content: `History message ${index + 1}`,
    createdAt: '2026-09-12T08:00:00Z',
  }));
  const requests = [], sent = [], sockets = [];
  const subscriptions = new Map();
  const failedHistory = [];
  const failHistoryPages = new Set((options.failHistoryPages || []).map(({ roomId, after }) => `${roomId}:${after}`));
  let historyDelay = 0;
  let dropAck = false;
  let dropPersistence = false;
  let failSubscriptions = 0;
  let joined = true;
  let holdNextRooms = false;
  let releaseRooms;
  let nextId = Math.max(0, ...messages.map(message => Number(message.messageId))) + 1;
  const username = options.username || 'alice';
  const roomNames = options.roomNames || ['Engineering', 'Design'];
  await page.addInitScript(({ username }) => {
    localStorage.setItem('linya.token', 'browser-fixture-token');
    localStorage.setItem('linya.username', username);
    localStorage.setItem('linya.room', '1');
  }, { username });
  await page.route(`${origin}/**`, async route => {
    const url = new URL(route.request().url());
    if (url.pathname === '/') {
      await route.fulfill({ contentType: 'text/html', headers: securityHeaders, body: html });
      return;
    }
    if (isStaticAsset(url.pathname)) {
      let assetPath;
      try { assetPath = path.resolve(staticRoot, `.${decodeURIComponent(url.pathname)}`); }
      catch (_) { await route.fulfill({ status: 400, body: 'Invalid asset path' }); return; }
      const relativePath = path.relative(staticRoot, assetPath);
      const contentType = staticContentTypes[path.extname(assetPath)];
      if (relativePath.startsWith(`..${path.sep}`) || relativePath === '..' || path.isAbsolute(relativePath)
        || !contentType || !fs.existsSync(assetPath) || !fs.statSync(assetPath).isFile()) {
        await route.fulfill({ status: 404, body: 'Asset not found' });
      } else {
        await route.fulfill({ contentType, headers: securityHeaders, body: fs.readFileSync(assetPath) });
      }
      return;
    }
    let data = { ok: true };
    if (url.pathname === '/api/v1/rooms') {
      if (route.request().method() === 'POST') {
        data.data = { room: { id: '1' } };
      } else {
        const secondUser = route.request().headers().authorization === 'Bearer second-user-token';
        data.data = { rooms: secondUser ? [{ id: '3', name: 'Second user room' }]
          : [...(joined ? [{ id: '1', name: roomNames[0] }] : []), { id: '2', name: roomNames[1] }] };
        if (holdNextRooms) { holdNextRooms = false; await new Promise(resolve => { releaseRooms = resolve; }); }
      }
    } else if (url.pathname === '/login') {
      data.token = 'second-user-token';
    } else if (url.pathname === '/api/v1/rooms/1/members/me') {
      joined = false;
    } else if (url.pathname === '/api/v1/rooms/1/members') {
      joined = true;
    } else if (url.pathname.endsWith('/messages')) {
      const roomId = url.pathname.split('/')[4];
      const after = Number(url.searchParams.get('after'));
      requests.push({ roomId, after });
      const pageKey = `${roomId}:${after}`;
      if (failHistoryPages.delete(pageKey)) {
        failedHistory.push({ roomId, after });
        await route.fulfill({ status: 503, headers: securityHeaders,
          json: { error: { code: 'dependency_unavailable', message: 'Fixture history page failure' } } });
        return;
      }
      const selected = messages.filter(item => item.roomId === roomId && Number(item.messageId) > after);
      if (historyDelay) await pause(historyDelay);
      data.data = { messages: selected.slice(0, 100), nextAfter: selected.length > 100 ? selected[99].messageId : null };
    }
    await route.fulfill({ json: data, headers: securityHeaders });
  });
  await page.routeWebSocket(`${origin.replace('http', 'ws')}/ws`, socket => {
    sockets.push(socket);
    subscriptions.set(socket, new Set());
    socket.onMessage(raw => {
      const request = JSON.parse(raw);
      if (request.type === 'room.subscribe') {
        if (failSubscriptions > 0) {
          failSubscriptions -= 1;
          socket.send(JSON.stringify({ version: 1, type: 'error', requestId: request.requestId,
            error: { code: 'dependency_unavailable', message: 'temporary fixture outage' } }));
          return;
        }
        subscriptions.get(socket).add(String(request.payload.roomId));
        socket.send(JSON.stringify({ version: 1, type: 'room.subscribed',
          requestId: request.requestId, payload: { roomId: request.payload.roomId } }));
      } else if (request.type === 'message.send') {
        sent.push(request);
        if (dropPersistence) return;
        const message = { ...request.payload, messageId: String(nextId++), senderId: username,
          createdAt: '2026-09-12T08:01:00Z' };
        messages.push(message);
        if (dropAck) return;
        socket.send(JSON.stringify({ version: 1, type: 'message.ack', requestId: request.requestId,
          payload: { clientMessageId: message.clientMessageId, messageId: message.messageId,
            createdAt: message.createdAt } }));
      }
    });
  });
  await page.goto(origin);
  await until(async () => (await page.locator('#status-text').textContent()) === '已连接', 'WebSocket did not open');
  await until(async () => (await page.locator('#message-list').getAttribute('aria-busy')) === 'false', 'History did not finish');
  await page.evaluate(() => document.fonts.ready);
  return { context, page, errors, messages, requests, sent, sockets, fontResponses, failedHistory,
    setHistoryDelay(value) { historyDelay = value; },
    setDropAck(value) { dropAck = value; },
    setDropPersistence(value) { dropPersistence = value; },
    failNextSubscription() { failSubscriptions += 1; },
    subscribedRooms() { return [...(subscriptions.get(sockets.at(-1)) || [])]; },
    holdRooms() { holdNextRooms = true; },
    roomsHeld() { return Boolean(releaseRooms); },
    releaseRooms() { releaseRooms(); },
    offline(content, roomId = '1') {
      const message = { messageId: String(nextId++), roomId, senderId: 'bob',
        clientMessageId: `offline-${nextId}`, content, createdAt: new Date(Date.now() + nextId * 1000).toISOString() };
      messages.push(message);
      return message;
    },
    incoming(content, roomId = '1', senderId = 'bob') {
      const message = { messageId: String(nextId++), roomId, senderId,
        clientMessageId: `incoming-${nextId}`, content, createdAt: '2026-09-12T08:02:00Z' };
      messages.push(message);
      const socket = sockets.at(-1);
      if (subscriptions.get(socket)?.has(String(roomId))) {
        socket.send(JSON.stringify({ version: 1, type: 'message.created', payload: { message } }));
      }
      return message;
    },
  };
}

async function desktopTests(browser) {
  const f = await fixture(browser, { width: 1440, height: 900 });
  const { page } = f;
  try {
    const input = page.locator('#message-input');
    await input.fill('Chinese input candidate');
    await input.dispatchEvent('compositionstart');
    await input.dispatchEvent('keydown', { key: 'Enter', code: 'Enter', isComposing: true });
    assert.equal(f.sent.length, 0, 'IME confirmation must not send a message');
    await input.dispatchEvent('compositionend');
    await input.dispatchEvent('keydown', { key: 'Enter', keyCode: 229 });
    assert.equal(f.sent.length, 0, 'IME legacy key must not send a message');
    await input.press('Enter');
    await until(() => f.sent.length === 1, 'Enter did not send exactly once');
    await until(async () => (await page.locator('.message-state.sent').count()) === 1, 'ACK state not rendered');
    await input.fill('\u4e2d'.repeat(1366));
    assert.equal(await page.locator('#send-button').isDisabled(), true, 'UTF-8 overflow must be rejected before sending');
    await input.fill('\u4e2d'.repeat(1365));
    assert.equal(await page.locator('#send-button').isDisabled(), false, 'Valid UTF-8 boundary must be sendable');
    await input.fill('Engineering draft');
    await input.press('Shift+Enter');
    assert.equal(f.sent.length, 1, 'Shift+Enter must only insert a newline');
    const draft = await input.inputValue();
    await page.locator('.room-item', { hasText: 'Design' }).click();
    assert.equal(await input.inputValue(), '', 'Draft leaked to another room');
    await input.fill('Design draft');
    await page.locator('.room-item', { hasText: 'Engineering' }).click();
    assert.equal(await input.inputValue(), draft, 'Original room draft was lost');
    await until(async () => (await page.locator('#message-list').getAttribute('aria-busy')) === 'false', 'History did not settle');
    const scrollable = await page.locator('#message-list').evaluate(element => element.scrollHeight > element.clientHeight);
    assert.ok(scrollable, 'Long history must scroll inside the message pane');
    const desktopComposer = await page.locator('#composer').boundingBox();
    assert.ok(desktopComposer.y + desktopComposer.height <= 900, 'Long history pushed composer below the viewport');
    assert.equal(await page.evaluate(() => document.documentElement.scrollHeight > innerHeight), false, 'Chat overflowed the page vertically');
    await page.locator('#message-list').evaluate(element => { element.scrollTop = 120; });
    const before = await page.locator('#message-list').evaluate(element => element.scrollTop);
    f.incoming('Live message while reading history');
    await until(async () => await page.locator('#message-list').getByText('Live message while reading history', { exact: true }).count(), 'Live message missing');
    const after = await page.locator('#message-list').evaluate(element => element.scrollTop);
    assert.ok(Math.abs(before - after) < 2, 'Incoming message interrupted history reading');
    await f.sockets.at(-1).close({ code: 1012, reason: 'test restart' });
    const offline = f.offline('Recovered during reconnect');
    await until(() => f.sockets.length === 2, 'Socket did not reconnect');
    await until(async () => await page.locator('#message-list').getByText(offline.content, { exact: true }).count(), 'Offline message not recovered');
    assert.equal(await page.locator('#message-list').getByText(offline.content, { exact: true }).count(), 1, 'Recovered message duplicated');
    assert.ok(f.requests.some(request => request.roomId === '1' && request.after > 0), 'Reconnect did not use an incremental cursor');
    f.setDropAck(true);
    await input.fill('Persisted with ACK lost');
    await input.press('Enter');
    await until(() => f.sent.length === 2, 'Lost-ACK message not sent');
    await f.sockets.at(-1).close({ code: 1012, reason: 'lost ACK' });
    await until(() => f.sockets.length === 3, 'Lost-ACK connection did not recover');
    await until(async () => await page.locator('.message-row.sent', { hasText: 'Persisted with ACK lost' }).count(), 'History did not settle a lost ACK');
    assert.equal(await page.locator('#message-list').getByText('Persisted with ACK lost', { exact: true }).count(), 1, 'Lost-ACK retry duplicated the message');
    f.setDropAck(false);
    f.setDropPersistence(true);
    await input.fill('Pending survives history eviction');
    await input.press('Enter');
    await until(() => f.sent.length === 3, 'Uncommitted message was not sent');
    for (let i = 0; i < 1001; i++) f.offline(`Backlog ${i}`);
    f.setDropPersistence(false);
    f.failNextSubscription();
    await f.sockets.at(-1).close({ code: 1012, reason: 'dependency recovery' });
    await until(() => f.sockets.length >= 5, 'Failed subscription did not reconnect');
    await until(async () => await page.locator('.message-row.sent', { hasText: 'Pending survives history eviction' }).count(), 'Pending message lost during history recovery');
    assert.equal(f.sent.filter(item => item.payload.content === 'Pending survives history eviction').length, 2, 'Pending message was not retried exactly once');
    await input.fill(draft);
    f.setHistoryDelay(300);
    await page.locator('.room-item', { hasText: 'Design' }).click();
    await page.locator('.room-item', { hasText: 'Engineering' }).click();
    await until(async () => (await page.locator('#message-list').getAttribute('aria-busy')) === 'false', 'Room switch remained loading');
    assert.equal(await page.locator('#current-room-name').textContent(), 'Engineering');
    assert.equal(await input.inputValue(), draft);
    await page.screenshot({ path: path.join(artifacts, 'chat-desktop.png'), fullPage: true });
    await page.locator('#logout-button').click();
    await until(async () => await page.locator('#auth-view').isVisible(), 'Logout did not return to login');
    assert.equal(await input.inputValue(), '', 'Draft survived logout');
    assert.deepEqual(f.errors, []);
    console.log('desktop: ime, UTF-8, multiline, drafts, scroll, reconnect, lost ACK, subscription retry, 1001-message recovery, rapid switch, logout passed');
  } finally { await f.context.close(); }
}

async function sessionTests(browser) {
  const f = await fixture(browser, { width: 1440, height: 900 });
  const { page } = f;
  try {
    await page.locator('#leave-room-button').click();
    await until(async () => (await page.locator('#current-room-name').textContent()) === 'Design', 'Leave did not change rooms');
    await page.locator('#join-room-button').click();
    await page.locator('#join-room-id-input').fill('1');
    await page.locator('#join-room-submit').click();
    await until(async () => await page.locator('#message-list').getByText('History message 1', { exact: true }).count(), 'Rejoining a room lost old history');
    await until(async () => !(await page.locator('#join-room-dialog').isVisible()), 'Join dialog did not close');
    f.holdRooms();
    await page.locator('#create-room-button').click();
    await page.locator('#room-name-input').fill('Refresh trigger');
    await page.locator('#room-submit').click();
    await until(() => f.roomsHeld(), 'Delayed room request did not start');
    await page.locator('#close-room-dialog').click();
    await page.locator('#logout-button').click();
    await until(async () => await page.locator('#auth-view').isVisible(), 'Logout did not finish');
    await page.locator('#username').fill('second-user');
    await page.locator('#password').fill('Browser-password-2026');
    await page.locator('#auth-submit').click();
    await until(async () => (await page.locator('#current-room-name').textContent()) === 'Second user room', 'Second session did not load');
    f.releaseRooms();
    await until(async () => !(await page.locator('#room-submit').isDisabled()), 'Old request did not finish');
    assert.equal(await page.locator('#current-room-name').textContent(), 'Second user room', 'Old request polluted the new session');
    assert.equal(await page.locator('.room-item', { hasText: 'Engineering' }).count(), 0);
    assert.deepEqual(f.errors, []);
    console.log('session: leave/rejoin cursor reset and delayed old-account response isolation passed');
  } finally { await f.context.close(); }
}

async function mobileTests(browser) {
  const f = await fixture(browser, { width: 390, height: 844 });
  try {
    await f.page.locator('#mobile-menu').click();
    assert.equal(await f.page.locator('#mobile-menu').getAttribute('aria-expanded'), 'true');
    await f.page.locator('#close-sidebar-button').click();
    await until(async () => { const box = await f.page.locator('#sidebar').boundingBox(); return box.x + box.width <= 0; }, 'Close button did not hide drawer');
    assert.equal(await f.page.locator('#mobile-menu').getAttribute('aria-expanded'), 'false');
    assert.equal(await f.page.locator('#mobile-menu').evaluate(element => element === document.activeElement), true,
      'Drawer close did not restore focus to the menu button');
    await f.page.locator('#mobile-menu').click();
    await f.page.locator('#create-room-button').click();
    await f.page.keyboard.press('Escape');
    await until(async () => !(await f.page.locator('#room-dialog').isVisible()), 'Escape did not dismiss the room dialog');
    assert.equal(await f.page.locator('#mobile-menu').getAttribute('aria-expanded'), 'true',
      'Dismissing a dialog unexpectedly closed the underlying drawer');
    await f.page.keyboard.press('Escape');
    await until(async () => { const box = await f.page.locator('#sidebar').boundingBox(); return box.x + box.width <= 0; }, 'Escape did not hide drawer');
    assert.equal(await f.page.locator('#mobile-menu').getAttribute('aria-expanded'), 'false');
    assert.equal(await f.page.locator('#mobile-menu').evaluate(element => element === document.activeElement), true,
      'Escape did not restore focus to the menu button');
    assert.equal(await f.page.locator('#mobile-scrim').isVisible(), false, 'Drawer scrim remained visible after Escape');
    await f.page.locator('#mobile-menu').click();
    await f.page.locator('.room-item', { hasText: 'Design' }).click();
    await until(async () => { const box = await f.page.locator('#sidebar').boundingBox(); return box.x + box.width <= 0; }, 'Mobile drawer stayed open');
    assert.equal(await f.page.locator('#mobile-menu').getAttribute('aria-expanded'), 'false');
    await f.page.locator('#message-input').fill('Mobile draft');
    const overflow = await f.page.evaluate(() => document.documentElement.scrollWidth > innerWidth);
    assert.equal(overflow, false, 'Mobile viewport has horizontal overflow');
    const composer = await f.page.locator('#composer').boundingBox();
    assert.ok(composer.y + composer.height <= 844, 'Composer is below the viewport');
    assert.ok(composer.width >= 380, 'Mobile chat is restricted to a desktop grid column');
    await f.page.screenshot({ path: path.join(artifacts, 'chat-mobile.png'), fullPage: true });
    assert.deepEqual(f.errors, []);
    console.log('mobile: room navigation, drawer close, Escape, dialog isolation, focus, composer framing, no horizontal overflow passed');
  } finally { await f.context.close(); }
}

async function controlTests(browser) {
  const f = await fixture(browser, { width: 1440, height: 900 });
  const { page } = f;
  try {
    await page.evaluate(() => {
      window.clipboardFixture = { writes: [], fail: false };
      Object.defineProperty(navigator, 'clipboard', { configurable: true, value: {
        async writeText(value) {
          if (window.clipboardFixture.fail) throw new DOMException('Clipboard permission denied', 'NotAllowedError');
          window.clipboardFixture.writes.push(value);
        },
      } });
    });
    await page.locator('#copy-room-button').click();
    await until(async () => await page.locator('.toast.success').count(), 'Clipboard success was not acknowledged');
    assert.deepEqual(await page.evaluate(() => window.clipboardFixture.writes), ['1'], 'Copied value is not the room ID');
    const successes = await page.locator('.toast.success').count();
    await page.evaluate(() => { window.clipboardFixture.fail = true; });
    await page.locator('#copy-room-button').click();
    await until(async () => await page.locator('.toast.error[role="alert"]').count(), 'Clipboard denial was not shown as an error');
    assert.equal(await page.locator('.toast.success').count(), successes, 'Clipboard denial showed a false success');
    assert.deepEqual(await page.evaluate(() => window.clipboardFixture.writes), ['1']);
    const latest = page.locator('#latest-message-button');
    assert.equal(await latest.isVisible(), false, 'Latest-message button should be hidden at the bottom');
    await page.locator('#message-list').evaluate(element => { element.scrollTop = element.scrollHeight - element.clientHeight - 60; });
    await pause(80);
    assert.equal(await latest.isVisible(), false, 'Latest-message button appears too close to the bottom');
    await page.locator('#message-list').evaluate(element => { element.scrollTop = element.scrollHeight - element.clientHeight - 80; });
    await until(async () => await latest.isVisible(), 'Latest-message button did not appear when reading history');
    await latest.click();
    await until(async () => !(await latest.isVisible()), 'Latest-message button did not hide after returning to the bottom');
    assert.ok(await page.locator('#message-list').evaluate(element =>
      element.scrollHeight - element.clientHeight - element.scrollTop < 2), 'Latest-message button did not reach the bottom');
    await page.locator('#logout-button').click();
    await until(async () => await page.locator('#auth-view').isVisible(), 'Logout did not reveal password control');
    const password = page.locator('#password'), toggle = page.locator('#password-toggle');
    await password.fill('Visible-password-fixture');
    assert.equal(await password.getAttribute('type'), 'password');
    await toggle.click();
    assert.equal(await password.getAttribute('type'), 'text');
    assert.equal(await toggle.getAttribute('aria-pressed'), 'true');
    assert.equal(await toggle.getAttribute('aria-label'), '隐藏密码');
    await page.locator('#register-tab').click();
    assert.equal(await password.getAttribute('type'), 'password', 'Registration tab did not conceal the password');
    assert.equal(await toggle.getAttribute('aria-pressed'), 'false');
    await toggle.click();
    await page.locator('#login-tab').click();
    assert.equal(await password.getAttribute('type'), 'password', 'Login tab did not conceal the password');
    assert.equal(await toggle.getAttribute('aria-label'), '显示密码');
    assert.equal(await page.locator('#auth-view').isVisible(), true, 'Password toggle submitted the form');
    assert.deepEqual(f.errors, []);
    console.log('controls: clipboard success/denial, latest-message navigation, password visibility and tab reset passed');
  } finally { await f.context.close(); }
}

async function assertFramed(page, selectors, label) {
  const viewport = page.viewportSize();
  assert.equal(await page.evaluate(() => document.documentElement.scrollWidth > innerWidth), false,
    `${label}: page has horizontal overflow`);
  for (const selector of selectors) {
    const element = page.locator(selector);
    assert.ok(await element.isVisible(), `${label}: ${selector} is not visible`);
    const box = await element.boundingBox();
    assert.ok(box && box.width > 0 && box.height > 0, `${label}: ${selector} has no usable area`);
    assert.ok(box.x >= -1 && box.y >= -1 && box.x + box.width <= viewport.width + 1
      && box.y + box.height <= viewport.height + 1,
    `${label}: ${selector} exceeds ${viewport.width}x${viewport.height}: ${JSON.stringify(box)}`);
  }
}

async function roomPresentationTests(browser) {
  const history = [
    ['2026-09-10T12:00:00Z', 'First day, first message'],
    ['2026-09-10T13:00:00Z', 'First day, second message'],
    ['2026-09-11T12:00:00Z', 'Second day message'],
  ];
  const f = await fixture(browser, { width: 1440, height: 900 }, {
    initialMessages: [...history.map(([createdAt, content], index) => ({
      messageId: String(index + 1), roomId: '1', clientMessageId: `grouped-${index}`, senderId: 'bob', content, createdAt,
    })), { messageId: '4', roomId: '2', clientMessageId: 'existing-background-history', senderId: 'bob',
      content: 'Existing background history', createdAt: '2026-09-11T13:00:00Z' }],
  });
  const { page } = f;
  const firstRoom = page.locator('.room-item[data-room-id="1"]');
  const secondRoom = page.locator('.room-item[data-room-id="2"]');
  const input = page.locator('#message-input');
  const all = page.locator('#all-rooms-filter'), unread = page.locator('#unread-rooms-filter');
  try {
    await until(() => f.subscribedRooms().includes('2'), 'Background room was not subscribed without visiting it');
    await until(async () => (await secondRoom.locator('.room-meta').textContent()).includes('Existing background history'),
      'Unvisited room history did not load');
    assert.equal(await page.locator('.room-unread').count(), 0, 'Initial background history was incorrectly marked unread');
    assert.deepEqual(await page.locator('#message-list .date-divider, #message-list .message-row').evaluateAll(elements =>
      elements.map(element => element.classList.contains('date-divider') ? 'date' : element.querySelector('.message-bubble').textContent)),
    ['date', history[0][1], history[1][1], 'date', history[2][1]], 'Messages are not grouped by their local date');
    assert.deepEqual(await page.locator('.date-divider time').evaluateAll(elements => elements.map(element => element.dateTime)),
      ['2026-09-10T12:00:00.000Z', '2026-09-11T12:00:00.000Z']);
    const dateLabels = await page.locator('.date-divider').allTextContents();
    assert.ok(dateLabels.every(label => label.trim()) && dateLabels[0] !== dateLabels[1], 'Date dividers have empty or repeated labels');
    assert.equal(await firstRoom.locator('.room-meta').textContent(), 'bob: Second day message');
    assert.equal(await firstRoom.locator('.room-time').getAttribute('datetime'), '2026-09-11T12:00:00.000Z');
    await input.fill('Draft\n across\trooms');
    assert.equal(await firstRoom.locator('.draft-label').textContent(), '草稿');
    assert.equal(await firstRoom.locator('.room-meta').textContent(), '草稿 Draft across rooms', 'Draft preview did not normalize whitespace');
    f.incoming('New\n live\tmessage');
    await until(async () => await page.locator('#message-list').getByText('New live message', { exact: true }).count(), 'Incoming message missing');
    assert.equal(await firstRoom.locator('.room-meta').textContent(), '草稿 Draft across rooms', 'Incoming message replaced the unsent draft preview');
    await input.fill('');
    assert.equal(await firstRoom.locator('.room-meta').textContent(), 'bob: New live message', 'Clearing draft did not restore normalized message preview');
    assert.equal(await firstRoom.locator('.draft-label').count(), 0);
    await input.fill('Final preview');
    await input.press('Enter');
    await until(async () => await page.locator('.message-row.sent', { hasText: 'Final preview' }).count(), 'Preview message was not acknowledged');
    assert.equal(await page.locator('#message-list .message-bubble').last().textContent(), 'Final preview',
      'ACK-assigned message ID did not restore server ordering after client/server clock skew');
    assert.equal(await firstRoom.locator('.room-meta').textContent(), '你: Final preview');
    assert.equal(await firstRoom.locator('.draft-label').count(), 0, 'Sent draft remained in the room preview');
    assert.equal(await firstRoom.locator('.room-time').getAttribute('datetime'), '2026-09-12T08:01:00.000Z',
      'Room preview time did not adopt the server ACK timestamp');
    f.sockets.at(-1).send(JSON.stringify({ version: 1, type: 'error', error: { code: 'invalid_content' } }));
    await until(async () => await page.locator('#message-list .system-message', { hasText: '消息内容无效' }).count(),
      'System error notice was not shown');
    assert.equal(await firstRoom.locator('.room-meta').textContent(), '你: Final preview', 'System notice replaced the room message summary');
    await input.fill('After system notice');
    await input.press('Enter');
    await until(async () => await page.locator('.message-row.sent', { hasText: 'After system notice' }).count(), 'Message after system notice was not acknowledged');
    const timeline = await page.locator('#message-list .system-message, #message-list .message-bubble').allTextContents();
    assert.ok(timeline.indexOf('Final preview') < timeline.indexOf('消息内容无效')
      && timeline.indexOf('消息内容无效') < timeline.indexOf('After system notice'),
    'System notice did not retain its position between confirmed messages');
    assert.equal(await firstRoom.locator('.room-meta').textContent(), '你: After system notice');
    assert.equal(await all.getAttribute('role'), 'tab');
    assert.equal(await all.getAttribute('aria-selected'), 'true');
    assert.equal(await unread.getAttribute('aria-selected'), 'false');
    f.incoming('Unread\n next\troom', '2');
    await until(async () => await secondRoom.locator('.room-unread').count(), 'Background room did not gain an unread badge');
    assert.equal(await secondRoom.locator('.room-unread').textContent(), '1');
    assert.equal(await secondRoom.locator('.room-meta').textContent(), 'bob: Unread next room');
    assert.equal(await page.locator('#unread-room-count').textContent(), '1');
    await unread.click();
    assert.equal(await unread.getAttribute('aria-selected'), 'true');
    assert.equal(await all.getAttribute('aria-selected'), 'false');
    assert.equal(await page.locator('.room-item').count(), 1, 'Unread filter retained read rooms');
    assert.equal(await page.locator('.room-item').getAttribute('data-room-id'), '2');
    await page.locator('#room-search').fill('Engineering');
    assert.equal(await page.locator('.room-item').count(), 0, 'Search was not combined with the unread filter');
    await page.locator('#room-search').fill('');
    assert.equal(await page.locator('.room-item').count(), 1);
    await unread.press('Home');
    assert.equal(await all.getAttribute('aria-selected'), 'true');
    assert.equal(await all.evaluate(element => element === document.activeElement), true, 'Filter keyboard navigation lost focus');
    assert.equal(await all.getAttribute('tabindex'), '0');
    assert.equal(await unread.getAttribute('tabindex'), '-1');
    assert.equal(await page.locator('.room-item').count(), 2);
    await all.press('ArrowRight');
    assert.equal(await unread.getAttribute('aria-selected'), 'true');
    await unread.press('ArrowLeft');
    assert.equal(await all.getAttribute('aria-selected'), 'true');
    await all.press('End');
    assert.equal(await unread.getAttribute('aria-selected'), 'true');
    await page.screenshot({ path: path.join(artifacts, 'desktop-unread-rooms.png'), fullPage: true, animations: 'disabled' });
    await secondRoom.click();
    await until(async () => await page.locator('#message-list').getByText('Unread next room', { exact: true }).count(), 'Opening an unread room did not show its message');
    assert.equal(await page.locator('#current-room-name').textContent(), 'Design');
    assert.equal(await page.locator('.room-item').count(), 0, 'Read room remained in the unread list');
    assert.equal(await page.locator('#room-list .empty-state').textContent(), '没有未读消息');
    assert.equal(await page.locator('#unread-room-count').isVisible(), false);
    await all.click();
    assert.equal(await secondRoom.getAttribute('aria-current'), 'true');
    assert.equal(await firstRoom.getAttribute('aria-current'), 'false');
    await f.sockets.at(-1).close({ code: 1012, reason: 'background recovery' });
    const recovered = f.offline('Recovered background unread');
    await until(() => f.sockets.length === 2, 'Background recovery socket did not reconnect');
    await until(async () => await firstRoom.locator('.room-unread').count(), 'Recovered background message was not marked unread');
    assert.equal(await firstRoom.locator('.room-unread').textContent(), '1', 'Recovery double-counted cached or newly fetched messages');
    assert.ok((await firstRoom.locator('.room-meta').textContent()).includes(recovered.content));
    assert.equal(await page.locator('#current-room-name').textContent(), 'Design', 'Background recovery switched the active room');
    await f.sockets.at(-1).close({ code: 1012, reason: 'repeat background recovery' });
    await until(() => f.sockets.length === 3, 'Second background recovery socket did not reconnect');
    await until(() => f.requests.some(request => request.roomId === '1' && request.after === Number(recovered.messageId)),
      'Second background recovery did not advance the history cursor');
    assert.equal(await firstRoom.locator('.room-unread').textContent(), '1', 'Repeated history recovery duplicated unread count');
    await unread.click();
    await page.locator('#room-search').fill('Design');
    await page.locator('#logout-button').click();
    await until(async () => await page.locator('#auth-view').isVisible(), 'Logout did not finish');
    await page.locator('#username').fill('second-user');
    await page.locator('#password').fill('Browser-password-2026');
    await page.locator('#auth-submit').click();
    await until(async () => (await page.locator('#current-room-name').textContent()) === 'Second user room', 'Second session did not load');
    assert.equal(await all.getAttribute('aria-selected'), 'true', 'Room filter leaked across accounts');
    assert.equal(await page.locator('#room-search').inputValue(), '', 'Room search leaked across accounts');
    assert.equal(await page.locator('.room-item').count(), 1);
    assert.deepEqual(f.errors, []);
    console.log('rooms: date groups, previews, ACK/system ordering, unvisited subscriptions, recovered unread counts, filters, keyboard navigation, read clearing and account isolation passed');
  } finally { await f.context.close(); }
}

async function fontTests(browser) {
  const f = await fixture(browser, { width: 1440, height: 900 }, { disableLocalFonts: true });
  try {
    assert.ok(f.fontResponses.length > 0, 'No WOFF2 font was fetched when local font sources were disabled');
    const loadedFamilies = await f.page.evaluate(() => [...document.fonts]
      .filter(font => font.status === 'loaded').map(font => font.family.replaceAll('"', '').replaceAll("'", '')));
    assert.ok(loadedFamilies.includes('Noto Sans SC'), 'The bundled CJK font did not finish loading');
    assert.deepEqual(f.errors, [], 'Bundled font caused a CSP, MIME or browser error');
    console.log(`fonts: local sources disabled, ${f.fontResponses.length} WOFF2 subsets fetched and Noto Sans SC loaded`);
  } finally { await f.context.close(); }
}

async function backgroundReliabilityTests(browser) {
  const f = await fixture(browser, { width: 1440, height: 900 });
  const { page } = f;
  const firstRoom = page.locator('.room-item[data-room-id="1"]');
  const secondRoom = page.locator('.room-item[data-room-id="2"]');
  try {
    await until(() => f.subscribedRooms().includes('2'), 'Empty background room was not subscribed');
    await until(() => f.requests.some(request => request.roomId === '2' && request.after === 0),
      'Empty background history did not request its baseline');
    assert.equal(await secondRoom.locator('.room-unread').count(), 0);
    await f.sockets.at(-1).close({ code: 1012, reason: 'first background delivery during outage' });
    const firstUnread = f.offline('First recovered background message', '2');
    await until(() => f.sockets.length === 2, 'Empty-room recovery socket did not reconnect');
    await until(async () => await secondRoom.locator('.room-unread').count(), 'First message in an empty background room was lost from unread recovery');
    assert.equal(await secondRoom.locator('.room-unread').textContent(), '1');
    await f.sockets.at(-1).close({ code: 1012, reason: 'background history eviction' });
    for (let index = 0; index < 1001; index++) f.offline(`Unread backlog ${index}`, '2');
    await until(() => f.sockets.length === 3, 'Background backlog socket did not reconnect');
    await until(async () => (await secondRoom.locator('.room-meta').textContent()).includes('Unread backlog 1000'),
      'Background backlog did not finish recovering');
    assert.equal(await secondRoom.locator('.room-unread').textContent(), '99+', 'Unread badge did not saturate for a large backlog');
    await secondRoom.click();
    await until(async () => (await page.locator('#message-list').getAttribute('aria-busy')) === 'false', 'Opening recovered backlog did not settle');
    assert.equal(await page.locator('#message-list .message-row').count(), 1000, 'Message cache bound was not enforced');
    assert.equal(await page.locator('#message-list').getByText(firstUnread.content, { exact: true }).count(), 0,
      'The oldest message did not leave the bounded display cache');
    assert.equal(await page.locator('.room-unread').count(), 0, 'Reading the backlog did not clear unread state');
    await firstRoom.click();
    await until(async () => (await page.locator('#message-list').getAttribute('aria-busy')) === 'false', 'Returning to the first room did not settle');
    f.sockets.at(-1).send(JSON.stringify({ version: 1, type: 'message.created', payload: { message: firstUnread } }));
    f.incoming('Fresh after replay', '2');
    await until(async () => (await secondRoom.locator('.room-meta').textContent()).includes('Fresh after replay'),
      'Fresh background message did not follow the replay');
    assert.equal(await secondRoom.locator('.room-unread').textContent(), '1',
      'A replay of an already read, evicted message was counted as unread again');
    assert.deepEqual(f.errors, []);
    console.log('background: first offline message after empty history, 1001-message recovery, bounded cache, saturated badge and read-message replay deduplication passed');
  } finally { await f.context.close(); }
}

async function initialHistoryRetryTests(browser) {
  const initialMessages = [{ messageId: '1', roomId: '1', clientMessageId: 'active-history', senderId: 'bob',
    content: 'Active room history', createdAt: '2026-09-10T12:00:00Z' },
  ...Array.from({ length: 101 }, (_, index) => ({ messageId: String(index + 2), roomId: '2',
    clientMessageId: `initial-history-${index}`, senderId: 'bob', content: `Old background message ${index}`,
    createdAt: '2026-09-11T12:00:00Z' }))];
  const f = await fixture(browser, { width: 1440, height: 900 }, {
    initialMessages, failHistoryPages: [{ roomId: '2', after: 101 }],
  });
  const { page } = f;
  const backgroundRoom = page.locator('.room-item[data-room-id="2"]');
  try {
    await until(() => f.failedHistory.length === 1, 'Initial history did not reach its injected second-page failure');
    await until(async () => await page.locator('.toast.error', { hasText: 'Fixture history page failure' }).count(),
      'Initial history failure did not finish processing');
    assert.ok((await backgroundRoom.locator('.room-meta').textContent()).includes('Old background message 99'));
    assert.equal(await backgroundRoom.locator('.room-unread').count(), 0, 'Partial initial history was marked unread');
    await f.sockets.at(-1).close({ code: 1012, reason: 'retry incomplete initial history' });
    await until(() => f.sockets.length === 2, 'Initial history retry did not reconnect');
    await until(async () => (await backgroundRoom.locator('.room-meta').textContent()).includes('Old background message 100'),
      'Second initial-history page did not retry successfully');
    assert.equal(f.requests.filter(request => request.roomId === '2' && request.after === 101).length, 2,
      'Initial history retry did not reuse its completed-page checkpoint');
    assert.equal(await backgroundRoom.locator('.room-unread').count(), 0,
      'Retrying an incomplete baseline incorrectly marked the 101st old message unread');
    f.incoming('New after completed baseline', '2');
    await until(async () => await backgroundRoom.locator('.room-unread').count(), 'New message after successful baseline was not unread');
    assert.equal(await backgroundRoom.locator('.room-unread').textContent(), '1');
    assert.deepEqual(f.errors, []);
    console.log('history: initial 101-message baseline, second-page failure, checkpoint retry, old-message exclusion and later unread delivery passed');
  } finally { await f.context.close(); }
}

async function visualTests(browser) {
  const viewports = [
    ['desktop', { width: 1440, height: 900 }],
    ['laptop', { width: 1280, height: 720 }],
    ['mobile', { width: 390, height: 844 }],
    ['narrow', { width: 320, height: 740 }],
  ];
  for (const [name, viewport] of viewports) {
    const f = await fixture(browser, viewport, {
      username: 'member_'.padEnd(64, 'x'),
      roomNames: ['产品设计与工程协作讨论'.repeat(10), '周末读书与灵感分享'],
    });
    const { page } = f;
    const mobile = viewport.width <= 760;
    const screenshot = suffix => page.screenshot({
      path: path.join(artifacts, `${name}-${suffix}.png`), fullPage: true, animations: 'disabled',
    });
    try {
      assert.deepEqual(await page.locator('link[rel="stylesheet"]').evaluateAll(links =>
        links.filter(link => !link.sheet || link.sheet.cssRules.length === 0).map(link => link.href)), [],
      `${name}: stylesheet did not load`);
      const longMessage = `这是用于验收窄屏排版的长中文消息。${'跨节点实时沟通与消息恢复'.repeat(32)}\n${'UnbrokenLongMessage'.repeat(30)}`;
      f.incoming(longMessage, '1', 'participant_'.padEnd(64, 'y'));
      await until(async () => await page.locator('#message-list').getByText(longMessage, { exact: true }).count(), `${name}: long message missing`);
      await page.locator('#message-input').fill('这条草稿用于确认输入区始终可见。');
      await assertFramed(page, ['#app-view', '.main-chat', '.chat-header', '#current-room-name',
        '#message-list', '#composer', '#message-input', '#send-button', '#copy-room-button', '#leave-room-button'], `${name} chat`);
      const heading = await page.locator('#current-room-name').boundingBox();
      const actions = await page.locator('.chat-actions').boundingBox();
      assert.ok(heading.x + heading.width <= actions.x + 1, `${name}: room name overlaps header actions`);
      assert.equal(await page.locator('#message-list').evaluate(element => element.scrollWidth > element.clientWidth),
        false, `${name}: long message causes horizontal scrolling`);
      assert.equal(await page.locator('img').evaluateAll(images => images.some(image => !image.complete || image.naturalWidth === 0)),
        false, `${name}: an image asset did not load`);
      await screenshot('long-content');
      if (mobile) {
        await page.locator('#mobile-menu').click();
        await until(async () => { const box = await page.locator('#sidebar').boundingBox(); return box.x >= -1; },
          `${name}: drawer did not open`);
      }
      await assertFramed(page, ['#sidebar', '#room-search', '#create-room-button', '#join-room-button',
        '#all-rooms-filter', '#unread-rooms-filter', '#user-name', '#logout-button'], `${name} sidebar`);
      if (mobile) await assertFramed(page, ['#close-sidebar-button'], `${name} sidebar close`);
      await screenshot('rooms');
      await page.locator('#create-room-button').click();
      await page.locator('#room-name-input').fill('产品与工程讨论组');
      await assertFramed(page, ['#room-dialog', '#room-name-input', '#room-submit', '#cancel-room-button',
        '#close-room-dialog'], `${name} create room`);
      await screenshot('create-room');
      await page.locator('#close-room-dialog').click();
      await page.locator('#join-room-button').click();
      await page.locator('#join-room-id-input').fill('128');
      await assertFramed(page, ['#join-room-dialog', '#join-room-id-input', '#join-room-submit', '#cancel-join-room-button',
        '#close-join-room-dialog'], `${name} join room`);
      await screenshot('join-room');
      await page.locator('#close-join-room-dialog').click();
      await page.locator('#logout-button').click();
      await until(async () => await page.locator('#auth-view').isVisible(), `${name}: logout did not reveal authentication`);
      await assertFramed(page, ['#auth-title', '#login-tab', '#register-tab', '#username', '#password', '#password-toggle', '#auth-submit'],
        `${name} login`);
      await screenshot('login');
      await page.locator('#register-tab').click();
      assert.equal(await page.locator('#register-tab').getAttribute('aria-selected'), 'true');
      await page.locator('#username').fill('new_member_'.padEnd(64, 'z'));
      await assertFramed(page, ['#auth-title', '#login-tab', '#register-tab', '#username', '#password', '#password-toggle', '#auth-submit'],
        `${name} registration`);
      await screenshot('register');
      assert.deepEqual(f.errors, [], `${name}: browser or static asset errors`);
      console.log(`visual ${name} ${viewport.width}x${viewport.height}: long content, controls, dialogs, login and registration passed`);
    } finally { await f.context.close(); }
  }
}

async function showcaseScreenshots(browser) {
  const conversation = [
    ['chenxu', '下午好，周五的评审已经约好了，大家 15:00 有时间吗？'],
    ['linyan', '可以，我会带上最新的页面方案。'],
    ['momo', '我也可以。文案已经更新，想一起看下首屏的信息层级。'],
    ['chenxu', '好，今天先确认方向，明天再整理细节。\n参考稿放在设计灵感房间里了。'],
    ['linyan', '收到，我晚点补一下移动端。周五见！'],
  ];
  for (const [name, viewport] of [['desktop', { width: 1440, height: 900 }], ['mobile', { width: 390, height: 844 }]]) {
    const f = await fixture(browser, viewport, {
      username: 'linyan', roomNames: ['产品与工程', '设计灵感'],
      initialMessages: conversation.map(([senderId, content], index) => ({
        messageId: String(index + 1), roomId: '1', clientMessageId: `showcase-${index}`, senderId, content,
        createdAt: `2026-09-12T06:${String(20 + index).padStart(2, '0')}:00Z`,
      })),
    });
    try {
      if (name === 'mobile') await f.page.locator('#mobile-menu').click();
      await f.page.locator('.room-item[data-room-id="2"]').click();
      await until(async () => (await f.page.locator('#message-list').getAttribute('aria-busy')) === 'false', 'Showcase second room did not load');
      if (name === 'mobile') await f.page.locator('#mobile-menu').click();
      await f.page.locator('.room-item[data-room-id="1"]').click();
      await until(async () => (await f.page.locator('#message-list').getAttribute('aria-busy')) === 'false', 'Showcase conversation did not reload');
      f.incoming('周五的分享主题已经整理好了。', '2', 'momo');
      await until(async () => await f.page.locator('.room-item[data-room-id="2"] .room-unread').count(), 'Showcase unread preview did not appear');
      await f.page.screenshot({ path: path.join(artifacts, `showcase-${name}.png`), fullPage: true, animations: 'disabled' });
      if (name === 'mobile') {
        await f.page.locator('#mobile-menu').click();
        await f.page.screenshot({ path: path.join(artifacts, 'showcase-mobile-rooms.png'), fullPage: true, animations: 'disabled' });
      }
      assert.deepEqual(f.errors, []);
    } finally { await f.context.close(); }
  }
  console.log('showcase: normal desktop/mobile conversation screenshots captured with isolated fixtures');
}

async function liveSmoke(browser, baseUrl) {
  const alice = await browser.newContext({ viewport: { width: 1440, height: 900 } });
  const bob = await browser.newContext({ viewport: { width: 390, height: 844 } });
  const suffix = Date.now().toString(36);
  const password = 'Browser-smoke-password-2026';
  const pages = [];
  try {
    const bundledFont = fs.readdirSync(path.join(staticRoot, 'assets/fonts')).find(name => name.endsWith('.woff2'));
    assert.ok(bundledFont, 'No bundled WOFF2 font is available for live MIME verification');
    for (const [asset, expectedType] of [['/chat.js', 'application/javascript'], ['/chat.css', 'text/css'],
      ['/assets/icons/send.svg', 'image/svg+xml'], ['/assets/fonts/fonts.css', 'text/css'],
      [`/assets/fonts/${encodeURIComponent(bundledFont)}`, 'font/woff2']]) {
      const response = await alice.request.get(new URL(asset, baseUrl).href);
      assert.equal(response.status(), 200, `${asset}: ${await response.text()}`);
      const contentType = (response.headers()['content-type'] || '').split(';')[0].trim().toLowerCase();
      assert.equal(contentType, expectedType, `${asset}: live static asset returned the wrong MIME type`);
      assert.ok((await response.body()).length > 0, `${asset}: live static asset is empty`);
    }
    for (const [context, prefix] of [[alice, 'ui-alice'], [bob, 'ui-bob']]) {
      const username = `${prefix}-${suffix}`;
      const registration = await context.request.post(`${baseUrl}/register`, { data: { username, password } });
      assert.equal(registration.status(), 201, await registration.text());
      const page = await context.newPage();
      pages.push(page);
      await page.goto(baseUrl);
      await page.locator('#username').fill(username);
      await page.locator('#password').fill(password);
      await page.locator('#auth-submit').click();
      await until(async () => (await page.locator('#status-text').textContent()) === '已连接', 'Live login/WebSocket failed');
    }
    const roomId = await pages[0].evaluate(() => localStorage.getItem('linya.room'));
    await pages[1].locator('#mobile-menu').click();
    await pages[1].locator('#join-room-button').click();
    await pages[1].locator('#join-room-id-input').fill(roomId);
    await pages[1].locator('#join-room-submit').click();
    await until(async () => (await pages[1].evaluate(() => localStorage.getItem('linya.room'))) === roomId, 'Live room join failed');
    await until(async () => (await pages[1].locator('#message-list').getAttribute('aria-busy')) === 'false', 'Live history did not finish');
    const content = `Browser delivery ${suffix}`;
    await pages[0].locator('#message-input').fill(content);
    await pages[0].locator('#send-button').click();
    await until(async () => await pages[0].locator('.message-row.sent', { hasText: content }).count(), 'Live ACK not shown');
    await until(async () => await pages[1].locator('#message-list').getByText(content, { exact: true }).count(), 'Live broadcast not delivered');
    await pages[1].reload();
    await until(async () => await pages[1].locator('#message-list').getByText(content, { exact: true }).count(), 'Reload did not restore live history');
    await pages[1].locator('#mobile-menu').click();
    await pages[1].locator('#create-room-button').click();
    await pages[1].locator('#room-name-input').fill(`Background smoke ${suffix}`);
    await pages[1].locator('#room-submit').click();
    await until(async () => !(await pages[1].locator('#room-dialog').isVisible()), 'Live background room creation did not finish');
    const backgroundRoomId = await pages[1].evaluate(() => localStorage.getItem('linya.room'));
    assert.notEqual(backgroundRoomId, roomId, 'Creating a room did not change the live receiver room');
    await pages[1].reload();
    await until(async () => (await pages[1].locator('#status-text').textContent()) === '已连接', 'Background receiver did not reconnect');
    const backgroundContent = `Background delivery ${suffix}`;
    await pages[0].locator('#message-input').fill(backgroundContent);
    await pages[0].locator('#send-button').click();
    await until(async () => await pages[0].locator('.message-row.sent', { hasText: backgroundContent }).count(), 'Background message ACK not shown');
    const unreadRoom = pages[1].locator(`.room-item[data-room-id="${roomId}"]`);
    await until(async () => await unreadRoom.locator('.room-unread').count(), 'Unvisited room after reconnect did not receive an unread message');
    assert.equal(await pages[1].evaluate(() => localStorage.getItem('linya.room')), backgroundRoomId, 'Background message changed the active room');
    assert.equal(await pages[1].locator('#message-list').getByText(backgroundContent, { exact: true }).count(), 0,
      'Background message leaked into the active conversation');
    await pages[1].locator('#mobile-menu').click();
    await pages[1].locator('#unread-rooms-filter').click();
    assert.equal(await pages[1].locator('.room-item').count(), 1, 'Live unread filter includes read rooms');
    await unreadRoom.click();
    await until(async () => await pages[1].locator('#message-list').getByText(backgroundContent, { exact: true }).count(), 'Opening background room did not recover its message');
    assert.equal(await pages[1].locator('#message-list').getByText(backgroundContent, { exact: true }).count(), 1,
      'Live history duplicated a background delivery');
    assert.equal(await pages[1].locator('#unread-room-count').isVisible(), false, 'Reading the room did not clear live unread count');
    await pages[0].screenshot({ path: path.join(artifacts, 'live-desktop.png'), fullPage: true });
    await pages[1].screenshot({ path: path.join(artifacts, 'live-mobile.png'), fullPage: true });
    await pages[0].locator('#logout-button').click();
    await until(async () => await pages[0].locator('#auth-view').isVisible(), 'Live logout failed');
    console.log('live: JS/CSS/SVG/WOFF2 status and MIME, two users, registration, login, room join/create, ACK, broadcast, reload history, background unread after reconnect, read clearing and logout passed');
  } finally { await alice.close(); await bob.close(); }
}

(async () => {
  fs.mkdirSync(artifacts, { recursive: true });
  const suite = process.env.HTTPSERVER_UI_SUITE || 'all';
  assert.ok(['all', 'visual'].includes(suite), `Unknown UI suite: ${suite}`);
  const browser = await chromium.launch({ headless: true,
    ...(process.env.HTTPSERVER_UI_BROWSER_CHANNEL ? { channel: process.env.HTTPSERVER_UI_BROWSER_CHANNEL } : {}) });
  try {
    if (suite === 'all') {
      await desktopTests(browser);
      await mobileTests(browser);
      await sessionTests(browser);
      await controlTests(browser);
      await roomPresentationTests(browser);
      await backgroundReliabilityTests(browser);
      await initialHistoryRetryTests(browser);
      await fontTests(browser);
    }
    await visualTests(browser);
    await showcaseScreenshots(browser);
    if (suite === 'all' && process.env.HTTPSERVER_UI_BASE_URL) await liveSmoke(browser, process.env.HTTPSERVER_UI_BASE_URL);
  }
  finally { await browser.close(); }
})().catch(error => { console.error(error); process.exitCode = 1; });
