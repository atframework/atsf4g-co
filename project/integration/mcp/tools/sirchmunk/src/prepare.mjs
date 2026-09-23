import fs from 'node:fs';
import path from 'node:path';
import { spawn } from 'node:child_process';
import { commandPaths, readJson } from '../../../common/src/localTools.mjs';
import { run } from '../../../common/src/command.mjs';
import { minimalEnvironment } from '../../../common/src/supervisor.mjs';
import { INTEGRATION_ROOT, validateWorkspaceBuildDir, isWithin } from '../../../common/src/paths.mjs';

/** Import completed legacy model files, never locks, logs or absolute status paths. */
export function modelCacheDirectory(paths, source) {
  const target = path.join(paths.downloadsDir, 'models/sirchmunk');
  validateWorkspaceBuildDir(paths.repoRoot, target);
  if (fs.existsSync(target) || !source || !fs.existsSync(source)) return target;
  validateWorkspaceBuildDir(paths.repoRoot, source);
  if (!['ready', 'downloaded'].includes(readJson(path.join(source, 'download-state.json'))?.state)) return target;
  const hub = path.join(source, 'huggingface/hub');
  if (!fs.existsSync(hub)) return target;
  validateWorkspaceBuildDir(paths.repoRoot, paths.agentTmpDir);
  fs.mkdirSync(paths.agentTmpDir, { recursive: true });
  const stage = fs.mkdtempSync(path.join(paths.agentTmpDir, 'model-import-'));
  try {
    const canonical = fs.realpathSync(hub);
    fs.cpSync(hub, path.join(stage, 'huggingface/hub'), { recursive: true, dereference: true, filter: file => {
      if (path.basename(file) === '.locks' || file.endsWith('.incomplete')) return false;
      if (!isWithin(fs.realpathSync(file), canonical)) throw new Error('legacy model cache link escapes its cache');
      return true;
    } });
    fs.mkdirSync(path.dirname(target), { recursive: true });
    try { fs.renameSync(stage, target); }
    catch (error) { if (!['EEXIST', 'ENOTEMPTY'].includes(error.code)) throw error; }
  } finally { fs.rmSync(stage, { recursive: true, force: true }); }
  return target;
}

export function pythonExecutable(explicit, execute = run) {
  const candidates = explicit ? [[path.resolve(explicit)]] : [
    ...commandPaths('py').flatMap(file => ['3.12', '3.13', '3.11', '3'].map(version => [file, '-' + version])),
    ...['python3', 'python'].flatMap(name => commandPaths(name).filter(file => !/\.(cmd|bat|ps1)$/i.test(file)).map(file => [file])),
  ];
  for (const candidate of candidates) {
    const result = execute([...candidate, '-I', '-c', 'import sys; assert sys.version_info >= (3,10); print(sys.executable)'], { check: false, timeout: 15000 });
    if (result.status === 0 && path.isAbsolute(result.stdout?.trim())) return result.stdout.trim();
  }
  throw new Error('Sirchmunk requires Python >= 3.10 with venv/pip; install Python or pass --sirchmunk-python=<python executable>');
}

export function prepareSirchmunk(paths, mcpRoot, { offline = false, python = null, indexURL = 'https://pypi.org/simple', execute = run, log = () => {} } = {}) {
  const script = path.join(mcpRoot, 'tools/sirchmunk/python/bootstrap.py');
  const root = path.join(paths.downloadsDir, 'python', `${process.platform}-${process.arch}`, 'sirchmunk');
  validateWorkspaceBuildDir(paths.repoRoot, root);
  const interpreter = pythonExecutable(python, execute);
  const env = { ...process.env, PIP_DISABLE_PIP_VERSION_CHECK: '1' };
  delete env.SIRCHMUNK_LLM_API_KEY;
  log('preparing pinned Sirchmunk in a workspace Python venv (first installation includes embedding dependencies)');
  const result = execute([interpreter, '-I', script, '--root', root, '--downloads', paths.downloadsDir, '--index-url', indexURL, ...(offline ? ['--offline'] : [])],
    { cwd: paths.repoRoot, check: false, timeout: 1800000, env });
  if (result.status !== 0) throw new Error('Sirchmunk preparation failed; see ' + path.join(root, 'prepare.log') + '\n' + String(result.stderr ?? '').slice(-1800));
  const state = readJson(path.join(root, 'prepared.json'));
  if (!state?.python || !state?.version) throw new Error('Sirchmunk preparation did not produce a verified environment');
  const old = readJson(paths.preparedStateReadPath())?.sirchmunk?.model_dir;
  return { ...state, model_dir: modelCacheDirectory(paths, old), offline };
}

/** This process only downloads/validates a model; it has no LLM credentials. */
export function startModelDownload(paths, prepared, integrationRoot = INTEGRATION_ROOT) {
  if (prepared.offline) return false;
  validateWorkspaceBuildDir(paths.repoRoot, prepared.model_dir);
  fs.mkdirSync(prepared.model_dir, { recursive: true });
  const cached = readJson(path.join(prepared.model_dir, 'download-state.json'));
  const pin = readJson(path.join(integrationRoot, 'tools/sirchmunk/upstream-lock.json'));
  if (cached?.state === 'ready' && cached.revision === pin?.embedding.revision && fs.existsSync(cached.snapshot ?? '')) return false;
  const logFile = fs.openSync(path.join(prepared.model_dir, 'download.log'), 'a');
  try {
    const child = spawn(prepared.python, ['-I', path.join(integrationRoot, 'tools/sirchmunk/python/model.py'), prepared.model_dir], {
      cwd: paths.repoRoot, windowsHide: true, detached: true, shell: false, stdio: ['ignore', logFile, logFile],
      env: minimalEnvironment({ PYTHONUTF8: '1', PYTHONNOUSERSITE: '1', HF_HUB_DISABLE_TELEMETRY: '1', DO_NOT_TRACK: '1' }),
    });
    child.once('error', () => process.stderr.write('Sirchmunk: could not start model download; the MCP entry will retry.\n'));
    child.unref();
    return true;
  } finally { fs.closeSync(logFile); }
}
