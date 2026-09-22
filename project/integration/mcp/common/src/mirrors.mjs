/** Per-invocation package sources. Never write the user's npm or Cargo config. */
export const NPM_MIRRORS = Object.freeze({
  npmmirror: { label: '阿里云 / 淘宝 npmmirror（推荐）', url: 'https://registry.npmmirror.com', docs: 'https://developer.aliyun.com/mirror/NPM' },
  huawei: { label: '华为云', url: 'https://repo.huaweicloud.com/repository/npm/', docs: 'https://www.huaweicloud.com/zhishi/npm.html' },
  sjtug: { label: '上海交通大学 SJTUG', url: 'https://mirrors.sjtug.sjtu.edu.cn/npm-registry', docs: 'https://mirrors.sjtug.sjtu.edu.cn/docs/npm-registry' },
  tencent: { label: '腾讯云', url: 'https://mirrors.cloud.tencent.com/npm/', docs: 'https://cloud.tencent.cn/document/product/213/8623' },
  official: { label: 'npm 官方源', url: 'https://registry.npmjs.org', docs: 'https://docs.npmjs.com/cli/using-npm/registry' },
});

export const CARGO_MIRRORS = Object.freeze({
  rsproxy: { label: 'RsProxy（推荐）', url: 'sparse+https://rsproxy.cn/index/', docs: 'https://rsproxy.cn/' },
  tuna: { label: '清华大学 TUNA（索引镜像，包从官方源下载）', url: 'sparse+https://mirrors.tuna.tsinghua.edu.cn/crates.io-index/', docs: 'https://mirrors.tuna.tsinghua.edu.cn/help/crates.io-index/' },
  ustc: { label: '中国科学技术大学 USTC（中科大）', url: 'sparse+https://mirrors.ustc.edu.cn/crates.io-index/', docs: 'https://mirrors.ustc.edu.cn/help/crates.io-index.html' },
  sjtug: { label: '上海交通大学 SJTUG', url: 'sparse+https://mirrors.sjtug.sjtu.edu.cn/crates.io-index/', docs: 'https://mirrors.sjtug.sjtu.edu.cn/crates.io-index/config.json' },
  nju: { label: '南京大学 NJU', url: 'sparse+https://mirrors.nju.edu.cn/crates.io-index/', docs: 'https://mirrors.nju.edu.cn/crates.io-index/config.json' },
  bfsu: { label: '北京外国语大学 BFSU（索引镜像，包从官方源下载）', url: 'sparse+https://mirrors.bfsu.edu.cn/crates.io-index/', docs: 'https://mirrors.bfsu.edu.cn/help/crates.io-index/' },
  official: { label: 'Cargo 官方源', url: 'sparse+https://index.crates.io/', docs: 'https://doc.rust-lang.org/cargo/reference/registries.html' },
});

export function mirrorSettings({ mirror = 'official', npmMirror, cargoMirror } = {}) {
  if (!['cn', 'official'].includes(mirror)) throw new Error('--mirror 仅支持：cn | official');
  const npmId = npmMirror ?? (mirror === 'cn' ? 'npmmirror' : 'official');
  const cargoId = cargoMirror ?? (mirror === 'cn' ? 'rsproxy' : 'official');
  if (!Object.hasOwn(NPM_MIRRORS, npmId)) throw new Error(`--npm-mirror 仅支持：${Object.keys(NPM_MIRRORS).join(' | ')}`);
  if (!Object.hasOwn(CARGO_MIRRORS, cargoId)) throw new Error(`--cargo-mirror 仅支持：${Object.keys(CARGO_MIRRORS).join(' | ')}`);
  const cargo = CARGO_MIRRORS[cargoId];
  return {
    npmId, cargoId, npmRegistry: NPM_MIRRORS[npmId].url,
    cargoIsolated: cargoId === 'official',
    cargoConfigArgs: cargoId !== 'official' ? ['--config', 'source.crates-io.replace-with="mcp-mirror"', '--config', `source.mcp-mirror.registry=${JSON.stringify(cargo.url)}`] : [],
  };
}

export async function selectMirrors({ options, backend, suggested, menu, interactive = !options.yes && !options.offline }) {
  const mirror = options.mirror ?? suggested;
  interactive = interactive && !options.yes && !options.offline;
  const selected = { mirror, npmMirror: options.npmMirror, cargoMirror: options.cargoMirror };
  const defaults = mirrorSettings(selected);
  if (interactive) {
    for (const [key, catalog, label] of [['npmMirror', NPM_MIRRORS, 'npm'], ['cargoMirror', CARGO_MIRRORS, 'Cargo']]) {
      if (selected[key]) continue;
      const ids = Object.keys(catalog);
      const note = key === 'cargoMirror' && backend !== 'tgrep' ? '；CodeGraph 本次不使用' : '';
      const index = await menu.singleSelect(`选择 ${label} 下载来源（官方源 / 国内镜像${note}）：`,
        ids.map(id => `${id} — ${catalog[id].label} — ${catalog[id].url}`),
        { defaultIndex: ids.indexOf(key === 'npmMirror' ? defaults.npmId : defaults.cargoId) });
      selected[key] = ids[index];
    }
  }
  return mirrorSettings(selected);
}

export function mirrorSummary(settings, backend) {
  return `npm：${settings.npmId} — ${NPM_MIRRORS[settings.npmId].label} — ${settings.npmRegistry}；`
    + `Cargo：${settings.cargoId} — ${CARGO_MIRRORS[settings.cargoId].label} — ${CARGO_MIRRORS[settings.cargoId].url}`
    + (backend === 'codegraph' ? '（本次不使用 Cargo）' : '');
}
