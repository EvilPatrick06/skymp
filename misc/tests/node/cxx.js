'use strict';
// The C++ compiler the native node tests build their fixtures with
// (Thornswood #1970). ctest runs them from whatever shell ran ctest, and the
// pull request build's Test step is a plain PowerShell, not a developer
// prompt, so cl.exe is not on its PATH. In order:
//   CXX                      when set, that compiler with this environment;
//   anything but Windows     c++;
//   a developer prompt       cl.exe as it stands (VCToolsInstallDir is set);
//   otherwise                the newest Visual Studio with the C++ tools,
//                            found through vswhere, with the environment its
//                            vcvars64.bat sets.
// Returns { command, env } for child_process.spawnSync.
const cp = require('node:child_process'), fs = require('node:fs'), path = require('node:path');

function compiler() {
  if (process.env.CXX) return { command: process.env.CXX, env: process.env };
  if (process.platform !== 'win32') return { command: 'c++', env: process.env };
  if (process.env.VCToolsInstallDir) return { command: 'cl.exe', env: process.env };

  const vswhere = path.join(process.env['ProgramFiles(x86)'] || 'C:\\Program Files (x86)',
    'Microsoft Visual Studio', 'Installer', 'vswhere.exe');
  if (!fs.existsSync(vswhere)) throw Error('No cl.exe: no developer prompt, no CXX, and no vswhere at ' + vswhere);
  const vs = cp.execFileSync(vswhere, ['-latest', '-products', '*', '-requires',
    'Microsoft.VisualStudio.Component.VC.Tools.x86.x64', '-property', 'installationPath'], { encoding: 'utf8' }).trim();
  if (!vs) throw Error('No Visual Studio with the C++ tools (vswhere found none)');
  const vcvars = path.join(vs, 'VC', 'Auxiliary', 'Build', 'vcvars64.bat');
  if (!fs.existsSync(vcvars)) throw Error('No vcvars64.bat at ' + vcvars);

  // vcvars64.bat asks vswhere itself, by name, so its folder goes on PATH.
  const pathKey = Object.keys(process.env).find(k => k.toUpperCase() === 'PATH') || 'PATH';
  const withVswhere = { ...process.env, [pathKey]: path.dirname(vswhere) + ';' + (process.env[pathKey] || '') };
  const env = {};
  const set = cp.execSync('"' + vcvars + '" >nul && set', { encoding: 'utf8', shell: 'cmd.exe', windowsHide: true, env: withVswhere });
  for (const line of set.split(/\r?\n/)) {
    const eq = line.indexOf('=');
    if (eq > 0) env[line.slice(0, eq)] = line.slice(eq + 1);
  }
  if (!env.VCToolsInstallDir) throw Error(vcvars + ' did not set VCToolsInstallDir');
  return { command: path.join(env.VCToolsInstallDir, 'bin', 'Hostx64', 'x64', 'cl.exe'), env };
}

module.exports = { compiler };
