/** Credentials are local workspace data; never include them in Agent entries. */
import fs from 'node:fs';
import path from 'node:path';
import { validateWorkspaceBuildDir } from '../../../common/src/paths.mjs';

export const configPath = paths => path.join(paths.privateDir, 'sirchmunk.json');

export function validateConfig(input) {
  for (const name of ['baseURL', 'apiKey', 'model']) {
    if (typeof input?.[name] !== 'string' || !input[name].trim() || /[\r\n\0]/.test(input[name])) {
      throw new Error(`Sirchmunk requires a nonempty ${name}`);
    }
  }
  let url;
  try { url = new URL(input.baseURL); } catch { throw new Error('Sirchmunk baseURL must be an http(s) URL'); }
  if (!['https:', 'http:'].includes(url.protocol) || url.username || url.password || url.search || url.hash) {
    throw new Error('Sirchmunk baseURL must be an http(s) URL without credentials, query or fragment');
  }
  return { baseURL: input.baseURL.trim(), apiKey: input.apiKey.trim(), model: input.model.trim() };
}

export function readConfig(paths) {
  const file = paths.readPath('private/sirchmunk.json');
  validateWorkspaceBuildDir(paths.repoRoot, file);
  if (!fs.existsSync(file)) return null;
  if (fs.lstatSync(file).isSymbolicLink()) throw new Error('Sirchmunk credentials must not be a symlink');
  let data;
  try { data = JSON.parse(fs.readFileSync(file, 'utf8')); }
  catch { throw new Error('Sirchmunk local credential file is invalid; correct it before continuing'); }
  return validateConfig(data);
}

export function writeConfig(paths, config) {
  const file = configPath(paths);
  const content = JSON.stringify(validateConfig(config), null, 2) + '\n';
  validateWorkspaceBuildDir(paths.repoRoot, file);
  if (fs.existsSync(file) && fs.lstatSync(file).isSymbolicLink()) throw new Error('Sirchmunk credentials must not be a symlink');
  if (fs.existsSync(file) && fs.readFileSync(file, 'utf8') === content) return file;
  fs.mkdirSync(paths.privateDir, { recursive: true, mode: 0o700 });
  const temporary = file + '.' + process.pid + '.tmp';
  try {
    fs.writeFileSync(temporary, content, { mode: 0o600, flag: 'wx' });
    fs.renameSync(temporary, file);
  } finally { if (fs.existsSync(temporary)) fs.unlinkSync(temporary); }
  return file;
}

export async function collectConfig(paths, { options, ui, env = process.env }) {
  const old = readConfig(paths);
  const defaults = {
    baseURL: options.llmBaseURL ?? env.SIRCHMUNK_LLM_BASE_URL ?? old?.baseURL ?? '',
    apiKey: env.SIRCHMUNK_LLM_API_KEY ?? old?.apiKey ?? '',
    model: options.llmModel ?? env.SIRCHMUNK_LLM_MODEL ?? old?.model ?? '',
  };
  if (ui.interactive) {
    defaults.baseURL = await ui.menu.textInput('Sirchmunk LLM baseURL', { defaultValue: defaults.baseURL });
    defaults.apiKey = await ui.menu.textInput('Sirchmunk LLM API Key', { defaultValue: defaults.apiKey, secret: true });
    defaults.model = await ui.menu.textInput('Sirchmunk LLM 模型名', { defaultValue: defaults.model });
  }
  try { return validateConfig(defaults); }
  catch (error) { throw new Error(`${error.message}; interactive setup asks for all three fields. For --yes use --llm-base-url, --llm-model and SIRCHMUNK_LLM_API_KEY (not a command-line secret).`); }
}
