import assert from 'node:assert/strict';
import { PassThrough } from 'node:stream';
import { spawnSync } from 'node:child_process';
import test, { beforeEach } from 'node:test';

import { MenuCancelled, MenuInputError, arrowsSupported, createInteractiveUi } from '../src/ui/terminalMenu.mjs';

beforeEach((t) => {
  const previous = process.env.TERM;
  process.env.TERM = 'xterm';
  t.after(() => {
    if (previous === undefined) delete process.env.TERM;
    else process.env.TERM = previous;
  });
});

function fakeStreams({ columns = 100, rows = 24 } = {}) {
  const input = new PassThrough();
  Object.defineProperty(input, 'isTTY', { value: true });
  const rawModeStates = [];
  input.setRawMode = (state) => { rawModeStates.push(Boolean(state)); };
  const output = new PassThrough();
  Object.defineProperty(output, 'isTTY', { value: true });
  Object.defineProperty(output, 'columns', { value: columns, writable: true });
  Object.defineProperty(output, 'rows', { value: rows, writable: true });
  let captured = '';
  output.on('data', (chunk) => { captured += chunk.toString('utf8'); });
  output.resume();
  return { input, output, rawModeStates, read: () => captured };
}

async function withTimeout(promise, milliseconds = 4000) {
  let timer;
  try {
    return await Promise.race([promise, new Promise((resolve, reject) => {
      timer = setTimeout(() => reject(new Error('menu did not settle')), milliseconds);
    })]);
  } finally { clearTimeout(timer); }
}

test('secret input never echoes input or saved defaults and restores raw mode on cancellation', async () => {
  for (const [previousRaw, cancel] of [[false, false], [false, true], [true, false], [true, true]]) {
    const streams = fakeStreams();
    streams.input.isRaw = previousRaw;
    const ui = createInteractiveUi(streams);
    const pending = withTimeout(ui.textInput('API Key', { defaultValue: 'saved-secret', secret: true }));
    streams.input.write('new-secret');
    streams.input.write(cancel ? '\x03' : '\r');
    if (cancel) await assert.rejects(pending, MenuCancelled);
    else assert.equal(await pending, 'new-secret');
    assert.equal(streams.read().includes('secret'), false);
    assert.equal(streams.rawModeStates.at(-1), previousRaw);
    assert.equal(streams.input.listenerCount('keypress'), 0);
    ui.close();
    await new Promise(resolve => setImmediate(resolve));
    assert.equal(streams.output.listenerCount('error'), 0);
  }
});

test('secret input preserves cancellation when stdin is destroyed', async t => {
  for (const error of [undefined, new Error('terminal disconnected')]) {
    const streams = fakeStreams();
    const ui = createInteractiveUi(streams);
    t.after(() => ui.close());
    const setRawMode = streams.input.setRawMode;
    streams.input.setRawMode = state => {
      if (streams.input.destroyed) throw Object.assign(new Error('stream destroyed'), { code: 'ERR_DESTROYED' });
      setRawMode(state);
    };
    const pending = withTimeout(ui.textInput('API Key', { secret: true }));
    streams.input.write('private-value');
    streams.input.destroy(error);
    await assert.rejects(pending, error => error instanceof MenuCancelled && /输入已关闭/.test(error.message));
    assert.equal(streams.input.listenerCount('keypress'), 0);
    assert.equal(streams.read().includes('private-value'), false);
  }
});

test('secret input cleanup failures preserve cancellation and still release input', async t => {
  for (const failure of ['raw-mode', 'newline']) {
    const streams = fakeStreams();
    const ui = createInteractiveUi(streams);
    t.after(() => ui.close());
    const pending = withTimeout(ui.textInput('API Key', { secret: true }));
    if (failure === 'raw-mode') streams.input.setRawMode = () => { throw new Error('raw mode restore failed'); };
    else streams.output.write = () => { throw new Error('terminal write failed'); };
    streams.input.write('\x03');
    await assert.rejects(pending, MenuCancelled);
    assert.equal(streams.input.listenerCount('keypress'), 0);
    assert.equal(streams.input.isPaused(), true);
  }
});

test('secret input does not write cleanup output after a stream error or UI close', async t => {
  for (const failure of ['output-error', 'ui-close']) {
    const streams = fakeStreams();
    const ui = createInteractiveUi(streams);
    t.after(() => ui.close());
    const pending = withTimeout(ui.textInput('API Key', { secret: true }));
    const before = streams.read();
    if (failure === 'output-error') streams.output.destroy(new Error('output failed'));
    else ui.close();
    const write = t.mock.method(streams.output, 'write', streams.output.write.bind(streams.output));
    await assert.rejects(pending, MenuCancelled);
    assert.equal(write.mock.callCount(), 0);
    assert.equal(streams.read(), before);
    assert.equal(streams.input.listenerCount('keypress'), 0);
    assert.equal(streams.input.isPaused(), true);
  }
});

