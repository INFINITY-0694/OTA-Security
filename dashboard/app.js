const loginScreen = document.querySelector('#loginScreen');
const loginForm = document.querySelector('#loginForm');
const loginMessage = document.querySelector('#loginMessage');
const loginButton = document.querySelector('#loginButton');
const releaseWorkspace = document.querySelector('#releaseWorkspace');
const sessionStatus = document.querySelector('#sessionStatus');
const logoutButton = document.querySelector('#logoutButton');
const version = document.querySelector('#version');
const releaseState = document.querySelector('#releaseState');
const releaseForm = document.querySelector('#releaseForm');
const formMessage = document.querySelector('#formMessage');
const publishButton = document.querySelector('#publishButton');

let adminToken = '';

async function readResponse(response) {
  const contentType = response.headers.get('content-type') || '';
  if (contentType.includes('application/json')) return response.json();
  return { error: await response.text() };
}

function setSignedIn(token) {
  adminToken = token;
  loginScreen.hidden = true;
  releaseWorkspace.hidden = false;
  sessionStatus.classList.add('signed-in');
  sessionStatus.innerHTML = '<i></i> Admin session';
  loginForm.reset();
  formMessage.textContent = '';
  loadCurrentVersion();
}

function signOut() {
  adminToken = '';
  releaseWorkspace.hidden = true;
  loginScreen.hidden = false;
  sessionStatus.classList.remove('signed-in');
  sessionStatus.innerHTML = '<i></i> Locked';
  releaseForm.reset();
  loginMessage.textContent = 'Signed out.';
}

async function loadCurrentVersion() {
  version.textContent = 'Checking...';
  releaseState.textContent = 'Connecting to release service';
  try {
    const response = await fetch('/version', { cache: 'no-store' });
    if (!response.ok) throw new Error('No release is currently available');
    version.textContent = (await response.text()).trim();
    releaseState.textContent = 'Production release';
  } catch (error) {
    version.textContent = '--';
    releaseState.textContent = error.message;
  }
}

loginForm.addEventListener('submit', async (event) => {
  event.preventDefault();
  const token = new FormData(loginForm).get('token').trim();
  loginButton.disabled = true;
  loginMessage.textContent = 'Checking access...';

  try {
    const response = await fetch('/api/auth', {
      method: 'POST',
      headers: { Authorization: `Bearer ${token}` },
    });
    const result = await readResponse(response);
    if (!response.ok) throw new Error(result.error || 'Sign-in failed');
    setSignedIn(token);
  } catch (error) {
    loginMessage.textContent = error.message;
  } finally {
    loginButton.disabled = false;
  }
});

logoutButton.addEventListener('click', signOut);

releaseForm.addEventListener('submit', async (event) => {
  event.preventDefault();
  if (!adminToken) {
    signOut();
    return;
  }

  publishButton.disabled = true;
  formMessage.textContent = 'Checking files and publishing release...';
  const data = new FormData(releaseForm);

  try {
    const response = await fetch('/api/release', {
      method: 'POST',
      headers: { Authorization: `Bearer ${adminToken}` },
      body: data,
    });
    const result = await readResponse(response);
    if (response.status === 401) {
      signOut();
      loginMessage.textContent = 'Session expired. Sign in again.';
      return;
    }
    if (!response.ok) throw new Error(result.error || 'Release upload failed');

    formMessage.textContent = `Published ${result.version} (${result.size.toLocaleString()} bytes).`;
    releaseForm.reset();
    await loadCurrentVersion();
  } catch (error) {
    formMessage.textContent = error.message;
  } finally {
    publishButton.disabled = false;
  }
});
