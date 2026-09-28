import { timingSafeEqual } from 'node:crypto';

export function validateAdminToken(authorization) {
  const expectedToken = process.env.OTA_ADMIN_TOKEN;
  if (!expectedToken) return 'unconfigured';

  const match = /^Bearer\s+(.+)$/i.exec(authorization || '');
  const supplied = Buffer.from(match ? match[1] : '', 'utf8');
  const expected = Buffer.from(expectedToken, 'utf8');
  if (supplied.length !== expected.length) return false;
  return timingSafeEqual(supplied, expected);
}