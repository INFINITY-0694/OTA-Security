import { put } from '@vercel/blob';
import formidable from 'formidable';
import { createHash } from 'node:crypto';
import { readFile } from 'node:fs/promises';
import { validateAdminToken } from '../lib/admin-auth.js';

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
  const auth = validateAdminToken(request.headers.authorization);
  if (auth === 'unconfigured') {
    return response.status(503).json({ error: 'Admin access is not configured for this deployment' });
  }
  if (!auth) {
    return response.status(401).json({ error: 'Unauthorized' });
  }
  if (!process.env.BLOB_READ_WRITE_TOKEN) {
    return response.status(503).json({ error: 'Blob storage is not configured for this deployment' });
  }
  try {
    const { fields, files } = await parseForm(request);
    const version = Array.isArray(fields.version) ? fields.version[0] : fields.version;
    const firmware = Array.isArray(files.firmware) ? files.firmware[0] : files.firmware;
    const releaseFile = Array.isArray(files.release) ? files.release[0] : files.release;
    if (!/^\d+\.\d+\.\d+$/.test(version || '') || !firmware?.filepath || !releaseFile?.filepath) {
      return response.status(400).json({ error: 'Version, firmware, and signed release manifest are required' });
    }
    const bytes = await readFile(firmware.filepath);
    const encrypted = Array.isArray(files.releaseBinary) ? files.releaseBinary[0] : files.releaseBinary;
    if (!encrypted?.filepath) {
      return response.status(400).json({ error: 'Encrypted release binary is required' });
    }
    const encryptedBytes = await readFile(encrypted.filepath);
    const release = JSON.parse(await readFile(releaseFile.filepath, 'utf8'));
    const digest = createHash('sha256').update(bytes).digest('hex');
    let chunkOffset = 0;
    let encryptedTotal = 0;
    const validChunks = release.chunk_size === 32 * 1024 && Array.isArray(release.chunks) &&
      release.chunks.length > 0 && release.chunks.every((chunk, index) => {
        const valid = chunk.index === index && chunk.offset === chunkOffset &&
          Number.isSafeInteger(chunk.size) && chunk.size > 0 && chunk.size <= release.chunk_size &&
          (index === release.chunks.length - 1 || chunk.size === release.chunk_size) &&
          chunk.encrypted_size === chunk.size + 16 && /^[\da-f]{64}$/i.test(chunk.sha256 || '') &&
          typeof chunk.nonce === 'string' && /^[A-Za-z0-9+/]{16}$/.test(chunk.nonce) &&
          typeof chunk.tag === 'string' && /^[A-Za-z0-9+/]{22}==$/.test(chunk.tag) &&
          Buffer.from(chunk.nonce, 'base64').length === 12 && Buffer.from(chunk.tag, 'base64').length === 16 &&
          typeof chunk.signature === 'string' && /^[A-Za-z0-9+/]+={0,2}$/.test(chunk.signature);
        if (valid) {
          chunkOffset += chunk.size;
          encryptedTotal += chunk.encrypted_size;
        }
        return valid;
      });
    if (release.version !== version || release.size !== bytes.length || release.encrypted_size !== encryptedBytes.length ||
        release.sha256 !== digest || !validChunks || chunkOffset !== bytes.length ||
        encryptedTotal !== encryptedBytes.length) {
      return response.status(400).json({ error: 'Signed manifest does not match the firmware upload' });
    }
    const uploaded = await put(`releases/${version}.bin`, encryptedBytes, { access: 'public', addRandomSuffix: false, contentType: 'application/octet-stream' });
    await put('release.json', JSON.stringify({ ...release, firmwareUrl: uploaded.url }), { access: 'public', addRandomSuffix: false, contentType: 'application/json' });
    return response.status(200).json({ version, size: bytes.length });
  } catch (error) {
    return response.status(500).json({ error: 'Release upload failed' });
  }
}
