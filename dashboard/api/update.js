import { list } from '@vercel/blob';

export default async function handler(request, response) {
  if (request.method !== 'GET') return response.status(405).end();
  try {
    const result = await list({ prefix: 'release.json' });
    const manifest = result.blobs[0];
    if (!manifest) return response.status(404).send('No release published');
    const release = await fetch(manifest.url).then((item) => item.json());
    const firmware = await fetch(release.firmwareUrl);
    if (!firmware.ok) return response.status(404).send('Firmware unavailable');
    response.setHeader('Cache-Control', 'no-store');
    response.setHeader('Content-Type', 'application/octet-stream');
    response.setHeader('Content-Length', String(release.size));
    return response.status(200).send(Buffer.from(await firmware.arrayBuffer()));
  } catch (error) {
    return response.status(500).send('Firmware service unavailable');
  }
}
