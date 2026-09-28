import { validateAdminToken } from '../lib/admin-auth.js';

export default function handler(request, response) {
  if (request.method !== 'POST') return response.status(405).end();

  const auth = validateAdminToken(request.headers.authorization);
  if (auth === 'unconfigured') {
    return response.status(503).json({ error: 'Admin access is not configured for this deployment' });
  }
  if (!auth) return response.status(401).json({ error: 'Invalid admin token' });
  return response.status(200).json({ authenticated: true });
}