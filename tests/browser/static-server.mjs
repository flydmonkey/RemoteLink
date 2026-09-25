import { createServer } from 'node:http';
import { readFile } from 'node:fs/promises';
import { extname, join, normalize } from 'node:path';

const root = join(process.cwd(), 'web');
createServer(async (request, response) => {
  try {
    const relative = normalize(decodeURIComponent(new URL(request.url, 'http://localhost').pathname)).replace(/^[/\\]+/, '');
    const path = join(root, relative || 'connect.html');
    const body = await readFile(path);
    const types = {'.html':'text/html; charset=utf-8','.css':'text/css; charset=utf-8','.js':'application/javascript; charset=utf-8','.png':'image/png'};
    response.writeHead(200, {'Content-Type':types[extname(path)]||'application/octet-stream','Cache-Control':'no-store'});
    response.end(body);
  } catch {
    response.writeHead(404); response.end('Not found');
  }
}).listen(4173, '127.0.0.1');
