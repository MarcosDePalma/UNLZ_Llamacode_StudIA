# Probar StudIA en una PC limpia

El instalador se armó y se verificó **en la máquina donde se programó**. Eso deja
una pregunta abierta que ninguna prueba local puede contestar: qué pasa en una
computadora que no tiene Visual Studio, ni Python, ni Qt, ni nada.

Este documento explica dos formas de averiguarlo, de menor a mayor esfuerzo.

---

## Antes de la máquina virtual: un usuario nuevo de Windows

Toma cinco minutos y no requiere instalar nada.

StudIA guarda cosas **por usuario**: los perfiles, el motor del modelo que baja
LlamaCode (`AppData\Roaming\LlamaCode\tools`), los modelos y la configuración.
Un usuario recién creado tiene todo eso vacío, igual que una PC nueva.

    Configuración → Cuentas → Otros usuarios → Agregar cuenta
    → "No tengo los datos de esta persona" → "Agregar un usuario sin cuenta Microsoft"

Después iniciás sesión con ese usuario y corrés el instalador.

**Qué detecta:** rutas que sin darte cuenta apuntan a tu carpeta personal,
configuración que creías que venía por defecto y en realidad la habías tocado,
el índice que no se encuentra, los perfiles duplicados.

**Qué NO detecta:** todo lo que se instala para toda la máquina y ya está puesto
en la tuya — Python, Node, Tesseract, el runtime de Visual C++. Para eso hace
falta la máquina virtual.

---

## La máquina virtual

### Por qué VirtualBox y no otra cosa

Windows trae dos sistemas de virtualización, **Hyper-V** y **Windows Sandbox**,
y los dos piden Windows 11 **Pro**. Esta máquina tiene Windows 11 **Home**: no
están disponibles y no se pueden activar.

La alternativa es **VirtualBox**, de Oracle, que es gratis y funciona en Home.

### Lo que hace falta

| | |
|---|---|
| Espacio en disco | ~64 GB (hay 201 GB libres en C:) |
| RAM para la VM | 6 GB de los 16 GB de la máquina |
| Núcleos para la VM | 4 de los 12 |
| Virtualización por hardware | ya está habilitada en el BIOS |

### Los pasos

**1. Instalar VirtualBox**

```
winget install --id Oracle.VirtualBox --source winget
```

**2. Bajar la imagen de Windows 11**

Está en el sitio de Microsoft, gratis y sin registrarse: buscá "Descargar
Windows 11" y elegí *ISO de dispositivo x64*. Son unos 6 GB.

Windows sin activar funciona sin límite de tiempo. Sólo aparece una marca de
agua en el escritorio y no deja cambiar el fondo de pantalla. Para probar una
instalación no molesta en nada.

**3. Crear la máquina virtual**

En VirtualBox: *Nueva* → nombre "StudIA limpia" → tipo Windows 11 → 6144 MB de
RAM → 4 CPUs → disco de 64 GB. En la pantalla de instalación desmarcá
*Skip Unattended Installation* sólo si querés hacer la instalación a mano.

**4. Instalar Windows dentro de la VM**

Es el instalador normal de Windows. Un detalle: Windows 11 insiste en
conectarse a internet y usar una cuenta Microsoft. Cuando llegue a esa pantalla,
`Shift + F10` abre una consola y ahí:

```
start ms-cxh:localonly
```

Eso habilita crear una cuenta local. (En algunas versiones el comando es
`oobe\bypassnro`; si uno no anda, probá el otro.)

**5. Pasar el instalador a la VM**

La forma simple es una carpeta compartida: *Dispositivos → Carpetas compartidas*
en el menú de VirtualBox. Para que funcione hay que instalar antes las
*Guest Additions* (*Dispositivos → Insertar imagen de CD de las Guest Additions*).

Si se complica, subir `StudIA-1.0-Setup.exe` a Drive y bajarlo desde el
navegador de la VM también sirve.

### Qué mirar cuando lo corras

Esto es lo que la prueba tiene que contestar:

1. **¿Abre?** Si falta una DLL, Windows lo dice en un cartel apenas arranca.
   Es el escenario que cubren las copias de `msvcp140.dll` y `vcruntime140*.dll`
   que el instalador deja al lado del ejecutable.
2. **¿Encuentra su índice?** StudIA tiene que arrancar con las 49 materias
   cargadas sin que le indiques nada.
3. **¿Levanta la búsqueda semántica?** El servidor de embeddings corre en CPU,
   así que en la VM **sí** tiene que funcionar. Si dice que no está disponible,
   hay un problema real.
4. **¿El script de dependencias instala lo que falta?** Esta es la parte que en
   la máquina de desarrollo **no se pudo probar**, porque ahí ya estaba todo
   instalado. Es el motivo principal de hacer esta prueba.
5. **¿Las citas avisan?** Sin los documentos originales, hacer clic en una cita
   tiene que mostrar un mensaje explicando que esta copia no los incluye.
6. **¿El copiador de documentos funciona?** Si además le pasás la carpeta
   `DATA_StudIA`, `copiar_documentos.bat` tiene que dejarla en `C:\StudIA_Docs`
   y hacer que las citas empiecen a abrir los archivos.

### Qué NO vas a poder probar

**La velocidad del chat.** VirtualBox no le pasa la placa de video a la máquina
virtual, así que el modelo de chat corre en CPU y va a ir lentísimo. Eso es una
limitación de la prueba, no un problema de StudIA.

Para que el chat responda algo, adentro de la VM hay que dejar que LlamaCode
baje el motor y un modelo — varios GB más. Si sólo querés verificar la
instalación, podés saltear esa parte: los puntos 1 a 5 no dependen del modelo de
chat.

---

## En la PC del profesor, que ya tiene LlamaCode

Es el caso más fácil de todos, porque lo pesado ya está hecho.

StudIA es un fork y **comparte identidad** con el LlamaCode original
(`setApplicationName("LlamaCode")`, `setOrganizationName("LlamaCode")`). Las dos
versiones leen la misma carpeta de datos, así que al instalar StudIA:

- **se reutiliza** el motor (llama.cpp) que ya bajó;
- **se reutiliza** el modelo de chat que ya tenga;
- **se reutilizan** sus perfiles y su configuración.

Lo que sí necesita es el ejecutable nuevo: StudIA está compilado adentro de
`LlamaCode.exe`, no es un complemento que se agregue aparte.

En la práctica: corre `StudIA-1.0-Setup.exe`, y de los 1,21 GB lo único
verdaderamente nuevo son el índice y el modelo de embeddings.

---

## Qué se entrega

**Obligatorio — `StudIA-1.0-Setup.exe` (1,21 GB).** Un archivo. Con esto StudIA
funciona entero: chat, búsqueda semántica, citas, abstención y modos.

**Opcional — la carpeta `DATA_StudIA` (7,7 GB) en un pendrive**, junto con
`copiar_documentos.bat`. Sirve sólo para abrir el documento original desde una
cita. Sin ella, StudIA responde exactamente igual y avisa cuando el archivo no
está.

No hay una versión de esto que quepa en un solo archivo. Windows no admite un
`.exe` de más de 4,2 GB, y varias rutas del corpus pasan los 260 caracteres que
el sistema acepta — el máximo relativo es de 261, así que no entrarían ni
poniéndolas en la raíz del disco. Por eso el copiador usa `robocopy`, que es la
herramienta que sí las maneja, y por eso el destino es corto.

Si desinstalás StudIA, los documentos copiados quedan en `C:\StudIA_Docs`:
están afuera de la carpeta de instalación, y un desinstalador que borre cosas
fuera de su propio directorio es una mala idea. Se borran a mano.