test('a delayed cleanup write error cannot crash after the secret prompt and UI have closed', () => {
  const module = new URL('../src/ui/terminalMenu.mjs', import.meta.url).href;
  // A real Writable reports errors asynchronously. Isolate the process so an
  // unhandled error proves the regression without crashing the test runner.
  const script = `
    import assert from 'node:assert/strict';
    import { PassThrough, Writable } from 'node:stream';
    import { MenuCancelled, createInteractiveUi } from ${JSON.stringify(module)};
    for (const [cancel, closeOutput] of [[false, false], [true, false], [true, true]]) {
      const input = new PassThrough();
      input.isTTY = true;
      input.setRawMode = value => { input.isRaw = value; };
      let finishWrite;
      const output = new Writable({ write(chunk, encoding, callback) {
        if (chunk.toString() === '\\n') finishWrite = callback;
        else callback();
      } });
      const ui = createInteractiveUi({ input, output });
      const pending = ui.textInput('API Key', { secret: true });
      input.write('private-value');
      input.write(cancel ? '\\x03' : '\\r');
      if (cancel) await assert.rejects(pending, MenuCancelled);
      else assert.equal(await pending, 'private-value');
      ui.close();
      assert.equal(input.isRaw, false);
      assert.equal(input.listenerCount('keypress'), 0);
      assert.equal(typeof finishWrite, 'function');
      if (closeOutput) output.destroy();
      else finishWrite(new Error('delayed terminal write failure'));
      await new Promise(resolve => setImmediate(resolve));
      assert.equal(output.listenerCount('error'), 0);
      assert.equal(output.listenerCount('close'), 0);
    }
  `;
  const result = spawnSync(process.execPath, ['--input-type=module', '--eval', script], { encoding: 'utf8', timeout: 5000 });
  assert.equal(result.status, 0, result.stderr);
  assert.equal(result.stdout, '');
});

test('EOF without stream close cancels both modes and future questions', async () => {
  for (const forceLine of [false, true]) {
    const { input, output } = fakeStreams();
    const ui = createInteractiveUi({ input, output, forceLine });
    const pending = withTimeout(ui.singleSelect('choose', ['a']));
    input.emit('end');
    await assert.rejects(pending, MenuCancelled);
    await assert.rejects(withTimeout(ui.singleSelect('again', ['a'])), MenuCancelled);
    ui.close();
  }
});

test('line Ctrl+C and Ctrl+D cancel without requiring stdin close', async () => {
  for (const key of ['\x03', '\x04']) {
    const { input, output } = fakeStreams();
    const ui = createInteractiveUi({ input, output, forceLine: true });
    const pending = withTimeout(ui.confirm('remove?'));
    input.write(key);
    await assert.rejects(pending, MenuCancelled);
    ui.close();
  }
});

test('line Escape cancels without accepting a default choice', async () => {
  const { input, output } = fakeStreams();
  const ui = createInteractiveUi({ input, output, forceLine: true });
  const pending = withTimeout(ui.singleSelect('choose', ['a']));
  input.write('\x1b');
  await assert.rejects(pending, MenuCancelled);
  ui.close();
});

test('closing an active menu cancels and releases the input stream', async () => {
  for (const forceLine of [false, true]) {
    const { input, output } = fakeStreams();
    const ui = createInteractiveUi({ input, output, forceLine });
    const pending = withTimeout(ui.singleSelect('choose', ['a']));
    ui.close();
    await assert.rejects(pending, MenuCancelled);
    assert.equal(input.isPaused(), true);
    assert.equal(input.listenerCount('keypress'), 0);
  }
});

test('completed arrow menu releases stdin so the installer can exit', async () => {
  const { input, output } = fakeStreams();
  const ui = createInteractiveUi({ input, output });
  const pending = ui.singleSelect('choose', ['a']);
  input.write('\r');
  await pending;
  ui.close();
  assert.equal(input.isPaused(), true);
});

test('arrowsSupported requires TTYs, raw mode, cursor control, and no --ui=line', () => {
  const { input, output } = fakeStreams();
  assert.equal(arrowsSupported({ input, output }), true);
  assert.equal(arrowsSupported({ input, output, forceLine: true }), false);
  assert.equal(arrowsSupported({ input: new PassThrough(), output }), false);
  const previous = process.env.TERM;
  process.env.TERM = 'dumb';
  try {
    assert.equal(arrowsSupported({ input, output }), false);
  } finally {
    if (previous === undefined) delete process.env.TERM;
    else process.env.TERM = previous;
  }
});

