// @ts-check

/** @type {import('@docusaurus/plugin-content-docs').SidebarsConfig} */
const sidebars = {
  docs: [
    'intro',
    {
      type: 'category',
      label: '快速开始',
      items: [
        'getting-started/prerequisites',
        'getting-started/build',
        'getting-started/run-deploy',
      ],
    },
    {
      type: 'category',
      label: '架构设计',
      items: [
        'architecture/overview',
        'architecture/message-flow',
        'architecture/gateway-proxy',
        'architecture/task-dispatcher',
        'architecture/rpc-codegen',
        'architecture/router',
        'architecture/data-layer',
        'architecture/configuration',
        'architecture/telemetry',
      ],
    },
    {
      type: 'category',
      label: '公共组件',
      items: [
        'components/overview',
        'components/dtmq',
        'components/distributed-transaction',
        'components/rank',
        'components/orbit',
      ],
    },
    {
      type: 'category',
      label: '服务',
      items: ['services/overview', 'services/robot'],
    },
    {
      type: 'category',
      label: '开发指南',
      items: [
        'development/add-service',
        'development/add-rpc-task',
        'development/add-db-table',
        'development/excel-config',
        'development/testing',
      ],
    },
  ],
};

export default sidebars;
