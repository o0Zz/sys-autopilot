#!/usr/bin/env python3
"""Browse, upload, download and live-edit the console's SD card in a browser.

Serves a one-page explorer on localhost and proxies its calls to the
sys-autopilot /files API, so the page is same-origin (no CORS) and the
credentials never reach the browser.

    python tools/explorer.py switch-5322.local
    python tools/explorer.py 192.168.1.42:4150 --token abc123
"""

import argparse
import base64
import http.server
import threading
import urllib.error
import urllib.parse
import urllib.request
import webbrowser

PAGE = r"""<!doctype html>
<meta charset="utf-8">
<title>sys-autopilot files</title>
<style>
  :root { color-scheme: dark }
  * { box-sizing: border-box }
  body { margin: 0; height: 100vh; display: flex; flex-direction: column;
         font: 13px ui-monospace, Consolas, monospace; background: #15171a; color: #d7dae0 }
  header, #bar { display: flex; gap: 8px; align-items: center; padding: 6px 10px;
                 background: #1d2025; border-bottom: 1px solid #2c3038 }
  .grow { flex: 1 }
  button, .btn, a { font: inherit; color: #d7dae0; background: #2a2e36; border: 1px solid #3a3f49;
                    border-radius: 4px; padding: 3px 9px; cursor: pointer; text-decoration: none }
  button:disabled { opacity: .4; cursor: default }
  #crumbs a { background: none; border: none; padding: 0; color: #78a9ff }
  main { flex: 1; display: flex; min-height: 0 }
  #list { width: 340px; margin: 0; padding: 0; overflow: auto; list-style: none;
          border-right: 1px solid #2c3038 }
  #list li { display: flex; gap: 8px; padding: 3px 10px; cursor: pointer; white-space: nowrap }
  #list li:hover { background: #22262d }
  #list li.sel { background: #2d3644 }
  #list .size { margin-left: auto; color: #6d737e }
  .dir { color: #78a9ff }
  #pane { flex: 1; display: flex; flex-direction: column; min-width: 0 }
  #ed { flex: 1; resize: none; border: 0; outline: 0; padding: 10px; font: inherit;
        background: #15171a; color: #d7dae0; white-space: pre; overflow: auto }
  #name { overflow: hidden; text-overflow: ellipsis }
  #msg { color: #6d737e }
</style>
<header>
  <span id="crumbs"></span>
  <span class="grow"></span>
  <span id="msg"></span>
  <label class="btn">Upload<input type="file" id="up" multiple hidden></label>
  <button id="refresh">Refresh</button>
</header>
<main>
  <ul id="list"></ul>
  <section id="pane">
    <div id="bar">
      <span id="name">no file</span>
      <span class="grow"></span>
      <label><input type="checkbox" id="live"> live</label>
      <button id="save" disabled>Save</button>
      <a id="dl" download>Download</a>
      <button id="del" disabled>Delete</button>
    </div>
    <textarea id="ed" spellcheck="false"></textarea>
  </section>
</main>
<script>
const $ = id => document.getElementById(id);
const api = (p, o) => fetch('/files?path=' + encodeURIComponent(p), o);
const human = n => n < 1024 ? n + ' B' : n < 1048576 ? (n / 1024).toFixed(1) + ' K'
                 : (n / 1048576).toFixed(1) + ' M';
let dir = '/', file = null, dirty = false;

function say(text, keep) {
  $('msg').textContent = text;
  if (!keep) setTimeout(() => { if ($('msg').textContent === text) $('msg').textContent = ''; }, 3000);
}

async function call(path, opts, what) {
  const r = await api(path, opts);
  if (!r.ok) { say(what + ' failed: ' + (await r.text()).slice(0, 120), true); throw new Error(r.status); }
  return r;
}

async function loadDir(path) {
  dir = path.endsWith('/') ? path : path + '/';
  const data = await (await call(dir, {}, 'list')).json();

  $('crumbs').innerHTML = '';
  let at = '/';
  const crumbs = [['sdmc:', '/']].concat(dir.split('/').filter(Boolean).map(s => {
    at += s + '/';
    return [s, at];
  }));
  crumbs.forEach(([label, target], i) => {
    if (i) $('crumbs').append(' / ');
    const a = document.createElement('a');
    a.href = '#';
    a.textContent = label;
    a.onclick = e => { e.preventDefault(); loadDir(target); };
    $('crumbs').append(a);
  });

  const entries = data.entries.sort((a, b) =>
    (a.type === b.type ? 0 : a.type === 'dir' ? -1 : 1) || a.name.localeCompare(b.name));
  if (dir !== '/') entries.unshift({ name: '..', type: 'dir' });

  $('list').innerHTML = '';
  for (const e of entries) {
    const target = e.name === '..' ? dir.replace(/[^/]+\/$/, '') : dir + e.name;
    const li = document.createElement('li');
    const label = document.createElement('span');
    const size = document.createElement('span');
    label.className = e.type === 'dir' ? 'dir' : '';
    label.textContent = e.type === 'dir' ? e.name + '/' : e.name;
    size.className = 'size';
    size.textContent = e.type === 'dir' ? '' : human(e.size);
    li.append(label, size);
    li.onclick = () => e.type === 'dir' ? loadDir(target) : openFile(target, li);
    $('list').append(li);
  }
}

async function openFile(path, li) {
  if (dirty && !confirm('Discard unsaved changes?')) return;
  [...$('list').children].forEach(n => n.classList.toggle('sel', n === li));
  file = path;
  dirty = false;
  $('name').textContent = path;
  $('dl').href = '/files?path=' + encodeURIComponent(path);
  $('dl').download = path.split('/').pop();
  $('del').disabled = false;
  await reload();
}

async function reload() {
  const buf = await (await call(file, {}, 'read')).arrayBuffer();
  const text = new TextDecoder().decode(buf);
  const binary = /[\u0000-\u0008\u000e-\u001f\ufffd]/.test(text.slice(0, 8192));
  const ed = $('ed');
  const atEnd = ed.scrollTop + ed.clientHeight >= ed.scrollHeight - 4;
  const top = ed.scrollTop;
  ed.value = binary ? '(binary, ' + human(buf.byteLength) + ' - use Download)' : text;
  ed.readOnly = binary;
  $('save').disabled = true;
  ed.scrollTop = atEnd ? ed.scrollHeight : top;
}

$('ed').oninput = () => { dirty = true; $('save').disabled = false; };

$('save').onclick = async () => {
  await call(file, { method: 'PUT', body: new Blob([$('ed').value]) }, 'save');
  dirty = false;
  $('save').disabled = true;
  say('saved ' + file);
  loadDir(dir);
};

$('del').onclick = async () => {
  if (!confirm('Delete ' + file + ' ?')) return;
  await call(file, { method: 'DELETE' }, 'delete');
  file = null;
  dirty = false;
  $('ed').value = '';
  $('name').textContent = 'no file';
  $('del').disabled = true;
  loadDir(dir);
};

$('up').onchange = async e => {
  for (const f of e.target.files) {
    say('uploading ' + f.name, true);
    await call(dir + f.name, { method: 'PUT', body: f }, 'upload');
  }
  e.target.value = '';
  say('uploaded');
  loadDir(dir);
};

$('refresh').onclick = () => { loadDir(dir); if (file) reload(); };

document.ondragover = e => e.preventDefault();
document.ondrop = e => {
  e.preventDefault();
  $('up').files = e.dataTransfer.files;
  $('up').dispatchEvent(new Event('change'));
};

setInterval(() => { if ($('live').checked && file && !dirty) reload(); }, 2000);
loadDir('/');
</script>
"""


