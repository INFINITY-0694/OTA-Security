const version = document.querySelector('#version');
const releaseState = document.querySelector('#releaseState');
const form = document.querySelector('#releaseForm');
const message = document.querySelector('#formMessage');

fetch('/version')
  .then((response) => response.ok ? response.text() : Promise.reject(new Error('Version endpoint unavailable')))
  .then((value) => {
    version.textContent = value.trim();
    releaseState.textContent = 'Published release';
  })
  .catch((error) => {
    version.textContent = '--';
    releaseState.textContent = error.message;
  });

form.addEventListener('submit', async (event) => {
  event.preventDefault();
  message.textContent = 'Uploading firmware...';
  const data = new FormData(form);
  const token = data.get('token');
  data.delete('token');
  try {
    const response = await fetch('/api/release', { method: 'POST', headers: { Authorization: `Bearer ${token}` }, body: data });
    const result = await response.json();
    if (!response.ok) throw new Error(result.error || 'Release failed');
    message.textContent = `Published ${result.version} (${result.size} bytes).`;
    form.reset();
    version.textContent = result.version;
    releaseState.textContent = 'Published release';
  } catch (error) {
    message.textContent = error.message;
  }
});
