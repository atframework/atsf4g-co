import readline from 'node:readline';
const input = readline.createInterface({ input: process.stdin });
input.on('line', line => {
  const request = JSON.parse(line);
  const result = request.method === 'status' ? { state: 'ready', embedding: 'loading', enable_knowledge_evolution: false, pid: process.pid } : { query: request.params.query, files: ['Source/example.cpp'] };
  process.stdout.write(JSON.stringify({ jsonrpc: '2.0', id: request.id, result }) + '\n');
});
input.on('close', () => process.exit(0));
