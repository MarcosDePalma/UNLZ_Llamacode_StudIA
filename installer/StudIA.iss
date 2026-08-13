; StudIA — instalador principal
;
; Deja andando la app completa: ejecutable, indice del corpus (studia.db) y el
; modelo de embeddings. Los documentos ORIGINALES no vienen aca: son 7,7 GB y
; solo sirven para abrir el PDF desde una cita. El texto citado esta adentro del
; indice, asi que el chat, las citas, la abstencion y los modos funcionan igual.
; Quien los quiera instala despues StudIA-Corpus.
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
; La app. Se excluye la carpeta StudIA entera (va explicita mas abajo): ahi
; adentro vive el enlace a DATA_StudIA y, si se dejara pasar, el compilador se
; metaria a recorrer 7,7 GB de PDFs para despues descartarlos.
Source: "{#Origen}\*"; DestDir: "{app}"; \
    Excludes: "StudIA,*.exp,*.lib,*.pdb,qmltooling"; \
    Flags: recursesubdirs createallsubdirs ignoreversion

; El indice: es el corazon de StudIA y lo unico que no se puede regenerar sin el
; corpus original.
Source: "{#Origen}\StudIA\studia.db"; DestDir: "{app}\StudIA"; Flags: ignoreversion

; Modelo de embeddings (bge-m3). Sin el la busqueda sigue andando, pero solo por
; palabras: se pierde la semantica.
Source: "{#Origen}\StudIA\modelos\*"; DestDir: "{app}\StudIA\modelos"; Flags: ignoreversion

Source: "{#Origen}\StudIA\instalar_dependencias.bat"; DestDir: "{app}\StudIA"; Flags: ignoreversion
Source: "{#Origen}\StudIA\instalar_dependencias.ps1"; DestDir: "{app}\StudIA"; Flags: ignoreversion

; Para quien reciba ademas los documentos originales en un pendrive. No hacen
; nada por si solos, pero tenerlos instalados evita depender de que el pendrive
; traiga tambien los scripts.
Source: "{#Origen}\StudIA\copiar_documentos.bat"; DestDir: "{app}\StudIA"; Flags: ignoreversion
Source: "{#Origen}\StudIA\copiar_documentos.ps1"; DestDir: "{app}\StudIA"; Flags: ignoreversion

[Icons]
Name: "{group}\{#Nombre}"; Filename: "{app}\{#Ejecutable}"
Name: "{group}\Instalar funciones extra"; Filename: "{app}\StudIA\instalar_dependencias.bat"
Name: "{group}\Desinstalar {#Nombre}"; Filename: "{uninstallexe}"
Name: "{autodesktop}\{#Nombre}"; Filename: "{app}\{#Ejecutable}"; Tasks: escritorio

[Registry]
; Donde quedo instalado. Lo lee el instalador del corpus para dejar los
; documentos en el lugar correcto sin preguntarle nada al usuario.
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
; Los documentos que haya dejado el instalador del corpus: si no, desinstalar
; deja 7,7 GB huerfanos que nadie encuentra despues.
Type: filesandordirs; Name: "{app}\StudIA\DATA_StudIA"

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
