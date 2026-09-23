/**
 * Interactive console menus for the installer.
 *
 * Two modes share one interface:
 * - arrows: TTY raw mode + `readline.emitKeypressEvents`; ↑/↓ move, space
 *   toggles (multi-select), Enter confirms, a/n select all/clear
 *   (multi-select), Escape/Ctrl+C/EOF cancel with `MenuCancelled`.
 * - line: numbered input fallback for terminals without cursor control or
 *   `--ui=line`; the same a/n shortcuts work for multi-select.
 *
 * Every menu restores the terminal (raw mode off, listeners removed) on all
 * exit paths; a rendering error also cancels with the terminal restored.
 * Menus with no choices fail immediately instead of waiting for input, and a
 * closed stdin never resolves as a default confirmation. The readline
 * interface exists only while a line-mode question is pending, so an idle
 * interface never echoes keystrokes into arrow-mode redraws.
 */

import readline from 'node:readline';
import readlinePromises from 'node:readline/promises';

export class MenuCancelled extends Error {
  constructor(message = '已取消：未修改 Agent 配置。') {
    super(message);
    this.name = 'MenuCancelled';
  }
}

export class MenuInputError extends Error {
  constructor(message) {
    super(message);
    this.name = 'MenuInputError';
  }
}

/** Arrow mode requires raw-capable TTY streams on both ends and cursor control. */
export function arrowsSupported({ input, output, forceLine = false }) {
  if (forceLine) return false;
  if (!input?.isTTY || !output?.isTTY) return false;
  if (typeof input.setRawMode !== 'function') return false;
  return process.env.TERM !== 'dumb';
}

/** Display-width aware clip for narrow terminals (CJK counts as two cells). */
function clip(text, columns) {
  const limit = Math.max(1, columns - 2);
  let wide = 0;
  let index = 0;
  while (index < text.length) {
    const code = text.codePointAt(index);
    const width = code > 0x7f ? 2 : 1;
    if (wide + width > limit - 1 && index + (code > 0xffff ? 2 : 1) < text.length) {
      return `${text.slice(0, index)}…`;
    }
    if (wide + width > limit) return `${text.slice(0, index)}…`;
    wide += width;
    index += code > 0xffff ? 2 : 1;
  }
  return text;
}

function isCancelKey(key) {
  return key?.name === 'escape' || (key?.ctrl && (key?.name === 'c' || key?.name === 'd'));
}

function isEnterKey(key) {
  return key?.name === 'return' || key?.name === 'enter';
}

/**
 * Interactive UI over the given streams. Non-interactive callers should not
 * create this object at all; every method may prompt or cancel.
 */
