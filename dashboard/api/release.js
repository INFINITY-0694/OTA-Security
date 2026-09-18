import { put } from '@vercel/blob';
import formidable from 'formidable';
import { readFile } from 'node:fs/promises';

export const config = { api: { bodyParser: false } };

function parseForm(request) {
  return new Promise((resolve, reject) => {
    const form = formidable({ multiples: false });
    form.parse(request, (error, fields, files) => {
      if (error) reject(error);
      else resolve({ fields, files });
    });
  });
}

export default async function handler(request, response) {
  if (request.method !== 'POST') return response.status(405).end();
  if (request.headers.authorization !== `Bearer ${process.env.OTA_ADMIN_TOKEN}`) {
    return response.status(401).json({ error: 'Unauthorized' });
  }
  try {
    const { fields, files } = await parseForm(request);
    const version = Array.isArray(fields.version) ? fields.version[0] : fields.version;
    const firmware = Array.isArray(files.firmware) ? files.firmware[0] : files.firmware;
    if (!/^\\d+\\.\\d+\\.\\d+$/.test(version || '') || !firmware?.filepath) {
      return response.status(400).json({ error: 'Version and firmware binary are required' });
    }
    const bytes = await readFile(firmware.filepath);
    const uploaded = await put(`releases/${version}.bin`, bytes, { access: 'public', addRandomSuffix: false, contentType: 'application/octet-stream' });
    await put('release.json', JSON.stringify({ version, size: bytes.length, firmwareUrl: uploaded.url }), { access: 'public', addRandomSuffix: false, contentType: 'application/json' });
    return response.status(200).json({ version, size: bytes.length });
  } catch (error) {
    return response.status(500).json({ error: 'Release upload failed' });
  }
}
