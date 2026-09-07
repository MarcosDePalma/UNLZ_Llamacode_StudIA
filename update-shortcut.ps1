param(
    [string]$Config = "Debug",
    [string]$ShortcutName = "LlamaCode",
    [string]$Icon = "assets\app_icon.ico",
    [string]$ShortcutPath,
    # Publicar/actualizar tambien el acceso del menu Inicio. Se apaga en los
    # tests (y en CI) para no tocar el perfil de la maquina que compila.
    [switch]$NoStartMenu,
    # Carpeta del menu Inicio. Parametrizada para poder testearla contra un
    # directorio temporal en vez del perfil real del usuario.
    [string]$StartMenuDir
)

$AppUserModelId = if ($Config -ieq 'Debug') {
    "LlamaCode.Desktop.Debug"
} else {
    "LlamaCode.Desktop.App"
}

$projectRoot = Split-Path -Parent $MyInvocation.MyCommand.Path
if (-not $ShortcutPath -or [string]::IsNullOrWhiteSpace($ShortcutPath)) {
    $ShortcutPath = Join-Path $projectRoot "$ShortcutName.lnk"
}

$exePath  = Join-Path $projectRoot "build\$Config\LlamaCode.exe"
$iconPath = Join-Path $projectRoot $Icon

Add-Type -TypeDefinition @"
using System;
using System.Runtime.InteropServices;

[ComImport]
[Guid("886D8EEB-8CF2-4446-8D02-CDBA1DBDCF99")]
[InterfaceType(ComInterfaceType.InterfaceIsIUnknown)]
public interface IPropertyStore
{
    void GetCount(out uint cProps);
    void GetAt(uint iProp, out PROPERTYKEY pkey);
    void GetValue(ref PROPERTYKEY key, out PROPVARIANT pv);
    void SetValue(ref PROPERTYKEY key, ref PROPVARIANT pv);
    void Commit();
}

[StructLayout(LayoutKind.Sequential, Pack = 4)]
public struct PROPERTYKEY
{
    public Guid fmtid;
    public uint pid;
}

[StructLayout(LayoutKind.Explicit)]
public struct PROPVARIANT
{
    [FieldOffset(0)] public ushort vt;
    [FieldOffset(8)] public IntPtr p;

    public static PROPVARIANT FromString(string value)
    {
        return new PROPVARIANT
        {
            vt = 31, // VT_LPWSTR
            p = Marshal.StringToCoTaskMemUni(value)
        };
    }
}

public static class LlamaCodeShortcutPropertyStore
{
    private const uint GPS_READWRITE = 0x00000002;

    [DllImport("shell32.dll", CharSet = CharSet.Unicode, PreserveSig = false)]
    private static extern void SHGetPropertyStoreFromParsingName(
        [MarshalAs(UnmanagedType.LPWStr)] string pszPath,
        IntPtr pbc,
        uint flags,
        ref Guid riid,
        [MarshalAs(UnmanagedType.Interface)] out IPropertyStore propertyStore);

    [DllImport("ole32.dll")]
    private static extern int PropVariantClear(ref PROPVARIANT pvar);

    public static void SetAppUserModelId(string shortcutPath, string appUserModelId)
    {
        IPropertyStore propertyStore = null;
        try
        {
            var propertyStoreId = new Guid("886D8EEB-8CF2-4446-8D02-CDBA1DBDCF99");
            SHGetPropertyStoreFromParsingName(shortcutPath, IntPtr.Zero, GPS_READWRITE, ref propertyStoreId, out propertyStore);

            var key = new PROPERTYKEY
            {
                fmtid = new Guid("9F4C2855-9F79-4B39-A8D0-E1D42DE1D5F3"),
                pid = 5
            };
            var value = PROPVARIANT.FromString(appUserModelId);
            try
            {
                propertyStore.SetValue(ref key, ref value);
                propertyStore.Commit();
            }
            finally
            {
                PropVariantClear(ref value);
            }
        }
        finally
        {
            if (propertyStore != null)
                Marshal.ReleaseComObject(propertyStore);
        }
    }
}
"@

function Set-LlamaCodeShortcutAppId {
    param([Parameter(Mandatory = $true)][string]$Path)

    if (Test-Path $Path) {
        [LlamaCodeShortcutPropertyStore]::SetAppUserModelId($Path, $AppUserModelId)
    }
}

