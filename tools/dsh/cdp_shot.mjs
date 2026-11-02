// Capture a page through the Chrome DevTools Protocol.
//
// `--screenshot` decides for itself when a page is finished, and a page with a frame loop never is.
// Driving the protocol asks for a capture at a moment this script chooses instead, which is the
// difference between "the harness gave up" and "here is what was on the canvas".
//
// Usage: node cdp_shot.mjs <devtools-http-base> <page-url> <output.png> [wait-ms] [width] [height]

const base = process.argv[2] ?? 'http://127.0.0.1:9222';
const pageUrl = process.argv[3];
const outputPath = process.argv[4];
const waitMs = Number.parseInt(process.argv[5] ?? '20000', 10);
const width = Number.parseInt(process.argv[6] ?? '900', 10);
const height = Number.parseInt(process.argv[7] ?? '700', 10);

if (!pageUrl || !outputPath) {
    console.error('usage: node cdp_shot.mjs <devtools-http-base> <page-url> <output.png> [wait-ms]');
    process.exit(2);
}

const sleep = (ms) => new Promise((resolve) => setTimeout(resolve, ms));

/// Poll the debugging endpoint until it answers with at least one page target.
async function findPageTarget() {
    const deadline = Date.now() + 30000;
    while (Date.now() < deadline) {
        try {
            const response = await fetch(`${base}/json`);
            const targets = await response.json();
            const page = targets.find((t) => t.type === 'page' && t.webSocketDebuggerUrl);
            if (page) {
                return page;
            }
        } catch {
            // The browser is still starting.
        }
        await sleep(250);
    }
    throw new Error('no page target appeared on the debugging endpoint');
}

class Cdp {
    constructor(ws, nextId) {
        this.ws = ws;
        this.nextId = nextId;
        this.pending = new Map();
        this.events = [];
        ws.addEventListener('message', (event) => {
            const message = JSON.parse(event.data);
            if (message.id !== undefined) {
                const entry = this.pending.get(message.id);
                if (entry) {
                    this.pending.delete(message.id);
                    if (message.error) {
                        entry.reject(new Error(`${entry.method}: ${message.error.message}`));
                    } else {
                        entry.resolve(message.result);
                    }
                }
            } else if (message.method) {
                this.events.push(message);
            }
        });
    }

    send(method, params = {}) {
        const id = this.nextId++;
        return new Promise((resolve, reject) => {
            this.pending.set(id, { resolve, reject, method });
            this.ws.send(JSON.stringify({ id, method, params }));
        });
    }
}

const target = await findPageTarget();
const ws = new WebSocket(target.webSocketDebuggerUrl);
await new Promise((resolve, reject) => {
    ws.addEventListener('open', resolve, { once: true });
    ws.addEventListener('error', reject, { once: true });
});

const cdp = new Cdp(ws, 1);

await cdp.send('Page.enable');
await cdp.send('Runtime.enable');
await cdp.send('Log.enable');
await cdp.send('Emulation.setDeviceMetricsOverride', {
    width,
    height,
    deviceScaleFactor: 1,
    mobile: false,
});

const navigate = cdp.send('Page.navigate', { url: pageUrl });

// The page reports what it is doing through console.log and through uncaught errors; both are worth
// keeping, because a black canvas is explained by one of them.
ws.addEventListener('message', (event) => {
    const message = JSON.parse(event.data);
    if (message.method === 'Runtime.consoleAPICalled') {
        const text = message.params.args.map((a) => a.value ?? a.description ?? '').join(' ');
        console.log(`[console.${message.params.type}] ${text}`);
    } else if (message.method === 'Log.entryAdded') {
        console.log(`[log.${message.params.entry.level}] ${message.params.entry.text}`);
    } else if (message.method === 'Runtime.exceptionThrown') {
        const detail = message.params.exceptionDetails;
        console.log(`[exception] ${detail.text} ${detail.exception?.description ?? ''}`);
    }
});

await navigate;
await sleep(waitMs);

const shot = await cdp.send('Page.captureScreenshot', { format: 'png', captureBeyondViewport: false });
const { writeFile } = await import('node:fs/promises');
await writeFile(outputPath, Buffer.from(shot.data, 'base64'));

console.log(`captured ${outputPath}`);
ws.close();
process.exit(0);
