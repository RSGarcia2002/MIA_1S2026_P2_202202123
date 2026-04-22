# EXTREAMFS - Proyecto 2

Universidad de San Carlos de Guatemala  
Facultad de Ingenieria  
Curso: Manejo e Implementacion de Archivos  

Estudiante: Randall Garcia  
Carnet: 202202123

## Descripcion

EXTREAMFS es un simulador de sistema de archivos EXT2/EXT3 en C++ con interfaz web.
Incluye manejo de discos, particiones, sistema de archivos, usuarios, reportes y
despliegue cloud (Frontend S3 + Backend EC2).

## Funcionalidad Implementada

- Administracion de discos: `mkdisk`, `rmdisk`, `fdisk` (`-add`, `-delete`)
- Montaje: `mount`, `unmount`, `mounted`
- Formateo: `mkfs -fs=2fs|3fs`
- Sesion/usuarios: `login`, `logout`, `mkgrp`, `rmgrp`, `mkusr`, `rmusr`, `chgrp`
- Archivos/carpetas: `mkdir`, `mkfile`, `cat`, `edit`, `remove`, `rename`, `copy`, `move`, `find`
- Permisos/propietario: `chmod`, `chown`
- Reportes: `rep`
- EXT3: `journaling`, `loss`, `recovery`

## Regla de IDs por Carnet

El sistema usa los ultimos dos digitos del carnet para IDs montados.  
Para carnet `202202123` se usa prefijo `23`.  
Ejemplos: `231A`, `232A`, `231B`.

## Endpoints HTTP

- `GET /health`
- `POST /execute`
- `GET /report?path=...`
- `GET /fs/mounted`
- `GET /fs/browse?id=...&path=...`
- `GET /fs/file?id=...&path=...`

## Requisitos

- Linux recomendado
- `g++`, `cmake`, `graphviz`
- `node`, `npm`

Ubuntu:

```bash
sudo apt update
sudo apt install -y build-essential cmake graphviz nodejs npm
```

## Compilacion y Ejecucion

Backend:

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

Build produccion:

```bash
npm run build
```

## Despliegue AWS

- Backend en EC2
- Frontend estatico en S3
- Variable de frontend:

```bash
VITE_API_BASE_URL=http://<EC2_PUBLIC_IP>:8080
```

Referencia de despliegue:
- `AWS_DEPLOY_P2.md`
- `deploy/ec2_backend_setup.sh`
- `deploy/frontend_build_prod.sh`

## Archivos de Demostracion

- `demo_p2.smia`
- `prueba_auxiliar_compat_p2.smia`
- `VALIDACION_LINUX_P2.md`
- `ESTADO_RUTA_100.md`

## Repositorio

https://github.com/RSGarcia2002/MIA_1S2026_P2_202202123
