import assert from 'node:assert/strict';
import test from 'node:test';
import { mirrorSettings, selectMirrors } from '../src/mirrors.mjs';

test('domestic interactive preparation offers npm and Cargo independently', async () => {
  const prompts = [];
  const menu = { singleSelect: async (title, choices, options) => {
    prompts.push({ title, choices, options });
    return choices.findIndex(choice => choice.startsWith(prompts.length === 1 ? 'huawei ' : 'ustc '));
  } };
  const selected = await selectMirrors({ options: { mirror: 'cn' }, backend: 'tgrep', suggested: 'cn', menu });
  assert.equal(prompts.length, 2);
  assert.equal(selected.npmRegistry, 'https://repo.huaweicloud.com/repository/npm/');
  assert.deepEqual(selected.cargoConfigArgs, ['--config', 'source.crates-io.replace-with="mcp-mirror"', '--config', 'source.mcp-mirror.registry="sparse+https://mirrors.ustc.edu.cn/crates.io-index/"']);
});

test('CodeGraph still displays the Cargo source selection; explicit selections skip menus', async () => {
  let questions = 0;
  const menu = { singleSelect: async (title, choices) => {
    questions++;
    if (questions === 2) assert.match(title, /Cargo.*本次不使用/);
    assert.ok(choices.some(choice => choice.includes('https://')), 'show actual addresses');
    return choices.findIndex(choice => choice.startsWith(questions === 1 ? 'sjtug ' : 'official '));
  } };
  const selected = await selectMirrors({ options: { mirror: 'cn' }, backend: 'codegraph', suggested: 'cn', menu });
  assert.equal(questions, 2);
  assert.equal(selected.npmId, 'sjtug');
  assert.equal(selected.cargoId, 'official');
  const explicit = await selectMirrors({ options: { mirror: 'cn', npmMirror: 'huawei', cargoMirror: 'tuna' }, backend: 'tgrep', suggested: 'cn', menu });
  assert.equal(questions, 2);
  assert.equal(explicit.cargoId, 'tuna');
});

test('noninteractive and offline preparation use defaults without any question', async () => {
  const menu = { singleSelect: () => { throw new Error('unexpected prompt'); } };
  for (const options of [{ yes: true }, { offline: true }]) {
    const selected = await selectMirrors({ options, backend: 'tgrep', suggested: 'cn', menu });
    assert.equal(selected.npmId, 'npmmirror');
    assert.equal(selected.cargoId, 'rsproxy');
  }
  assert.equal(mirrorSettings().npmRegistry, 'https://registry.npmjs.org');
  assert.deepEqual(mirrorSettings().cargoConfigArgs, []);
  assert.equal(mirrorSettings().cargoIsolated, true, 'official Cargo must not inherit a global replacement');
  const nonTty = await selectMirrors({ options: { mirror: 'cn' }, interactive: false, backend: 'tgrep', suggested: 'cn', menu });
  assert.equal(nonTty.npmId, 'npmmirror');
  const explicit = await selectMirrors({ options: { npmMirror: 'huawei', cargoMirror: 'tuna' }, backend: 'tgrep', suggested: 'cn', menu });
  assert.equal(explicit.npmId, 'huawei');
});

for (const [npmId, cargoId] of [['official', 'ustc'], ['npmmirror', 'official']]) {
  test(`source menus independently select npm ${npmId} and Cargo ${cargoId} even with official defaults`, async () => {
    const prompts = [];
    const menu = { singleSelect: async (title, choices, { defaultIndex }) => {
      prompts.push(title);
      assert.match(choices[defaultIndex], /^official /);
      const index = choices.findIndex(choice => choice.startsWith(`${prompts.length === 1 ? npmId : cargoId} `));
      assert.ok(index >= 0);
      return index;
    } };
    const result = await selectMirrors({ options: { mirror: 'official' }, backend: 'tgrep', suggested: 'cn', menu });
    assert.equal(prompts.length, 2);
    assert.equal(result.npmId, npmId);
    assert.equal(result.cargoId, cargoId);
  });
}

test('an explicit npm source still prompts for Cargo and honours cancellation', async () => {
  const cancelled = new Error('cancelled');
  let count = 0;
  await assert.rejects(selectMirrors({ options: { npmMirror: 'huawei' }, backend: 'codegraph', suggested: 'cn',
    menu: { singleSelect: async title => { count++; assert.match(title, /Cargo/); throw cancelled; } },
  }), error => error === cancelled);
  assert.equal(count, 1);
});

test('invalid mirror ids fail before preparing any dependencies', () => {
  assert.throws(() => mirrorSettings({ mirror: 'bad' }), /--mirror/);
  assert.throws(() => mirrorSettings({ npmMirror: 'bad' }), /--npm-mirror/);
  assert.throws(() => mirrorSettings({ cargoMirror: 'bad' }), /--cargo-mirror/);
});