class Handler(http.server.BaseHTTPRequestHandler):
    console = ""
    auth = {}
    # The console's HTTP server is single-threaded: overlapping requests time
    # out, so the browser's parallel fetches are funnelled through one at a time.
    lock = threading.Lock()

    def do_GET(self):
        if self.path == "/":
            return self._send(200, "text/html; charset=utf-8", PAGE.encode())
        if self.path == "/favicon.ico":
            return self._send(204, "text/plain", b"")
        self._proxy()

    do_PUT = do_DELETE = do_GET

    def _proxy(self):
        length = int(self.headers.get("Content-Length") or 0)
        body = self.rfile.read(length) if length else None
        headers = dict(self.auth)
        if body is not None:
            headers["Content-Type"] = "application/octet-stream"
        req = urllib.request.Request(self.console + self.path, data=body,
                                     method=self.command, headers=headers)
        try:
            with self.lock, urllib.request.urlopen(req, timeout=120) as r:
                self._send(r.status, r.headers.get("Content-Type", "application/octet-stream"),
                           r.read())
        except urllib.error.HTTPError as e:
            self._send(e.code, "text/plain", e.read() or e.reason.encode())
        except Exception as e:
            self._send(502, "text/plain", str(e).encode())

    def _send(self, status, ctype, body):
        self.send_response(status)
        self.send_header("Content-Type", ctype)
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)

    def log_message(self, *args):
        pass


def main():
    p = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    p.add_argument("console", help="host, host:port or URL of the console")
    p.add_argument("--token", help="bearer token, when the console requires auth")
    p.add_argument("--user")
    p.add_argument("--password")
    p.add_argument("--port", type=int, default=8150, help="local port (default 8150)")
    p.add_argument("--no-browser", action="store_true")
    args = p.parse_args()

    url = args.console if "://" in args.console else "http://" + args.console
    if urllib.parse.urlsplit(url).port is None:
        url += ":4150"
    Handler.console = url.rstrip("/")
    if args.token:
        Handler.auth = {"Authorization": "Bearer " + args.token}
    elif args.user or args.password:
        raw = ("%s:%s" % (args.user or "", args.password or "")).encode()
        Handler.auth = {"Authorization": "Basic " + base64.b64encode(raw).decode()}

    local = "http://127.0.0.1:%d/" % args.port
    print("%s  ->  %s" % (local, Handler.console))
    if not args.no_browser:
        webbrowser.open(local)
    http.server.ThreadingHTTPServer(("127.0.0.1", args.port), Handler).serve_forever()


if __name__ == "__main__":
    main()
