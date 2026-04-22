# Checklist Proyecto 2 (Objetivo 100/100)

## 1) Comandos previos (5 pts)
- [x] `mkfs` funciona en particion montada.
- [x] `login` con root funciona.
- [x] `logout` funciona.
- [x] `mkdir` funciona en rutas nuevas.
- [x] `mkfile` funciona y se puede verificar con `cat`.

## 2) Infraestructura nube + aplicacion web (45 pts)
- [x] Backend C++ desplegado en EC2 Linux.
- [x] Security Groups permiten trafico al puerto del backend.
- [x] Frontend desplegado en S3 (sitio estatico).
- [x] Politica/permisos de bucket S3 correctos.
- [x] Link web de S3 accesible publicamente.
- [x] Frontend se conecta al backend EC2 en ejecucion real.
- [x] Flujo GUI: iniciar sesion, seleccionar disco/particion, navegar carpetas, abrir archivos.

## 3) Nuevos comandos (24 pts)
- [x] `fdisk` con `-add`.
- [x] `fdisk` con `-delete`.
- [x] `unmount`.
- [x] `mkfs` con `-fs=2fs|3fs`.
- [x] `remove`.
- [x] `rename`.
- [x] `copy`.
- [x] `move`.
- [x] `find`.
- [x] `chown`.
- [x] `chmod`.

## 4) Journaling (14 pts)
- [x] `mkfs -fs=3fs` crea EXT3.
- [x] `loss` simula perdida de estructura.
- [x] `journaling` muestra operaciones registradas.
- [x] Evidencia en GUI (capturas/video corto) de `loss` + `journaling`.

## 5) Documentacion y defensa (12 pts)
- [x] Manual tecnico actualizado a P2.
- [x] Manual de usuario actualizado con flujo GUI y cloud.
- [x] Arquitectura final: Frontend S3 -> Backend EC2.
- [x] Script de demostracion listo para evaluacion.
- [x] Respuestas de defensa preparadas (permisos, inodos, bitmaps, journal, endpoints).

## Riesgos a evitar
- No dejarlo local solamente (penaliza fuerte en P2).
- No usar otro proveedor fuera de AWS.
- No depender de pasos manuales ambiguos para correr demo.

## Evidencias asociadas
- `VALIDACION_LINUX_P2.md`
- `ESTADO_RUTA_100.md`
- `MANUAL_TECNICO_P2.md`
- `MANUAL_USUARIO_P2.md`
- `ARQUITECTURA_P2.md`
