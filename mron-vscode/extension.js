const path = require("path");
const { workspace, window } = require("vscode");
const { LanguageClient, TransportKind } = require("vscode-languageclient/node");

/** @type {LanguageClient | undefined} */
let client;

/**
 * Find the mron-lsp binary. Checks (in order):
 *   1. User setting mron.serverPath
 *   2. Next to the extension directory (../mron-lsp or ../mron-lsp.exe)
 *   3. On PATH (just "mron-lsp")
 */
function findServer(context) {
    const cfg = workspace.getConfiguration("mron");
    const explicit = cfg.get("serverPath", "");
    if (explicit) return explicit;

    const ext = process.platform === "win32" ? ".exe" : "";
    const adjacent = path.join(context.extensionPath, "..", "mron-lsp" + ext);
    try {
        require("fs").accessSync(adjacent, require("fs").constants.X_OK);
        return adjacent;
    } catch (_) { /* not found */ }

    return "mron-lsp" + ext;
}

function activate(context) {
    const command = findServer(context);

    const serverOptions = {
        run: { command, transport: TransportKind.stdio },
        debug: { command, transport: TransportKind.stdio },
    };

    const schema = workspace.getConfiguration("mron").get("schema", "");

    const clientOptions = {
        documentSelector: [{ scheme: "file", language: "mron" }],
        initializationOptions: schema ? { schema } : {},
    };

    client = new LanguageClient("mron", "MRON Language Server", serverOptions, clientOptions);
    client.start();
    context.subscriptions.push({ dispose: () => client && client.stop() });
}

function deactivate() {
    if (client) return client.stop();
}

module.exports = { activate, deactivate };