function Update-LlamaCodeShortcutFile {
    param([Parameter(Mandatory = $true)][string]$Path)

    $shortcut = $wsh.CreateShortcut($Path)

    if (Test-Path $exePath) {
        $shortcut.TargetPath = $exePath
        $shortcut.Arguments = ""
        $shortcut.WorkingDirectory = Split-Path -Parent $exePath
        # Prefer the standalone .ico so Debug/Release shortcuts visibly differ even
        # if Windows caches the embedded exe icon.
        if (Test-Path $iconPath) {
            $shortcut.IconLocation = "$iconPath,0"
        } else {
            $shortcut.IconLocation = "$exePath,0"
        }
    } else {
        # Keep the shortcut bound to the requested build artifact even before
        # the first build creates it; do not hide a missing binary behind cmd.exe.
        $shortcut.TargetPath = $exePath
        $shortcut.Arguments = ""
        $shortcut.WorkingDirectory = Split-Path -Parent $exePath
        if (Test-Path $iconPath) {
            $shortcut.IconLocation = "$iconPath,0"
        }
    }

    $shortcut.Save()
    Set-LlamaCodeShortcutAppId -Path $Path
}

$wsh = New-Object -ComObject WScript.Shell
Update-LlamaCodeShortcutFile -Path $ShortcutPath

$updatedPinnedShortcuts = @()
$taskbarDir = Join-Path $env:APPDATA "Microsoft\Internet Explorer\Quick Launch\User Pinned\TaskBar"
if ((Test-Path $exePath) -and (Test-Path $taskbarDir)) {
    $expectedTarget = [System.IO.Path]::GetFullPath($exePath)
    Get-ChildItem -LiteralPath $taskbarDir -Filter "*.lnk" -ErrorAction SilentlyContinue | ForEach-Object {
        $pinned = $wsh.CreateShortcut($_.FullName)
        $legacyDebugPin = ($Config -ieq 'Debug' -and
            $_.BaseName -in @('LlamaCode', 'LlamaCode-Debug'))
        $legacyReleasePin = ($Config -ine 'Debug' -and
            $_.BaseName -eq 'LlamaCode')
        if ($legacyDebugPin -or $legacyReleasePin) {
            # A stale pinned shortcut may point to a deleted build and therefore
            # cannot be matched by target. Repair it by its stable legacy name.
            Update-LlamaCodeShortcutFile -Path $_.FullName
            $updatedPinnedShortcuts += $_.FullName
        } elseif (-not [string]::IsNullOrWhiteSpace($pinned.TargetPath)) {
            $pinnedTarget = [System.IO.Path]::GetFullPath($pinned.TargetPath)
            if ([string]::Equals($pinnedTarget, $expectedTarget, [System.StringComparison]::OrdinalIgnoreCase)) {
                Update-LlamaCodeShortcutFile -Path $_.FullName
                $updatedPinnedShortcuts += $_.FullName
            }
        }
    }
}

# Menu Inicio: es de donde arranca la app cualquiera que no tenga el pin, y es
# lo unico que resuelven las herramientas que buscan apps por nombre. Se mantiene
# junto con el .lnk del repo para que Release y Debug convivan y ninguno quede
# apuntando a un build viejo.
$startMenuShortcut = ""
if (-not $NoStartMenu) {
    if ([string]::IsNullOrWhiteSpace($StartMenuDir)) {
        $StartMenuDir = Join-Path $env:APPDATA "Microsoft\Windows\Start Menu\Programs"
    }
    if (-not (Test-Path $StartMenuDir)) {
        New-Item -ItemType Directory -Force -Path $StartMenuDir | Out-Null
    }
    $startMenuShortcut = Join-Path $StartMenuDir "$ShortcutName.lnk"
    Update-LlamaCodeShortcutFile -Path $startMenuShortcut
}

$saved = $wsh.CreateShortcut($ShortcutPath)
[PSCustomObject]@{
    ShortcutPath      = $ShortcutPath
    TargetPath        = $saved.TargetPath
    Arguments         = $saved.Arguments
    WorkingDirectory  = $saved.WorkingDirectory
    IconLocation      = $saved.IconLocation
    AppUserModelID    = $AppUserModelId
    UpdatedPinned     = $updatedPinnedShortcuts
    StartMenuShortcut = $startMenuShortcut
} | Format-List
