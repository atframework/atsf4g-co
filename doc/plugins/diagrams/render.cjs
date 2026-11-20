const {execFile} = require('node:child_process');
const path = require('node:path');
const fs = require('node:fs/promises');
const browser = require('./browser.cjs');

let xml;

function svgDocument(source) {
  if (!xml) {
    const {JSDOM} = require('jsdom');
    const window = new JSDOM('').window;
    xml = {window, purifier: require('dompurify')(window)};
  }
  const document = new xml.window.DOMParser().parseFromString(source, 'image/svg+xml');
  const root = document.documentElement;
  if (root.localName !== 'svg' || root.namespaceURI !== 'http://www.w3.org/2000/svg' ||
      document.querySelector('parsererror')) {
    throw new Error('The renderer did not return valid SVG XML');
  }
  return root;
}

function inlineSvg(source, idPrefix) {
  const root = svgDocument(source);
  if (idPrefix) {
    const ids = new Map();
    for (const element of root.querySelectorAll('[id]')) {
      const old = element.id;
      element.id = idPrefix + '-' + old;
      ids.set(old, element.id);
    }
    for (const element of [root, ...root.querySelectorAll('*')]) {
      for (const attribute of Array.from(element.attributes)) {
        let value = attribute.value.replace(/url\(#([^)]*)\)/g, (original, id) =>
          ids.has(id) ? 'url(#' + ids.get(id) + ')' : original);
        if (attribute.localName === 'href' && value.startsWith('#') && ids.has(value.slice(1))) {
          value = '#' + ids.get(value.slice(1));
        }
        attribute.value = value;
      }
    }
  }
  return xml.purifier.sanitize(root.outerHTML, {
    USE_PROFILES: {svg: true, svgFilters: true},
    ADD_TAGS: ['foreignobject', 'div', 'span', 'p', 'br', 'pre', 'code', 'strong', 'em', 'blockquote',
      'ul', 'ol', 'li', 'a', 'table', 'thead', 'tbody', 'th', 'td', 'tr', 'h1', 'h2', 'h3', 'h4', 'h5', 'h6'],
  });
}

function command(executable, args, cwd, source) {
  return new Promise((resolve, reject) => {
    const child = execFile(executable, args, {cwd, timeout: 75000, maxBuffer: 12 * 1024 * 1024, windowsHide: true},
      (error, stdout, stderr) => {
        if (error) {
          const reason = error.code === 'ENOENT' ? 'executable not found' :
            (stderr.trim() || error.message);
          reject(new Error(path.basename(executable) + ': ' + reason));
        } else {
          resolve(stdout);
        }
      });
    child.stdin.on('error', () => {});
    child.stdin.end(source, 'utf8');
  });
}

async function plantuml(request) {
  const jar = process.env.PLANTUML_JAR;
  if (!jar) throw new Error('Set PLANTUML_JAR to the local PlantUML JAR');
  const jarPath = await fs.realpath(jar);
  for (const boundary of ['startuml', 'enduml']) {
    if (Array.from(request.source.matchAll(new RegExp('^\\s*@' + boundary + '\\b', 'gm'))).length !== 1) {
      throw new Error('PlantUML requires exactly one @startuml and @enduml pair');
    }
  }
  return command(process.env.JAVA_BINARY || 'java', [
    '-Djava.awt.headless=true',
    '-DPLANTUML_SECURITY_PROFILE=ALLOWLIST',
    '-Dplantuml.allowlist.path=' + request.root,
    '-jar', jarPath, '--svg', '--pipe', '--charset', 'UTF-8', '--stop-on-error',
    '--no-error-image', '--disable-metadata', '--theme', 'plain',
  ], request.cwd, request.source);
}

function optionFrom(source) {
  const option = JSON.parse(source);
  if (option === null || typeof option !== 'object' || Array.isArray(option)) {
    throw new Error('Chart source must be a JSON object');
  }
  return option;
}

function needsGl(option) {
  return Boolean(option.grid3D || option.geo3D || option.globe ||
    [].concat(option.series ?? []).some(series => /(?:3D|GL|^surface$)/.test(series?.type ?? '')));
}

function echartsSvg(option) {
  const echarts = require('echarts');
  const chart = echarts.init(null, null, {
    renderer: 'svg', ssr: true, width: browser.width, height: browser.height,
  });
  try {
    chart.setOption({...option, animation: false, backgroundColor: option.backgroundColor ?? '#fff',
      textStyle: {fontFamily: browser.fontFamily, ...option.textStyle}});
    const svg = chart.renderToSVGString();
    svgDocument(svg);
    return svg;
  } finally {
    chart.dispose();
  }
}

async function render(request) {
  const {kind, cwd, source, id, extensions} = request;
  if (kind === 'd2') {
    const svg = await command(process.env.D2_BINARY || 'd2', [
      '--target=', '--bundle=true', '--no-xml-tag=true', '--salt=' + id, '--timeout=60', '-', '-',
    ], cwd, source);
    return {svg: inlineSvg(svg)};
  }
  if (kind === 'plantuml') return {svg: inlineSvg(await plantuml(request), id)};
  if (kind === 'mermaid') return {images: await browser.mermaid(source, id)};
  const option = optionFrom(source);
  if (kind === 'chartjs') return {images: [{format: 'png', content: await browser.chartjs(option)}]};
  if (extensions === 'gl' || needsGl(option)) {
    return {images: [{format: 'png', content: await browser.echartsGl(option)}]};
  }
  return {images: [{format: 'svg', content: echartsSvg(option)}]};
}

module.exports = {render};