test('arrow single select moves and confirms, then restores raw mode', async () => {
  const { input, output, rawModeStates, read } = fakeStreams();
  const ui = createInteractiveUi({ input, output });
  const chosen = withTimeout(ui.singleSelect('后端', ['tgrep', 'codegraph'], { defaultIndex: 0 }));
  input.write('\x1b[B'); // down
  input.write('\r'); // enter
  assert.equal(await chosen, 1);
  assert.match(read(), /已选择：codegraph/);
  assert.deepEqual(rawModeStates, [true, false]);
  ui.close();
});

test('arrow single select keeps the default on plain Enter', async () => {
  const { input } = fakeStreams();
  const ui = createInteractiveUi({ input, output: fakeStreams().output });
  const chosen = withTimeout(ui.singleSelect('镜像', ['cn', 'official'], { defaultIndex: 1 }));
  input.write('\r');
  assert.equal(await chosen, 1);
  ui.close();
});

test('arrow single select cancels on Escape/Ctrl+C/EOF with raw mode restored', async () => {
  for (const cancel of ['\x1b', '\x03', 'EOF']) {
    const streams = fakeStreams();
    const ui = createInteractiveUi({ input: streams.input, output: streams.output });
    const pending = withTimeout(ui.singleSelect('后端', ['tgrep', 'codegraph']));
    if (cancel === 'EOF') streams.input.end();
    else streams.input.write(cancel);
    await assert.rejects(pending, MenuCancelled);
    assert.deepEqual(streams.rawModeStates, [true, false], `raw mode restored after ${cancel}`);
    ui.close();
  }
});

test('arrow multi select toggles, wraps, selects all, and clears', async () => {
  const { input, output, read } = fakeStreams();
  const ui = createInteractiveUi({ input, output });
  const entries = [
    { key: 'a', label: 'A' },
    { key: 'b', label: 'B' },
    { key: 'c', label: 'C' },
  ];

  const first = withTimeout(ui.multiSelect('选择', entries, []));
  input.write(' '); // toggle first
  input.write('\x1b[A'); // up from index 0 wraps to last
  input.write(' '); // toggle last
  input.write('\r');
  assert.deepEqual([...(await first)].sort(), ['a', 'c']);
  assert.match(read(), /已选 2 项：a, c/);

  const all = withTimeout(ui.multiSelect('选择', entries, []));
  input.write('a');
  input.write('\r');
  assert.equal((await all).size, 3);

  const none = withTimeout(ui.multiSelect('选择', entries, ['a']));
  input.write('n');
  input.write('\r');
  assert.equal((await none).size, 0);
  ui.close();
});

test('arrow menus render marks and clip long labels for narrow terminals', async () => {
  const wide = fakeStreams();
  const wideUi = createInteractiveUi({ input: wide.input, output: wide.output });
  const pending = withTimeout(wideUi.multiSelect('选择', [
    { key: 'a', label: '短标签', note: '备注' },
    { key: 'b', label: 'B'.repeat(120) },
  ], []));
  wide.input.write(' ');
  wide.input.write('\r');
  await pending;
  assert.match(wide.read(), /\[x\] 短标签 — 备注/);
  wideUi.close();

  const narrow = fakeStreams({ columns: 30 });
  const narrowUi = createInteractiveUi({ input: narrow.input, output: narrow.output });
  const narrowPending = withTimeout(narrowUi.singleSelect('后端', ['X'.repeat(80), 'codegraph']));
  narrow.input.write('\r');
  await narrowPending;
  assert.match(narrow.read(), /…/);
  narrowUi.close();
});

test('line mode select reads numbers, keeps defaults on Enter, and rejects garbage', async () => {
  const { input, output, read } = fakeStreams();
  const ui = createInteractiveUi({ input, output, forceLine: true });
  assert.equal(ui.mode, 'line');

  const first = withTimeout(ui.singleSelect('后端', ['tgrep', 'codegraph'], { defaultIndex: 0 }));
  input.write('2\n');
  assert.equal(await first, 1);

  const second = withTimeout(ui.singleSelect('后端', ['tgrep', 'codegraph'], { defaultIndex: 1 }));
  input.write('\n');
  assert.equal(await second, 1);

  const third = withTimeout(ui.singleSelect('后端', ['tgrep', 'codegraph'], { defaultIndex: 0 }));
  input.write('x\n');
  await new Promise((resolve) => setImmediate(resolve));
  input.write('1\n');
  assert.equal(await third, 0);
  assert.match(read(), /请输入 1-2 的编号/);
  ui.close();
});

