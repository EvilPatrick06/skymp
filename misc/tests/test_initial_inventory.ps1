$ErrorActionPreference='Stop'
$taskRepo=Split-Path -Parent (Split-Path -Parent (Split-Path -Parent $PSCommandPath))
$taskCache='C:\ThornswoodTemps&Worktrees\worktrees\engine-one-skymp'
$taskScratch=Join-Path ([IO.Path]::GetTempPath()) ('thornswood-initial-save-'+[guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $taskScratch | Out-Null
$taskSource=[IO.File]::ReadAllText((Join-Path $taskRepo 'skyrim-platform\src\platform_se\skyrim_platform\LoadGame.cpp'))
$taskParts=@()
foreach($name in 'Decompress','Compress','WriteChangeForm'){
 $start=$taskSource.IndexOf('LoadGame::'+$name+'(')
 if($start -lt 0){throw ('Missing native function: '+$name)}
 $start=$taskSource.LastIndexOf("`n",$start-1)+1
 # The return type is on the immediately preceding line in these definitions.
 if($name -in 'Decompress','Compress'){$start=$taskSource.LastIndexOf("`n",$start-2)+1}
 $brace=$taskSource.IndexOf('{',$start);$depth=1;$end=$brace+1
 while($depth -gt 0 -and $end -lt $taskSource.Length){if($taskSource[$end] -eq '{'){$depth++};if($taskSource[$end] -eq '}'){$depth--};$end++}
 if($depth){throw 'Unterminated native function'}
 $taskParts+=$taskSource.Substring($start,$end-$start)
}
[IO.File]::WriteAllText((Join-Path $taskScratch 'InitialSaveFunctionsUnderTest.inc'),($taskParts -join "`n"))
$taskApiSource=[IO.File]::ReadAllText((Join-Path $taskRepo 'skyrim-platform\src\platform_se\skyrim_platform\LoadGameApi.cpp'))
$taskApiParts=@()
foreach($signature in 'double InitialInventoryInteger(', 'std::unique_ptr<std::vector<InitialInventory::Item>> CreateInitialInventory('){
 $start=$taskApiSource.IndexOf($signature)
 if($start -lt 0){throw ('Missing inventory API function: '+$signature)}
 $brace=$taskApiSource.IndexOf('{',$start);$depth=1;$end=$brace+1
 while($depth -gt 0 -and $end -lt $taskApiSource.Length){if($taskApiSource[$end] -eq '{'){$depth++};if($taskApiSource[$end] -eq '}'){$depth--};$end++}
 if($depth){throw 'Unterminated inventory API function'}
 $taskApiParts+=$taskApiSource.Substring($start,$end-$start)
}
[IO.File]::WriteAllText((Join-Path $taskScratch 'InitialInventoryApiUnderTest.inc'),($taskApiParts -join "`n"))
$taskVs='C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools'
$taskEnv=cmd /c "`"$taskVs\VC\Auxiliary\Build\vcvars64.bat`" >nul && set"
foreach($value in $taskEnv){if($value -match '^([^=]+)=(.*)$'){[Environment]::SetEnvironmentVariable($matches[1],$matches[2],'Process')}}
$taskCl=Join-Path $taskVs 'VC\Tools\MSVC\14.44.35207\bin\Hostx64\x64\cl.exe'
$taskInclude=Join-Path $taskCache 'build\vcpkg_installed\x64-windows-sp\include'
$taskZlib=Join-Path $taskCache 'build\vcpkg_installed\x64-windows-sp\lib\zlib.lib'
$taskLib=Join-Path $taskCache 'build\savefile\Release\savefile.lib'
Push-Location $taskScratch
try{
 & $taskCl /nologo /EHsc /std:c++17 /MT ('/I'+$taskScratch) ('/I'+$taskInclude) ('/I'+(Join-Path $taskRepo 'savefile\include')) ('/I'+(Join-Path $taskRepo 'skyrim-platform\src\platform_se\skyrim_platform')) (Join-Path $taskRepo 'misc\tests\test_initial_inventory.cpp') (Join-Path $taskRepo 'savefile\src\SFStructure.cpp') (Join-Path $taskRepo 'savefile\src\SFSeekerOfDifferences.cpp') $taskLib $taskZlib /Fe:initial-save.exe
 if($LASTEXITCODE){throw 'Initial save test compilation failed'}
 & .\initial-save.exe (Join-Path $taskRepo 'skyrim-platform\src\platform_se\skyrim_platform\assets\template.ess') (Join-Path $taskScratch 'roundtrip.ess')
 if($LASTEXITCODE){throw 'Initial save binary regression failed'}
 & $taskCl /nologo /EHsc /std:c++17 /MT ('/I'+(Join-Path $taskRepo 'savefile\include')) (Join-Path $taskRepo 'misc\tests\test_initial_save_refs.cpp') (Join-Path $taskRepo 'savefile\src\SFStructure.cpp') /Fe:initial-refs.exe
 if($LASTEXITCODE){throw 'Initial reference test compilation failed'}
 & .\initial-refs.exe
 if($LASTEXITCODE){throw 'Initial reference regression failed'}
 Write-Output ('PASS actual native save functions and full ESS Reader/Writer: inventory splice, RefIDs, compression, length widths. Evidence '+$taskScratch)
}finally{Pop-Location}
