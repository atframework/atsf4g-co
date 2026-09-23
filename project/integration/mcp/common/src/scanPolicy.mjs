/** Detect Unreal source layouts without querying or changing version control. */
import { projectInfo } from './paths.mjs';

export const UNREAL_EXCLUDE_DIRS = Object.freeze([
  'Binaries', 'Intermediate', 'Saved', 'DerivedDataCache', 'LocalDerivedDataCache',
  '.mcp-data', 'Content', '.vs', '.idea', '.git', 'node_modules', '.cache', '.tgrep', '.codegraph',
]);

export function isUnrealWorkspace(root) {
  return projectInfo(root).isUnreal;
}

export function unrealCodegraphPolicy() {
  return {
    // Perforce source can be excluded by a Git-only whitelist. These includes
    // restore source; upstream's explicit excludes always take precedence.
    include: ['**/Source/**', '**/Plugins/**', '**/Config/**', '**/Script/**', '**/Scripts/**', '**/Shaders/**'],
    exclude: [...UNREAL_EXCLUDE_DIRS.map((name) => `**/${name}/**`), '**/.codegraph-*/**'],
  };
}
