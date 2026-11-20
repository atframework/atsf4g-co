const {contentRoot} = require('./index.cjs');

// Renderers may read transitive includes and image files. Watch the locale's
// source tree so changes to those files invalidate the compiled Markdown.
module.exports = function watchDiagramSources(source) {
  if (/\b(?:mermaid|mmd|d2|plantuml|puml|echarts|chartjs|chart|diagram)\b/i.test(source)) {
    const root = contentRoot(this.resourcePath);
    if (root) this.addContextDependency(root);
  }
  return source;
};
