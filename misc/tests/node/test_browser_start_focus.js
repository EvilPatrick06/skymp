'use strict';
// Thornswood: the browser is shown at start, never focused at start, and the
// Cursor Menu reaches CursorFocus from the moment the game's data has loaded.
//
// 31 Oct 2026, dev 20261031-143003-dev: [Browser] ShowAtStart focused the page
// at 14:59:44, fifteen seconds before the game's data loaded and twenty-five
// before any plugin ran. The Cursor Menu's events reached CursorFocus only
// through EventHandler's MenuOpenCloseEvent sink, which is added once a plugin
// subscribes to menuOpen or menuClose. So the page held the game's input with
// the Cursor Menu closed: no cursor drawn, no mouse move reaching the page,
// and a click landing at the middle of the screen (1720,720 of 3440x1440, the
// game's untouched cursor position), where it picked the character drawn
// there. These checks read the sources, since the functions need the game.
const fs = require('node:fs'), path = require('node:path');
const dir = path.join(__dirname, '../../../skyrim-platform/src/platform_se/skyrim_platform');
const read = (f) => fs.readFileSync(path.join(dir, f), 'utf8');
let failed = 0;
function check(name, ok) {
  console.log((ok ? 'PASS ' : 'FAIL ') + name);
  if (!ok) failed++;
}
// The body of a function, from its opening brace to the matching close.
function body(src, signature) {
  const at = src.indexOf(signature);
  if (at < 0) return null;
  const open = src.indexOf('{', at);
  let depth = 0;
  for (let i = open; i < src.length; i++) {
    if (src[i] === '{') depth++;
    else if (src[i] === '}' && --depth === 0) return src.slice(open, i + 1);
  }
  return null;
}
const code = (s) => s.replace(/\/\/[^\n]*/g, '').replace(/\/\*[\s\S]*?\*\//g, '');

const tilted = read('BrowserApiTilted.cpp');
const show = body(tilted, 'void BrowserApiTilted::ShowAtStart()');
check('Tilted ShowAtStart exists', !!show);
check('Tilted ShowAtStart shows the page', !!show && /Visible\(\)\s*=\s*true/.test(code(show)));
check('Tilted ShowAtStart does not focus it', !!show && !/SetFocusedImpl|ChromeFocus|Queue\(/.test(code(show)));

const nirn = read('BrowserApiNirnLab.cpp');
const ctor = body(nirn, 'BrowserApiNirnLab::BrowserApiNirnLab()');
check('NirnLab start shows the page', !!ctor && /wantedIsVisible\s*=\s*true/.test(code(ctor)));
check('NirnLab start does not focus it', !!ctor && !/wantedIsFocused\s*=\s*true/.test(code(ctor)));

const watch = body(tilted, 'void BrowserApiTilted::WatchMenus()');
check('WatchMenus adds a sink of its own to the UI', !!watch && /AddEventSink\(&g_menuSink\)/.test(code(watch)));
const sink = body(tilted, 'class MenuSink');
check('its sink hands the Cursor Menu to OnCursorMenu',
      !!sink && /CursorMenu::MENU_NAME\)\s*\{\s*BrowserApiTilted::OnCursorMenu\(event->opening\)/.test(code(sink)));
check('its sink hands the Loading Menu to OnLoadingMenu',
      !!sink && /LoadingMenu::MENU_NAME\)\s*\{\s*BrowserApiTilted::OnLoadingMenu\(event->opening\)/.test(code(sink)));

const handler = code(read('EventHandler.cpp'));
const loaded = handler.slice(handler.indexOf('case SKSE::MessagingInterface::kDataLoaded'));
const w = loaded.indexOf('BrowserApiTilted::WatchMenus()');
const t = loaded.indexOf('TickHandler::GetSingleton()->Update()');
check('kDataLoaded starts WatchMenus before the first tick runs a plugin', w >= 0 && t >= 0 && w < t);
const menuEvent = body(handler, 'EventHandler::ProcessEvent(\n  const RE::MenuOpenCloseEvent*');
check("the plugins' lazy menu sink no longer calls the browser itself (no double calls)",
      !!menuEvent && !/OnCursorMenu|OnLoadingMenu/.test(menuEvent));

if (failed) { console.log(failed + ' check(s) failed'); process.exitCode = 1; }
