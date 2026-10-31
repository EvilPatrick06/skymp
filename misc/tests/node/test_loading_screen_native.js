'use strict';
// Thornswood #3130. Builds test_loading_screen_native.cpp against the real
// LoadingScreen.h, runs the two scripts it prints in a stand-in window, and
// reads the four places that use it.
const fs = require('node:fs'), path = require('node:path'), os = require('node:os'), cp = require('node:child_process'), vm = require('node:vm');
const root = path.join(__dirname, '../../..');
const headers = path.join(root, 'skyrim-platform/src/tilted/ui');
if (!fs.existsSync(path.join(headers, 'LoadingScreen.h'))) throw Error('LoadingScreen.h missing');

let failed = 0;
function check(what, ok) {
  console.log((ok ? 'ok   ' : 'FAIL ') + what);
  if (!ok) failed++;
}

// The page's side: the scripts set the flag and dispatch the event once each.
function runInPage(script) {
  const heard = [];
  const window = {
    dispatchEvent(e) { heard.push(e); return true; }
  };
  class CustomEvent {
    constructor(type, init) { this.type = type; this.detail = init && init.detail; }
  }
  vm.runInNewContext(script, { window, CustomEvent });
  return { window, heard };
}

const work = fs.mkdtempSync(path.join(os.tmpdir(), 'thornswood-native-loading-'));
try {
  const fixture = path.join(__dirname, 'test_loading_screen_native.cpp');
  const exe = path.join(work, process.platform === 'win32' ? 'loading.exe' : 'loading');
  const {command: compiler, env} = require('./cxx').compiler();
  const args = process.platform === 'win32'
    ? ['/nologo', '/EHsc', '/std:c++17', '/I' + headers, '/Fe:' + exe, '/Fo:' + path.join(work, 'loading.obj'), fixture]
    : ['-std=c++17', '-I' + headers, fixture, '-o', exe];
  const build = cp.spawnSync(compiler, args, {encoding: 'utf8', cwd: work, env});
  process.stdout.write(build.stdout || ''); process.stderr.write(build.stderr || '');
  if (build.error) throw build.error;
  check('the fixture builds', build.status === 0);
  if (build.status === 0) {
    const run = cp.spawnSync(exe, [], {encoding: 'utf8'});
    process.stderr.write(run.stderr || '');
    check('the fixture runs', run.status === 0);
    const [open, closed] = String(run.stdout || '').split(/\r?\n/);

    const a = runInPage(open);
    check('opening: window.skyrimPlatformLoading is true', a.window.skyrimPlatformLoading === true);
    check('opening: one skyrimPlatform:loading event, detail.loading true',
      a.heard.length === 1 && a.heard[0].type === 'skyrimPlatform:loading' && a.heard[0].detail.loading === true);

    const b = runInPage(closed);
    check('closing: window.skyrimPlatformLoading is false', b.window.skyrimPlatformLoading === false);
    check('closing: one skyrimPlatform:loading event, detail.loading false',
      b.heard.length === 1 && b.heard[0].type === 'skyrimPlatform:loading' && b.heard[0].detail.loading === false);

    // A page with no CustomEvent (or one that throws) is not broken by it.
    let threw = false;
    try { vm.runInNewContext(open, { window: {} }); } catch (e) { threw = true; }
    check('a page that cannot take the event does not throw', !threw);
  }
} finally {
  const parent = fs.realpathSync(os.tmpdir());
  const target = fs.realpathSync(work);
  if (path.dirname(target).toLowerCase() !== parent.toLowerCase() || !path.basename(target).startsWith('thornswood-native-loading-')) throw Error('Unsafe test cleanup target');
  fs.rmSync(target, {recursive: true, force: true});
}

// Where it is used. Read, since none of these builds off Windows.
const read = (p) => fs.readFileSync(path.join(root, p), 'utf8');
const events = read('skyrim-platform/src/platform_se/skyrim_platform/EventHandler.cpp');
const tilted = read('skyrim-platform/src/platform_se/skyrim_platform/BrowserApiTilted.cpp');
// The browser's own menu sink (fork #134, Thornswood #5140) hears the Loading
// Menu whether or not a plugin listens for menus. It is added at kDataLoaded,
// before the first tick runs a plugin, so it is ahead of the plugins' sink, and
// their menuOpen is queued for the next tick in any case.
check('the menu sink tells the browser of the Loading Menu, before the queued menuOpen',
  /if \(event->menuName == RE::LoadingMenu::MENU_NAME\) \{\s*BrowserApiTilted::OnLoadingMenu\(event->opening\);\s*\}/.test(tilted) &&
  /BrowserApiTilted::WatchMenus\(\);\s*TickHandler::GetSingleton\(\)->Update\(\);/.test(events) &&
  !events.includes('BrowserApiTilted::OnLoadingMenu('));
check('OnLoadingMenu sets Loading() and runs the script',
  /void BrowserApiTilted::OnLoadingMenu\(bool opening\)[\s\S]*?DX11RenderHandler::Loading\(\) = opening;[\s\S]*?ExecuteJavaScript\(LoadingScreen::Script\(opening\)\)/.test(tilted));
const render = read('skyrim-platform/src/tilted/ui/DX11RenderHandler.cpp');
check('the page is drawn by ShouldDraw(Visible(), Loading())',
  render.includes('if (LoadingScreen::ShouldDraw(Visible(), Loading())) {') && !/\n  if \(Visible\(\)\) \{\n    std::unique_lock/.test(render));
{
  const at = render.indexOf('if (LoadingScreen::ShouldCover(Loading())) {');
  const clear = render.indexOf('ClearRenderTargetView(pRenderTargetView.Get(),', at);
  const begin = render.indexOf('m_pSpriteBatch->Begin(');
  check("while loading the game's picture is cleared to kCover, before the page is drawn",
    at > 0 && clear > at && clear < begin && render.indexOf('LoadingScreen::kCover', clear) > clear &&
    render.indexOf('LoadingScreen::kCover', clear) < begin && render.indexOf('GetRenderTargetSize();') < at);
}
const load = read('skyrim-platform/src/tilted/ui/MyLoadHandler.cpp');
check('a page that loads during a load is told',
  /if \(DX11RenderHandler::Loading\(\)\) \{\s*frame->ExecuteJavaScript\(LoadingScreen::Script\(true\)/.test(load));
const browser = read('skymp5-client/src/services/services/browserService.ts');
const bad = /private readonly badMenus: Menu\[\] = \[([\s\S]*?)\];/.exec(browser);
check('the client no longer hides the browser for the Loading Menu',
  !!bad && !/^\s*Menu\.Loading,/m.test(bad[1]) && /^\s*Menu\.Map,/m.test(bad[1]));

if (failed) { console.log(failed + ' failed'); process.exitCode = 1; }
else console.log('all passed');