export function createInteractiveUi({ input, output, forceLine = false }) {
  const columns = () => (typeof output.columns === 'number' && output.columns > 0 ? output.columns : 80);
  const mode = arrowsSupported({ input, output, forceLine }) ? 'arrows' : 'line';
  const closeRejections = new Set();
  let inputClosed = input.destroyed || input.readableEnded;
  const onInputClose = () => {
    inputClosed = true;
    for (const reject of closeRejections) reject(new MenuCancelled('输入已关闭，已取消。'));
    closeRejections.clear();
  };
  input.on('close', onInputClose);
  input.on('end', onInputClose);
  input.on('error', onInputClose);
  output.on('error', onInputClose);

  const ensureOpen = () => {
    if (inputClosed || input.destroyed || input.readableEnded) throw new MenuCancelled('输入已关闭，已取消。');
  };

  /** Line-mode question with an EOF race so a closed stdin never hangs. */
  async function ask(prompt) {
    ensureOpen();
    const io = readlinePromises.createInterface({ input, output });
    let rejectClosed;
    const closedPromise = new Promise((resolve, reject) => {
      rejectClosed = reject;
      closeRejections.add(reject);
    });
    const cancel = () => rejectClosed(new MenuCancelled());
    const cancelKey = (string, key) => { if (isCancelKey(key)) cancel(); };
    input.on('keypress', cancelKey);
    io.on('close', cancel);
    io.on('error', cancel);
    io.on('SIGINT', cancel);
    try {
      return String(await Promise.race([io.question(prompt), closedPromise]));
    } finally {
      closeRejections.delete(rejectClosed);
      input.removeListener('keypress', cancelKey);
      io.removeListener('close', cancel);
      io.removeListener('error', cancel);
      io.removeListener('SIGINT', cancel);
      io.close();
      input.pause();
    }
  }

  /** Write the block once; the returned callback rewrites it in place. */
  async function textInput(prompt, { defaultValue = '', secret = false } = {}) {
    if (!secret) return (await ask(`${prompt}${defaultValue ? ` [${defaultValue}]` : ''}> `)).trim() || defaultValue;
    ensureOpen();
    if (!input.isTTY || typeof input.setRawMode !== 'function') throw new MenuInputError('API Key 隐藏输入需要支持 raw mode 的终端；也可用 SIRCHMUNK_LLM_API_KEY 非交互配置。');
    output.write(`${prompt}${defaultValue ? ' [回车保持已保存的值]' : ''}（输入不显示）> `);
    const previousRaw = Boolean(input.isRaw);
    let value = '';
    let cleanup;
    try {
      return await new Promise((resolve, reject) => {
        const keypress = (text, key) => {
          if (isCancelKey(key)) { reject(new MenuCancelled()); return; }
          if (isEnterKey(key)) { resolve(value.trim() || defaultValue); return; }
          if (key?.name === 'backspace') value = [...value].slice(0, -1).join('');
          else if (!key?.ctrl && !key?.meta && text && !/[\r\n\0\x1b]/.test(text)) value += text;
        };
        cleanup = () => { closeRejections.delete(reject); input.removeListener('keypress', keypress); };
        closeRejections.add(reject);
        readline.emitKeypressEvents(input);
        input.on('keypress', keypress);
        input.setRawMode(true);
        input.resume();
      });
    } finally {
      cleanup?.();
      // Terminal teardown must not replace the prompt's result or cancellation.
      if (!input.destroyed) {
        try { input.setRawMode(previousRaw); } catch { /* terminal may already be gone */ }
      }
      try { input.pause(); } catch { /* still release listeners if the stream closed */ }
      if (!inputClosed && !output.destroyed && !output.writableEnded && !output.errored) {
        // write() can fail after ui.close() removes the UI's error listener.
        // On failure its callback precedes 'error', so retain this one-shot
        // listener until error/close; on success remove it immediately.
        const finishWrite = () => {
          output.removeListener('error', finishWrite);
          output.removeListener('close', finishWrite);
        };
        output.once('error', finishWrite);
        output.once('close', finishWrite);
        try {
          output.write('\n', error => { if (!error) finishWrite(); });
        } catch { finishWrite(); }
      }
    }
  }

  /** Write the block once; the returned callback rewrites it in place. */
  function beginBlock(lines) {
    const write = (block) => {
      for (const line of block) output.write(`${clip(line, columns())}\n`);
    };
    write(lines);
    let drawn = lines.length;
    return (next) => {
      if (drawn > 0) output.write(`\x1b[${drawn}A`);
      output.write('\r\x1b[J');
      write(next);
      drawn = next.length;
    };
  }

  /**
   * Generic keypress loop. `handleKey(key)` returns {redraw:true}, {value},
   * or {}; throwing cancels. The terminal is restored on every exit path.
   */
  function runArrowMenu({ header, linesFor, handleKey }) {
    return new Promise((resolve, reject) => {
      let rawEnabled = false;
      const wasRaw = Boolean(input.isRaw);
      let finished = false;
      const restore = () => {
        if (finished) return;
        finished = true;
        input.removeListener('keypress', onKeypress);
        closeRejections.delete(onClose);
        output.removeListener('resize', onResize);
        if (rawEnabled) {
          try { input.setRawMode(wasRaw); } catch { /* best effort */ }
        }
        input.pause();
      };
      const onKeypress = (string, key) => {
        if (finished) return;
        try {
          const outcome = handleKey(key);
          if (outcome?.redraw) redraw(linesFor());
          if (outcome && Object.hasOwn(outcome, 'value')) {
            redraw(linesFor());
            restore();
            resolve(outcome.value);
          }
        } catch (error) {
          restore();
          reject(error);
        }
      };
      const onClose = () => {
        restore();
        reject(new MenuCancelled());
      };
      const onResize = () => {
        try { redraw(linesFor()); } catch (error) { restore(); reject(error); }
      };
      let redraw;
      try {
        ensureOpen();
        output.write(`${header}\n`);
        redraw = beginBlock(linesFor());
        input.setRawMode(true);
        rawEnabled = true;
        readline.emitKeypressEvents(input);
        input.on('keypress', onKeypress);
        closeRejections.add(onClose);
        output.on('resize', onResize);
        input.resume();
      } catch (error) {
        restore();
        reject(error);
      }
    });
  }

  function arrowHint(multi) {
    return multi ? '（↑/↓ 移动，空格 勾选，a 全选，n 清空，回车 确认；Esc 取消）' : '（↑/↓ 移动，回车 确认；Esc 取消）';
  }

  // Keep the active choice visible when the product list exceeds the screen.
  function visibleChoices(lines, active) {
    const count = Math.max(1, (output.rows || 24) - 4);
    const start = Math.max(0, Math.min(active - Math.floor(count / 2), lines.length - count));
    return lines.slice(start, start + count);
  }

  async function singleSelectArrows(prompt, choices, { defaultIndex = 0 } = {}) {
    if (choices.length === 0) {
      throw new MenuInputError('没有可选项，无法继续（这是安装器缺陷，请反馈）。');
    }
    let active = Math.min(Math.max(defaultIndex, 0), choices.length - 1);
    const index = await runArrowMenu({
      header: `${prompt} ${arrowHint(false)}`,
      linesFor: () => visibleChoices(choices.map((choice, position) => `${position === active ? '❯' : ' '} ${choice}`), active),
      handleKey: (key) => {
        if (isCancelKey(key)) throw new MenuCancelled();
        if (key?.name === 'up') {
          active = (active - 1 + choices.length) % choices.length;
          return { redraw: true };
        }
        if (key?.name === 'down') {
          active = (active + 1) % choices.length;
          return { redraw: true };
        }
        if (isEnterKey(key)) return { value: active };
        return {};
      },
    });
    output.write(`已选择：${choices[index]}\n`);
    return index;
  }

  async function multiSelectArrows(prompt, entries, defaults) {
    if (entries.length === 0) {
      throw new MenuInputError('没有可选项，无法继续（这是安装器缺陷，请反馈）。');
    }
    const selected = new Set(defaults);
    let active = 0;
    const chosen = await runArrowMenu({
      header: `${prompt} ${arrowHint(true)}`,
      linesFor: () => visibleChoices(entries.map((entry, position) => `${position === active ? '❯' : ' '} ${selected.has(entry.key) ? '[x]' : '[ ]'} ${entry.label}${entry.note ? ` — ${entry.note}` : ''}`), active),
      handleKey: (key) => {
        if (isCancelKey(key)) throw new MenuCancelled();
        if (key?.name === 'up') {
          active = (active - 1 + entries.length) % entries.length;
          return { redraw: true };
        }
        if (key?.name === 'down') {
          active = (active + 1) % entries.length;
          return { redraw: true };
        }
        if (key?.name === 'space') {
          if (selected.has(entries[active].key)) selected.delete(entries[active].key);
          else selected.add(entries[active].key);
          return { redraw: true };
        }
        if (key?.name === 'a') {
          for (const entry of entries) selected.add(entry.key);
          return { redraw: true };
        }
        if (key?.name === 'n') {
          selected.clear();
          return { redraw: true };
        }
        if (isEnterKey(key)) return { value: selected };
        return {};
      },
    });
    output.write(`已选 ${chosen.size} 项${chosen.size > 0 ? `：${[...chosen].join(', ')}` : ''}。\n`);
    return chosen;
  }

  async function singleSelectLine(prompt, choices, { defaultIndex = 0 } = {}) {
    if (choices.length === 0) {
      throw new MenuInputError('没有可选项，无法继续（这是安装器缺陷，请反馈）。');
    }
    output.write(`${prompt}\n`);
    choices.forEach((choice, index) => {
      output.write(`  ${index + 1}. ${choice}${index === defaultIndex ? '（回车默认）' : ''}\n`);
    });
    while (true) {
      const answer = (await ask('>')).trim();
      if (answer === '') return defaultIndex;
      const index = Number(answer) - 1;
      if (Number.isInteger(index) && index >= 0 && index < choices.length) return index;
      output.write(`请输入 1-${choices.length} 的编号。\n`);
    }
  }

  async function multiSelectLine(prompt, entries, defaults) {
    if (entries.length === 0) {
      throw new MenuInputError('没有可选项，无法继续（这是安装器缺陷，请反馈）。');
    }
    output.write(`${prompt}\n`);
    entries.forEach((entry, index) => {
      const mark = defaults.includes(entry.key) ? '[x]' : '[ ]';
      output.write(`  ${index + 1}. ${mark} ${entry.label}${entry.note ? ` — ${entry.note}` : ''}\n`);
    });
    output.write('输入编号列表（如 1,3,5）、a=全选、n=全不选，回车保持上面的勾选状态。\n');
    while (true) {
      const answer = (await ask('>')).trim().toLowerCase();
      if (answer === '') return new Set(defaults);
      if (answer === 'a') return new Set(entries.map((entry) => entry.key));
      if (answer === 'n') return new Set();
      const picks = answer.split(/[,，\s]+/).map((value) => Number(value) - 1);
      if (picks.length > 0 && picks.every((index) => Number.isInteger(index) && index >= 0 && index < entries.length)) {
        return new Set(picks.map((index) => entries[index].key));
      }
      output.write('无法识别输入，请重试。\n');
    }
  }

  /** y/yes confirms; anything else (including empty input) declines. */
  async function confirmLine(prompt) {
    const answer = (await ask(`${prompt}（y/N）> `)).trim().toLowerCase();
    return answer === 'y' || answer === 'yes';
  }

  return {
    mode,
    textInput,
    close: () => {
      onInputClose();
      input.removeListener('close', onInputClose);
      input.removeListener('end', onInputClose);
      input.removeListener('error', onInputClose);
      output.removeListener('error', onInputClose);
      input.pause();
    },
    singleSelect: mode === 'arrows' ? singleSelectArrows : singleSelectLine,
    multiSelect: mode === 'arrows' ? multiSelectArrows : multiSelectLine,
    confirm: confirmLine,
  };
}
