; StudIA — instalador principal
;
; Instala el PROGRAMA: ejecutable, modelo de embeddings y las dependencias.
;
; El material de estudio NO viene aca. Viaja aparte, en una carpeta con dos
; cosas adentro: studia.db (el indice) y DATA_StudIA (los PDF). Se copia donde
; el usuario quiera y se elige el .db desde la app, en Abrir indice: de esa
; misma ruta sale tambien donde estan los documentos. Sin scripts de copiado ni
; rutas fijas: antes habia que dejar el corpus en C:\StudIA_Docs con robocopy.
;
; Separarlo tiene dos motivos. Uno practico: el indice cambia cada vez que se
; reindexa y el programa no, asi que actualizar uno no obliga a rehacer el otro.
; Otro de limites de Windows: un .exe no pasa de 4,2 GB y el material los supera.
;
; No requiere administrador: instala para el usuario actual.
;
; Se compila con installer\compilar.bat, que ademas copia el runtime de MSVC.

#define Nombre    "StudIA"
#define Version   "1.0"
#define Autor     "Marcos Agustin De Palma"
#define Ejecutable "LlamaCode.exe"
#define Origen    "..\build\Release"

[Setup]
AppId={{074B25A0-6549-4F83-9F6F-A22755B9895F}
AppName={#Nombre}
AppVersion={#Version}
AppVerName={#Nombre} {#Version}
AppPublisher={#Autor}
AppComments=Asistente de estudio — PPS Ingenieria Mecatronica, UNLZ
DefaultDirName={autopf}\{#Nombre}
DefaultGroupName={#Nombre}
DisableProgramGroupPage=yes
UninstallDisplayName={#Nombre}
UninstallDisplayIcon={app}\{#Ejecutable}

; Sin UAC: instala en la carpeta del usuario. Un instalador que arranca pidiendo
; permisos de administrador es la primera excusa para no probarlo.
PrivilegesRequired=lowest
PrivilegesRequiredOverridesAllowed=dialog

ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible

OutputDir=..\dist
OutputBaseFilename={#Nombre}-{#Version}-Setup
SetupIconFile=..\assets\app_icon.ico
WizardStyle=modern

; El grueso del paquete son vectores (float32) y un modelo GGUF ya cuantizado:
; comprimen poquisimo. Subir a /max agrega mucho tiempo de compilacion para
; ganar migajas, asi que se queda en normal.
Compression=lzma2/normal
SolidCompression=yes
LZMAUseSeparateProcess=yes

; Con casi 1 GB de indice, avisar antes de empezar evita el corte a mitad.
ExtraDiskSpaceRequired=0

[Languages]
Name: "es"; MessagesFile: "compiler:Languages\Spanish.isl"

[Tasks]
Name: "escritorio"; Description: "Crear un acceso directo en el escritorio"; \
    GroupDescription: "Accesos directos:"
Name: "extras"; Description: "Instalar las funciones extra (diagramas, OCR, graficos)"; \
    GroupDescription: "Al terminar:"

[Files]
; La app. Se excluye la carpeta StudIA entera (lo de adentro va explicito mas
; abajo): si se dejara pasar, el compilador entraria a recorrer el indice y los
; documentos que hubiera ahi para despues descartarlos.
Source: "{#Origen}\*"; DestDir: "{app}"; \
    Excludes: "StudIA,*.exp,*.lib,*.pdb,qmltooling"; \
    Flags: recursesubdirs createallsubdirs ignoreversion

; Modelo de embeddings (bge-m3). Sin el la busqueda sigue andando, pero solo por
; palabras: se pierde la semantica.
Source: "{#Origen}\StudIA\modelos\*"; DestDir: "{app}\StudIA\modelos"; Flags: ignoreversion

Source: "{#Origen}\StudIA\instalar_dependencias.bat"; DestDir: "{app}\StudIA"; Flags: ignoreversion
Source: "{#Origen}\StudIA\instalar_dependencias.ps1"; DestDir: "{app}\StudIA"; Flags: ignoreversion

; Las instrucciones, tambien instaladas: quien las pierda las tiene a mano desde
; el menu Inicio en vez de tener que volver a pedirlas.
Source: "Instrucciones de instalación.txt"; DestDir: "{app}"; Flags: ignoreversion

[Icons]
Name: "{group}\{#Nombre}"; Filename: "{app}\{#Ejecutable}"
Name: "{group}\Instalar funciones extra"; Filename: "{app}\StudIA\instalar_dependencias.bat"
Name: "{group}\Instrucciones de instalación"; Filename: "{app}\Instrucciones de instalación.txt"
Name: "{group}\Desinstalar {#Nombre}"; Filename: "{uninstallexe}"
Name: "{autodesktop}\{#Nombre}"; Filename: "{app}\{#Ejecutable}"; Tasks: escritorio

[Registry]
; Donde quedo instalado. Queda para que cualquier herramienta externa sepa
; encontrar la instalacion sin preguntarle nada al usuario.
Root: HKA; Subkey: "Software\StudIA"; ValueType: string; ValueName: "InstallDir"; \
    ValueData: "{app}"; Flags: uninsdeletekey
Root: HKA; Subkey: "Software\StudIA"; ValueType: string; ValueName: "Version"; \
    ValueData: "{#Version}"

[Run]
Filename: "{app}\StudIA\instalar_dependencias.bat"; \
    Description: "Instalar las funciones extra"; \
    Flags: postinstall shellexec waituntilterminated; Tasks: extras
Filename: "{app}\{#Ejecutable}"; Description: "Abrir {#Nombre} ahora"; \
    Flags: postinstall nowait skipifsilent

[UninstallDelete]
; Por si el usuario dejo el material adentro de la carpeta del programa: sin
; esto, desinstalar deja varios GB huerfanos que despues nadie encuentra. Si lo
; puso en otro lado (lo recomendado), no se toca: no es nuestro.
Type: filesandordirs; Name: "{app}\StudIA\DATA_StudIA"
Type: files; Name: "{app}\StudIA\studia.db"

[Code]
// StudIA descarga por su cuenta el motor del modelo (llama.cpp) y necesita
// lugar para eso ademas de lo que ocupa el paquete. Con el disco al limite la
// instalacion entra igual y el programa falla despues, que es peor.
function InitializeSetup(): Boolean;
var
  Libre, Total: Int64;
  Unidad: String;
begin
  Result := True;
  Unidad := ExtractFileDrive(ExpandConstant('{autopf}')) + '\';
  if GetSpaceOnDisk64(Unidad, Libre, Total) then
    if Libre < Int64(6) * 1024 * 1024 * 1024 then
      Result := MsgBox('En ' + Unidad + ' quedan menos de 6 GB libres.' + #13#10#13#10 +
                       'StudIA ocupa cerca de 1,6 GB, y despues necesita bajar el ' +
                       'motor del modelo y el modelo de chat.' + #13#10#13#10 +
                       'Instalar igual?', mbConfirmation, MB_YESNO) = IDYES;
end;
