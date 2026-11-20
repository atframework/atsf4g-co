const {createHash, randomUUID} = require('node:crypto');
const fs = require('node:fs/promises');
const path = require('node:path');
const {render} = require('./render.cjs');

const site = path.resolve(__dirname, '../..');
const assets = path.join(site, '.docusaurus', 'diagrams');
const docs = path.join(site, 'docs');
const translations = path.join(site, 'i18n');
const formats = [
  ['.mermaid', 'mermaid'], ['.mmd', 'mermaid'], ['.d2', 'd2'],
  ['.puml', 'plantuml'], ['.plantuml', 'plantuml'], ['.echarts.json', 'echarts'],
  ['.chart.json', 'chartjs'],
];
const languages = new Set(formats.map(([, kind]) => kind));
const pendingAssets = new Map();

function within(root, filename) {
  const relative = path.relative(root, filename);
  return relative !== '..' && !relative.startsWith('..' + path.sep) && !path.isAbsolute(relative);
}

function contentRoot(filename) {
  if (within(docs, filename)) return docs;
  if (!within(translations, filename)) return undefined;
  const [locale, plugin, version] = path.relative(translations, filename).split(path.sep);
  if (plugin === 'docusaurus-plugin-content-docs' && version === 'current') {
    return path.join(translations, locale, plugin, version);
  }
}

function readAttribute(attribute) {
  if (typeof attribute.value === 'string') return attribute.value;
  const value = attribute.value?.data?.estree?.body?.[0]?.expression;
  if (value?.type === 'Literal' && typeof value.value === 'string') return value.value;
  if (value?.type === 'TemplateLiteral' && value.expressions.length === 0) return value.quasis[0].value.cooked;
  throw new Error('Diagram attributes must be static strings');
}

function normalizeKind(kind) {
  return kind === 'chart' ? 'chartjs' : kind;
}

function requestFor(node) {
  if (node.type === 'code' && languages.has(normalizeKind(node.lang))) {
    const options = Object.fromEntries(Array.from(
      (node.meta ?? '').matchAll(/\b(alt|extensions)=(?:"([^"]*)"|'([^']*)'|([^\s}]+))/g),
      ([, name, doubleQuoted, singleQuoted, bare]) => [name, doubleQuoted ?? singleQuoted ?? bare],
    ));
    return {...options, kind: normalizeKind(node.lang), source: node.value};
  }
  if (node.type === 'image') {
    const match = formats.find(([extension]) => node.url.toLowerCase().endsWith(extension));
    if (match) return {kind: match[1], src: node.url, alt: node.alt, title: node.title};
  }
  if (node.name === 'Diagram' && node.type.startsWith('mdxJsx')) {
    const props = {};
    for (const attribute of node.attributes) {
      if (!['type', 'src', 'source', 'alt', 'extensions'].includes(attribute.name) || attribute.name in props) {
        throw new Error('Use type, src or source, alt, and extensions attributes on Diagram');
      }
      props[attribute.name] = readAttribute(attribute);
    }
    const kind = normalizeKind(props.type);
    if (!languages.has(kind)) throw new Error('Unknown Diagram type: ' + props.type);
    if ((props.src === undefined) === (props.source === undefined) || node.children.length) {
      throw new Error('Provide exactly one of src or source on Diagram');
    }
    return {...props, kind};
  }
}

