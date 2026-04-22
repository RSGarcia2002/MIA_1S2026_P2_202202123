# Manual Tecnico Proyecto 2 - EXTREAMFS

## 1. Datos Generales

- Proyecto: MIA_1S2026_P2_202202123
- Estudiante: Randall Garcia
- Carnet: 202202123
- Prefijo de IDs montadas: `23`

## 2. Arquitectura

- Backend C++:
  - Parser y dispatcher de comandos (`Analyzer`)
  - Disco/particiones (`DiskManagement`)
  - Formateo EXT2/EXT3 + journal (`FileSystem`)
  - Operaciones de archivos y permisos (`FileOperations`)
  - Usuarios/sesion (`UserSession`)
  - Reporteria (`Reports`)
  - API HTTP propia con sockets POSIX (`HttpServer`)
- Frontend React + Vite:
  - Editor de scripts
  - Salida de comandos
  - Visualizador de reportes
  - Explorador de archivos (`/fs/*`)

## 3. Estructuras y Layout

- MBR / Partition / EBR
- SuperBloque
- Inodo
- BloqueDir
- BloqueFile
- BloqueApunt
- JournalEntry (EXT3)

## 4. IDs de Montaje por Carnet

En `DiskManagement`, la constante `CARNET_DIGITS` esta en `"23"`.
El formato final es:

`<23><numero_montaje_en_disco><letra_disco>`

Ejemplos:
- `231A`
- `232A`
- `231B`

## 5. Comandos Implementados

- Disco/particion: `mkdisk`, `rmdisk`, `fdisk`, `mount`, `unmount`, `mounted`
- FS: `mkfs (2fs|3fs)`, `loss`, `journaling`, `recovery`
- Sesion/usuarios: `login`, `logout`, `mkgrp`, `rmgrp`, `mkusr`, `rmusr`, `chgrp`
- Archivos/carpetas: `mkdir`, `mkfile`, `edit`, `cat`, `remove`, `rename`, `copy`, `move`, `find`
- Permisos: `chmod`, `chown`
- Reportes: `rep`

Compatibilidad:
- `mkfile` acepta `-cont` y `-contenido`
- `remove` soporta `-r`

## 6. API HTTP

- `GET /health`
- `POST /execute` (JSON `{ "commands": "..." }`)
- `GET /report?path=...`
- `GET /fs/mounted`
- `GET /fs/browse?id=...&path=...`
- `GET /fs/file?id=...&path=...`

## 7. Build y Run

```bash
cmake -S . -B build
cmake --build build -j1
./build/MIA_P1 --server 8080
```

Frontend:

```bash
npm install
npm run dev
```

## 8. Evidencia de Validacion

- `VALIDACION_LINUX_P2.md`
- `ESTADO_RUTA_100.md`
- `demo_p2.smia`
- `prueba_auxiliar_compat_p2.smia`

