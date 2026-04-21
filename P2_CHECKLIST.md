# Checklist Proyecto 2 (Objetivo 100/100)

## 1) Comandos previos (5 pts)
- [ ] `mkfs` funciona en particion montada.
- [ ] `login` con root funciona.
- [ ] `logout` funciona.
- [ ] `mkdir` funciona en rutas nuevas.
- [ ] `mkfile` funciona y se puede verificar con `cat`.

## 2) Infraestructura nube + aplicacion web (45 pts)
- [ ] Backend C++ desplegado en EC2 Linux.
- [ ] Security Groups permiten trafico al puerto del backend.
- [ ] Frontend desplegado en S3 (sitio estatico).
- [ ] Politica/permisos de bucket S3 correctos.
- [ ] Link web de S3 accesible publicamente.
- [ ] Frontend se conecta al backend EC2 en ejecucion real.
- [ ] Flujo GUI: iniciar sesion, seleccionar disco/particion, navegar carpetas, abrir archivos.

## 3) Nuevos comandos (24 pts)
- [ ] `fdisk` con `-add`.
- [ ] `fdisk` con `-delete`.
- [ ] `unmount`.
- [ ] `mkfs` con `-fs=2fs|3fs`.
- [ ] `remove`.
- [ ] `rename`.
- [ ] `copy`.
- [ ] `move`.
- [ ] `find`.
- [ ] `chown`.
- [ ] `chmod`.

## 4) Journaling (14 pts)
- [ ] `mkfs -fs=3fs` crea EXT3.
- [ ] `loss` simula perdida de estructura.
- [ ] `journaling` muestra operaciones registradas.
- [ ] Evidencia en GUI (capturas/video corto) de `loss` + `journaling`.

## 5) Documentacion y defensa (12 pts)
- [ ] Manual tecnico actualizado a P2.
- [ ] Manual de usuario actualizado con flujo GUI y cloud.
- [ ] Arquitectura final: Frontend S3 -> Backend EC2.
- [ ] Script de demostracion listo para evaluacion.
- [ ] Respuestas de defensa preparadas (permisos, inodos, bitmaps, journal, endpoints).

## Riesgos a evitar
- No dejarlo local solamente (penaliza fuerte en P2).
- No usar otro proveedor fuera de AWS.
- No depender de pasos manuales ambiguos para correr demo.

