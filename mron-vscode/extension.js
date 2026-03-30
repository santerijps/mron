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
    const fs = require("fs");

    // Check inside the extension directory itself
    const inside = path.join(context.extensionPath, "mron-lsp" + ext);
    try { fs.accessSync(inside, fs.constants.X_OK); return inside; } catch (_) { }

    // Check next to the extension directory
    const adjacent = path.join(context.extensionPath, "..", "mron-lsp" + ext);
    try { fs.accessSync(adjacent, fs.constants.X_OK); return adjacent; } catch (_) { }

    // Check workspace root
    const folders = workspace.workspaceFolders;
    if (folders) {
        const ws = path.join(folders[0].uri.fsPath, "mron-lsp" + ext);
        try { fs.accessSync(ws, fs.constants.X_OK); return ws; } catch (_) { }
    }

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