async function resolveSource(request, filename, root) {
  if (request.src === undefined) return {source: request.source, cwd: path.dirname(filename)};
  const reference = decodeURIComponent(request.src);
  if (!reference || /^[/\\]|:|[?#]/.test(reference)) {
    throw new Error('Use a relative local diagram path: ' + request.src);
  }
  const candidate = path.resolve(path.dirname(filename), reference);
  if (!within(root, candidate)) throw new Error('Diagram source must be inside ' + path.relative(site, root));
  const actual = await fs.realpath(candidate);
  if (!within(await fs.realpath(root), actual)) throw new Error('Diagram source resolves outside its docs directory');
  const source = (await fs.readFile(actual, 'utf8')).replace(/^\uFEFF/, '');
  return {source, cwd: path.dirname(actual)};
}

function element(name, attributes, children = [], type = 'mdxJsxTextElement') {
  return {type, name, children, attributes: Object.entries(attributes).map(([key, value]) =>
    ({type: 'mdxJsxAttribute', name: key, value}))};
}

function htmlValue(html) {
  return {type: 'mdxJsxAttributeValueExpression', value: '', data: {estree: {
    type: 'Program', sourceType: 'module', body: [{
      type: 'ExpressionStatement', expression: {type: 'ObjectExpression', properties: [{
        type: 'Property', kind: 'init', method: false, shorthand: false, computed: false,
        key: {type: 'Identifier', name: '__html'}, value: {type: 'Literal', value: html},
      }]},
    }],
  }}};
}

async function writeAsset(format, contents) {
  const filename = path.join(assets, createHash('sha256').update(contents).digest('hex') + '.' + format);
  const previous = pendingAssets.get(filename);
  if (previous) return previous;
  const write = (async () => {
    await fs.mkdir(assets, {recursive: true});
    const temporary = filename + '.' + process.pid + '.' + randomUUID() + '.tmp';
    await fs.writeFile(temporary, contents);
    try {
      await fs.rename(temporary, filename);
    } finally {
      await fs.rm(temporary, {force: true});
    }
    return filename;
  })();
  pendingAssets.set(filename, write);
  try {
    return await write;
  } finally {
    pendingAssets.delete(filename);
  }
}

async function replace(node, request, filename, root) {
  if (request.extensions !== undefined && (request.kind !== 'echarts' || request.extensions !== 'gl')) {
    throw new Error('The gl extension is available for ECharts');
  }
  const {source, cwd} = await resolveSource(request, filename, root);
  if (!source?.trim()) throw new Error('Diagram source is empty');
  const id = 'diagram-' + createHash('sha256')
    .update(path.relative(site, filename).replaceAll(path.sep, '/') + ':' + node.position.start.offset)
    .digest('hex').slice(0, 20);
  const output = await render({...request, source, cwd, root, id});
  const alt = request.alt || request.kind;
  const flow = node.type === 'mdxJsxFlowElement' || node.type === 'code';
  const type = flow ? 'mdxJsxFlowElement' : 'mdxJsxTextElement';
  if (output.svg) {
    const inline = element('span', {className: 'diagram diagram-inline', role: 'group', 'aria-label': alt,
      dangerouslySetInnerHTML: htmlValue(output.svg)}, [], type);
    Object.assign(node, inline);
    return;
  }
  const children = await Promise.all(output.images.map(async ({theme, format, content}) => {
    const asset = await writeAsset(format, content);
    const url = path.relative(path.dirname(filename), asset).split(path.sep).map(encodeURIComponent).join('/');
    const image = {type: 'image', url, alt, title: request.title ?? null};
    return theme ? element('span', {className: 'diagram-' + theme}, [image]) : image;
  }));
  Object.assign(node, element('span', {className: 'diagram'}, children, type));
}

function remark() {
  return async (tree, file) => {
    const root = contentRoot(file.path);
    if (!root) return;
    const jobs = [];
    function walk(node) {
      try {
        const request = requestFor(node);
        if (request) {
          jobs.push(replace(node, request, file.path, root).catch(error => {
            file.fail(error.message, node);
          }));
          return;
        }
      } catch (error) {
        file.fail(error.message, node);
      }
      for (const child of node.children ?? []) walk(child);
    }
    walk(tree);
    await Promise.all(jobs);
  };
}

function watch() {
  return {
    name: 'build-diagrams',
    configureWebpack() {
      return {module: {rules: [{
        test: /\.mdx?$/,
        include: [docs, translations],
        enforce: 'pre',
        use: {loader: require.resolve('./watch-loader.cjs')},
      }]}};
    },
  };
}

module.exports = {remark, watch, contentRoot};