test('line mode multi select supports lists, a/n, defaults, and EOF cancels', async () => {
  const { input } = fakeStreams();
  const output = fakeStreams().output;
  const ui = createInteractiveUi({ input, output, forceLine: true });
  const entries = [
    { key: 'a', label: 'A' },
    { key: 'b', label: 'B' },
    { key: 'c', label: 'C' },
  ];

  const first = withTimeout(ui.multiSelect('选择', entries, []));
  input.write('1,3\n');
  assert.deepEqual([...(await first)].sort(), ['a', 'c']);

  const all = withTimeout(ui.multiSelect('选择', entries, []));
  input.write('a\n');
  assert.equal((await all).size, 3);

  const none = withTimeout(ui.multiSelect('选择', entries, ['b']));
  input.write('n\n');
  assert.equal((await none).size, 0);

  const keep = withTimeout(ui.multiSelect('选择', entries, ['b', 'c']));
  input.write('\n');
  assert.deepEqual([...(await keep)].sort(), ['b', 'c']);

  // A closed stdin never resolves a question as a default confirmation.
  const eof = withTimeout(ui.multiSelect('选择', entries, ['a']));
  input.end();
  await assert.rejects(eof, MenuCancelled);
  ui.close();
});

test('confirm accepts only explicit y/yes and declines everything else', async () => {
  const cases = [
    { input: 'y\n', expected: true },
    { input: 'yes\n', expected: true },
    { input: '\n', expected: false },
    { input: 'no\n', expected: false },
  ];
  for (const { input: typed, expected } of cases) {
    const streams = fakeStreams();
    const ui = createInteractiveUi({ input: streams.input, output: streams.output, forceLine: true });
    const pending = withTimeout(ui.confirm('确认移除？'));
    streams.input.write(typed);
    assert.equal(await pending, expected, `answer ${JSON.stringify(typed)}`);
    ui.close();
  }
});

test('menus without choices fail immediately without reading input', async () => {
  const { input, output } = fakeStreams();
  const ui = createInteractiveUi({ input, output });
  await assert.rejects(ui.singleSelect('空', []), MenuInputError);
  await assert.rejects(ui.multiSelect('空', [], []), MenuInputError);
  const lineUi = createInteractiveUi({ input, output, forceLine: true });
  await assert.rejects(lineUi.singleSelect('空', []), MenuInputError);
  ui.close();
  lineUi.close();
});

test('short terminals keep the active choice visible and redraw on resize', async () => {
  const { input, output, read } = fakeStreams({ columns: 12, rows: 8 });
  const ui = createInteractiveUi({ input, output });
  const choices = Array.from({ length: 20 }, (_, i) => `项目${i} 名称很长`);
  const pending = ui.singleSelect('选择', choices);
  for (let i = 0; i < 19; i += 1) input.write('\x1b[B');
  const lastFrame = () => read().split('\r\x1b[J').at(-1).trimEnd().split('\n');
  assert.equal(lastFrame().length, 4);
  assert.ok(lastFrame().some((line) => line.includes('❯ 项目19')));
  for (const line of lastFrame()) {
    assert.ok([...line].reduce((width, char) => width + (char.codePointAt(0) > 0x7f ? 2 : 1), 0) <= 12);
  }
  output.rows = 6;
  output.columns = 40;
  output.emit('resize');
  assert.equal(lastFrame().length, 2);
  assert.ok(lastFrame().some((line) => line.includes('❯ 项目19 名称很长')));
  input.write('\r');
  assert.equal(await pending, 19);
  ui.close();
  assert.equal(output.listenerCount('resize'), 0);
});

test('stream and rendering errors settle the menu and restore raw mode', async () => {
  for (const failure of ['input', 'output', 'render']) {
    const { input, output, rawModeStates } = fakeStreams();
    const ui = createInteractiveUi({ input, output });
    const pending = withTimeout(ui.singleSelect('choose', ['a', 'b']));
    if (failure === 'render') {
      output.write = () => { throw new Error('render failed'); };
      input.write('\x1b[B');
      await assert.rejects(pending, /render failed/);
    } else {
      (failure === 'input' ? input : output).emit('error', new Error('stream failed'));
      await assert.rejects(pending, MenuCancelled);
    }
    assert.deepEqual(rawModeStates, [true, false]);
    ui.close();
  }
});
