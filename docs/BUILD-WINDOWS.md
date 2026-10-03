# Building CoA with mod-coa-lre on Windows

These steps build the CoA worldserver from source with this module, then swap it into an existing CoA server (for example one installed by Corfirean's CoA Server Manager).

## Toolchain
| Tool | Version used |
|---|---|
| Visual Studio 2022 toolset (MSVC 14.44, x64) | via `vcvars64.bat -vcvars_ver=14.44` |
| CMake + Ninja | from Visual Studio |
| Boost | 1.87 (`C:\local\boost_1_87_0`) |
| MySQL client | 8.4 (`C:\tools\mysql\current`) |
| OpenSSL | 3.5 (`C:\Program Files\OpenSSL-Win64`) |

## Pinned sources
| Repo | Commit |
|---|---|
| `Corfirean/azerothcore-wotlk-coa` | `df5bcdf260924fd9f5f178ffc6075bec5dc206ed` |
| `Corfirean/mod-coa-playerbots` | `7ad1c0b5eb2640a862c410e0aeb00f37eda2f585` |
| `Corfirean/mod-coa-content-scaling` | `d279b59c43bb06223a706425535c0b6a6f18a3a8` |

## Steps (cmd)
1. Load the compiler:
   ```bat
   call "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat" -vcvars_ver=14.44
   ```
2. Point Boost at 1.87 (an old `Boost_ROOT` environment variable can otherwise win):
   ```bat
   set Boost_ROOT=C:\local\boost_1_87_0
   ```
3. Configure once:
   ```bat
   cd /d C:\src\coa-build\core
   cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo -DAPPS_BUILD=all -DTOOLS_BUILD=none -DSCRIPTS=static -DMODULES=static -DUSE_COREPCH=1 -DUSE_SCRIPTPCH=1 -DBOOST_ROOT=C:/local/boost_1_87_0 -DMYSQL_INCLUDE_DIR=C:/tools/mysql/current/include -DMYSQL_LIBRARY=C:/tools/mysql/current/lib/mysqlclient.lib "-DOPENSSL_ROOT_DIR=C:/Program Files/OpenSSL-Win64"
   ```
4. Build:
   ```bat
   cmake --build build --target worldserver
   ```

## Gotcha: unzipped sources keep old timestamps
`Expand-Archive` keeps the timestamps stored in a zip. If they are older than the last build, Ninja skips the file and only relinks old code. After unzipping, mark the module as changed (PowerShell):
```powershell
Get-ChildItem "C:\src\coa-build\core\modules\mod-coa-lre" -Recurse -File | ForEach-Object { $_.LastWriteTime = Get-Date }
```

## Database
Run the module SQL on every world database (cmd):
```bat
"<server>\mysql\bin\mysql.exe" "--defaults-extra-file=<server>\mysql\admin-client.ini" -h 127.0.0.1 -P 3307 acore_world < "C:\src\coa-build\core\modules\mod-coa-lre\sql\mod_coa_lre_world.sql"
```

## Swap into the server
Stop every worldserver that uses the binary, keep a backup of the original, then copy (PowerShell):
```powershell
$S = "<server>\Core"; if (Get-Process worldserver -ErrorAction SilentlyContinue) { "STOP: a worldserver is running" } else { Copy-Item "C:\src\coa-build\core\build\bin\worldserver.exe" "$S\worldserver.exe" -Force }
```
If the server is managed by Corfirean's CoA Server Manager, do not apply Manager updates while the custom binary is in place: they overwrite it.
