const path = require('node:path');
const {chromium} = require('playwright-core');

const fontFamily = 'Noto Sans CJK SC, Microsoft YaHei, Arial, sans-serif';
const width = 800;
const height = 480;
let browser;
let page;
let queue = Promise.resolve();
let idle;
const loaded = new Set();

async function getPage() {
  if (page) return page;
  try {
    browser = await chromium.launch({
      executablePath: process.env.PLAYWRIGHT_CHROMIUM_EXECUTABLE_PATH || undefined,
      headless: true,
      args: ['--use-gl=angle', '--use-angle=swiftshader', '--enable-unsafe-swiftshader'],
    });
    page = await browser.newPage({viewport: {width, height}});
    await page.route('**/*', route => route.abort());
    await page.setContent('<style>body{margin:0}canvas{display:block}</style><div id="diagram"></div>');
    return page;
  } catch (error) {
    await closeBrowser();
    throw new Error('Cannot start Chromium for diagrams. Run pnpm diagrams:install-browser. ' + error.message);
  }
}

async function closeBrowser() {
  const current = browser;
  browser = undefined;
  page = undefined;
  loaded.clear();
  await current?.close();
}

function inBrowser(task) {
  clearTimeout(idle);
  const job = queue.then(async () => task(await getPage()));
  const settled = job.then(() => {}, () => {});
  queue = settled;
  void settled.then(() => {
    if (queue === settled) {
      idle = setTimeout(() => {
        queue = closeBrowser().catch(error => console.warn('Closing diagram Chromium: ' + error.message));
      }, 1000);
    }
  });
  return job;
}

async function loadScript(page, name, filename) {
  if (loaded.has(name)) return;
  const directory = path.dirname(require.resolve(name + '/package.json'));
  await page.addScriptTag({path: path.join(directory, 'dist', filename)});
  loaded.add(name);
}

function mermaid(source, id) {
  return inBrowser(async page => {
    await loadScript(page, 'mermaid', 'mermaid.min.js');
    const images = [];
    for (const theme of ['light', 'dark']) {
      const svg = await page.evaluate(async ({source, id, theme, fontFamily}) => {
        const diagramId = id + '-' + theme;
        await document.fonts.ready;
        window.mermaid.initialize({
          startOnLoad: false,
          securityLevel: 'strict',
          theme: theme === 'light' ? 'default' : 'dark',
          fontFamily,
          deterministicIds: true,
          deterministicIDSeed: diagramId,
          handDrawnSeed: parseInt(id.slice(-8), 16) || 1,
        });
        const {svg} = await window.mermaid.render(diagramId, source);
        // Mermaid returns HTML serialization, including HTML void elements in labels.
        const template = document.createElement('template');
        template.innerHTML = svg;
        const root = template.content.firstElementChild;
        if (root?.localName !== 'svg') throw new Error('Mermaid did not return SVG');
        const xml = new XMLSerializer().serializeToString(root);
        if (new DOMParser().parseFromString(xml, 'image/svg+xml').querySelector('parsererror')) {
          throw new Error('Mermaid output is not valid SVG XML');
        }
        return xml;
      }, {source, id, theme, fontFamily});
      images.push({theme, format: 'svg', content: svg});
    }
    return images;
  });
}

function chartjs(option) {
  return inBrowser(async page => {
    if (!loaded.has('chart.js')) {
      const entry = require.resolve('chart.js');
      await page.addScriptTag({path: path.join(path.dirname(entry), 'chart.umd.js')});
      loaded.add('chart.js');
    }
    const data = await page.evaluate(({option, fontFamily, width, height}) => {
      const container = document.querySelector('#diagram');
      const canvas = document.createElement('canvas');
      canvas.width = width;
      canvas.height = height;
      container.replaceChildren(canvas);
      window.Chart.defaults.font.family = fontFamily;
      const chart = new window.Chart(canvas, {
        ...option,
        options: {...option.options, responsive: false, animation: false, devicePixelRatio: 2},
      });
      // Give transparent charts a light background on both site themes.
      chart.ctx.save();
      chart.ctx.globalCompositeOperation = 'destination-over';
      chart.ctx.fillStyle = '#fff';
      chart.ctx.fillRect(0, 0, chart.width, chart.height);
      chart.ctx.restore();
      const result = chart.toBase64Image();
      chart.destroy();
      container.replaceChildren();
      return result;
    }, {option, fontFamily, width, height});
    if (!data.startsWith('data:image/png;base64,')) throw new Error('Chart.js did not return PNG');
    return Buffer.from(data.slice('data:image/png;base64,'.length), 'base64');
  });
}

function echartsGl(option) {
  return inBrowser(async page => {
    await loadScript(page, 'echarts', 'echarts.min.js');
    await loadScript(page, 'echarts-gl', 'echarts-gl.min.js');
    try {
      await page.evaluate(async ({option, width, height, fontFamily}) => {
        const container = document.querySelector('#diagram');
        container.style.cssText = 'width:' + width + 'px;height:' + height + 'px;background:#fff';
        const fixedView = component => ({
          ...component,
          viewControl: {distance: 235, ...component.viewControl, autoRotate: false},
        });
        for (const componentName of ['grid3D', 'geo3D', 'globe']) {
          const components = option[componentName];
          if (components) {
            option[componentName] = Array.isArray(components) ? components.map(fixedView) : fixedView(components);
          }
        }
        window.diagramChart = window.echarts.init(container);
        window.diagramChart.setOption({...option, animation: false, textStyle: {fontFamily, ...option.textStyle}});
        await new Promise(resolve => requestAnimationFrame(() => requestAnimationFrame(resolve)));
        // ECharts GL may use a single canvas when there is no separate 2D layer.
        const layers = Object.values(window.diagramChart.getZr().painter.getLayers());
        if (!layers.some(layer => layer.renderer?.gl && !layer.renderer.gl.isContextLost())) {
          throw new Error('ECharts did not render a WebGL layer');
        }
      }, {option, width, height, fontFamily});
      return await page.locator('#diagram').screenshot();
    } finally {
      await page.evaluate(() => {
        window.diagramChart?.dispose();
        delete window.diagramChart;
      });
    }
  });
}

module.exports = {fontFamily, width, height, mermaid, chartjs, echartsGl};
