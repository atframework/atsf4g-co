// @ts-check
// atsf4g-co documentation site (Docusaurus 3, docs-only mode, zh-CN + en i18n).
//
// Fonts: English faces are imported from a China-reachable CDN (jsDelivr Fontsource mirrors npm packages;
// Google Fonts hosts are intentionally NOT used). Only the highest-priority English face of each stack is
// imported; Chinese faces rely on locally installed fonts and the fallback chain in src/css/custom.css.

// @ts-ignore
const {themes: prismThemes} = require('prism-react-renderer');

/** @type {import('@docusaurus/types').Config} */
const config = {
  title: 'atsf4g-co',
  tagline: 'AT Service Framework for Game - Coroutine',
  favicon: 'img/logo.svg',

  url: 'https://atframework.github.io',
  baseUrl: '/atsf4g-co/',

  organizationName: 'atframework',
  projectName: 'atsf4g-co',

  onBrokenLinks: 'warn',

  markdown: {
    mermaid: true,
    hooks: {
      onBrokenMarkdownLinks: 'warn',
    },
  },
  themes: ['@docusaurus/theme-mermaid'],

  headTags: [
    {
      tagName: 'link',
      attributes: {
        rel: 'preconnect',
        href: 'https://cdn.jsdelivr.net',
        crossorigin: 'anonymous',
      },
    },
    // Body English face: Noto Sans (regular + bold).
    {
      tagName: 'link',
      attributes: {
        rel: 'stylesheet',
        href: 'https://cdn.jsdelivr.net/npm/@fontsource/noto-sans@5/index.css',
      },
    },
    {
      tagName: 'link',
      attributes: {
        rel: 'stylesheet',
        href: 'https://cdn.jsdelivr.net/npm/@fontsource/noto-sans@5/700.css',
      },
    },
    // Code English face: Noto Sans Mono (regular + bold).
    {
      tagName: 'link',
      attributes: {
        rel: 'stylesheet',
        href: 'https://cdn.jsdelivr.net/npm/@fontsource/noto-sans-mono@5/index.css',
      },
    },
    {
      tagName: 'link',
      attributes: {
        rel: 'stylesheet',
        href: 'https://cdn.jsdelivr.net/npm/@fontsource/noto-sans-mono@5/700.css',
      },
    },
  ],

  i18n: {
    defaultLocale: 'zh-CN',
    locales: ['zh-CN', 'en'],
    localeConfigs: {
      'zh-CN': {
        label: '简体中文',
        direction: 'ltr',
        htmlLang: 'zh-CN',
      },
      en: {
        label: 'English',
        direction: 'ltr',
        htmlLang: 'en-US',
      },
    },
  },

  presets: [
    [
      'classic',
      /** @type {import('@docusaurus/preset-classic').Options} */
      ({
        docs: {
          routeBasePath: '/',
          sidebarPath: './sidebars.js',
          editUrl: 'https://github.com/atframework/atsf4g-co/edit/main/doc/',
        },
        blog: false,
        theme: {
          customCss: './src/css/custom.css',
        },
      }),
    ],
  ],

  themeConfig:
    /** @type {import('@docusaurus/preset-classic').ThemeConfig} */
    ({
      navbar: {
        title: 'atsf4g-co',
        logo: {
          alt: 'atsf4g-co Logo',
          src: 'img/logo.svg',
        },
        items: [
          {
            type: 'docSidebar',
            sidebarId: 'docs',
            position: 'left',
            label: '文档',
          },
          {
            href: 'https://github.com/atframework/atsf4g-co',
            label: 'GitHub',
            position: 'right',
          },
          {
            type: 'localeDropdown',
            position: 'right',
          },
        ],
      },
      footer: {
        style: 'dark',
        links: [
          {
            title: '文档',
            items: [
              {label: '快速开始', to: '/getting-started/prerequisites'},
              {label: '架构设计', to: '/architecture/overview'},
              {label: '开发指南', to: '/development/add-service'},
            ],
          },
          {
            title: '社区',
            items: [
              {
                label: 'GitHub',
                href: 'https://github.com/atframework/atsf4g-co',
              },
              {
                label: 'Issues',
                href: 'https://github.com/atframework/atsf4g-co/issues',
              },
            ],
          },
        ],
        copyright: `Copyright © ${new Date().getFullYear()} atframework. Built with Docusaurus.`,
      },
      prism: {
        theme: prismThemes.github,
        darkTheme: prismThemes.dracula,
        additionalLanguages: ['bash', 'cpp', 'protobuf', 'yaml', 'json', 'powershell'],
      },
    }),
};

export default config;
