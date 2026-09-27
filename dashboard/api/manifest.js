import { list } from '@vercel/blob';

export default async function handler(request, response) {
  if (request.method !== 'GET') return response.status(405).end();
  try {
    const result = await list({ prefix: 'release.json' });
    const manifest = result.blobs[0];
    if (!manifest) return response.status(404).send('No release published');
    const release = await fetch(manifest.url);
    if (!release.ok) return response.status(404).send('Release manifest unavailable');
    response.setHeader('Cache-Control', 'no-store');
    response.setHeader('Content-Type', 'application/json');
    return response.status(200).send(await release.text());
  } catch (error) {
    return response.status(500).send('Release service unavailable');
  }
}
