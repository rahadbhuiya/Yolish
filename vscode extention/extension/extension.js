const vscode = require('vscode');
const path   = require('path');
const fs     = require('fs');

function getYsPath() {
    const cfg = vscode.workspace.getConfiguration('yolish');
    return cfg.get('executablePath') || 'ys';
}

function getTarget() {
    const cfg = vscode.workspace.getConfiguration('yolish');
    const t   = cfg.get('compileTarget') || 'auto';
    if (t !== 'auto') return t;
    switch (process.platform) {
        case 'win32':  return 'windows';
        case 'darwin': return 'macos';
        default:       return 'linux';
    }
}

function getOrCreateTerminal() {
    // Reuse existing Yolish terminal if open
    for (const t of vscode.window.terminals) {
        if (t.name === 'Yolish') return t;
    }
    return vscode.window.createTerminal('Yolish');
}

function activate(context) {

    //  Run command 
    const runCmd = vscode.commands.registerCommand('yolish.run', () => {
        const editor = vscode.window.activeTextEditor;
        if (!editor) {
            vscode.window.showWarningMessage('No active Yolish file.');
            return;
        }
        // Save before running
        editor.document.save().then(() => {
            const file = editor.document.fileName;
            const ys   = getYsPath();
            const term = getOrCreateTerminal();
            term.show(true);
            term.sendText(`${ys} "${file}"`);
        });
    });

    //  Compile command 
    const compileCmd = vscode.commands.registerCommand('yolish.compile', () => {
        const editor = vscode.window.activeTextEditor;
        if (!editor) {
            vscode.window.showWarningMessage('No active Yolish file.');
            return;
        }
        editor.document.save().then(() => {
            const file   = editor.document.fileName;
            const dir    = path.dirname(file);
            const base   = path.basename(file, '.y');
            const target = getTarget();
            const ys     = getYsPath();
            const outExt = target === 'windows' ? '.exe' : '';
            const out    = path.join(dir, base + outExt);
            const term   = getOrCreateTerminal();
            term.show(true);
            term.sendText(`${ys} -c "${file}" -o "${out}" --target ${target}`);
            vscode.window.showInformationMessage(
                `Compiling ${base}.y → ${base}${outExt} (${target})`
            );
        });
    });

    //  REPL command 
    const replCmd = vscode.commands.registerCommand('yolish.repl', () => {
        const ys   = getYsPath();
        const term = vscode.window.createTerminal('Yolish REPL');
        term.show(true);
        term.sendText(ys);
    });

    context.subscriptions.push(runCmd, compileCmd, replCmd);

    //  Status bar item 
    const statusRun = vscode.window.createStatusBarItem(
        vscode.StatusBarAlignment.Left, 100
    );
    statusRun.text     = '$(play) Run Yolish';
    statusRun.tooltip  = 'Run current .y file (F5)';
    statusRun.command  = 'yolish.run';

    const statusCompile = vscode.window.createStatusBarItem(
        vscode.StatusBarAlignment.Left, 99
    );
    statusCompile.text    = '$(gear) Compile';
    statusCompile.tooltip = 'Compile to native binary (Ctrl+Shift+B)';
    statusCompile.command = 'yolish.compile';

    // Show status bar items only when a .y file is active
    function updateStatusBar() {
        const editor = vscode.window.activeTextEditor;
        if (editor && editor.document.fileName.endsWith('.y')) {
            statusRun.show();
            statusCompile.show();
        } else {
            statusRun.hide();
            statusCompile.hide();
        }
    }

    vscode.window.onDidChangeActiveTextEditor(updateStatusBar, null, context.subscriptions);
    updateStatusBar();

    context.subscriptions.push(statusRun, statusCompile);
}

function deactivate() {}

module.exports = { activate, deactivate };
