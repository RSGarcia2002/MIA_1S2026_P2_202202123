# Estado hacia 100 - Proyecto 2

Fecha: 2026-04-21

## Estado tecnico actual

- Backend AWS EC2 operativo: `http://3.22.95.46:8080/health`
- Frontend S3 operativo: `http://mia-p2-202202123-017091937199-us-east-2.s3-website.us-east-2.amazonaws.com`
- Servicio backend persistente con `systemd`.
- Security Group con SSH restringido a IP actual.

## Funcionalidad validada

- Comandos P2 implementados y funcionales:
  - `unmount`
  - `loss`
  - `journaling`
  - `remove`
  - `rename`
  - `copy`
  - `move`
  - `find`
  - `chown`
  - `chmod`
  - `mkfs -fs=2fs|3fs`
- Compatibilidad adicional para defensa:
  - `mkfile` ahora acepta `-cont` y `-contenido`
  - `edit` implementado
  - `recovery` implementado (reconstruye desde journal)

## Archivos clave

- Script demo principal: `demo_p2.smia`
- Script estilo auxiliar (sin cambios de estilo): `prueba_auxiliar_compat_p2.smia`
- Checklist de rubrica: `P2_CHECKLIST.md`

## Pendientes para cerrar 100

1. Ejecutar corrida final de demostracion grabando evidencias (capturas/video corto).
2. Confirmar la misma corrida en Linux local (entorno final de evaluacion).
3. Entregar documentacion final (manual tecnico + usuario + arquitectura).
4. Rotar Access Keys expuestas previamente y dejar solo credenciales vigentes.

## Lista de capturas sugerida (rubrica)

1. EC2 corriendo backend + `curl /health`.
2. Security Group (22 restringido, 8080 abierto segun rubrica).
3. S3 bucket con static website hosting activo.
4. URL publica del frontend funcionando.
5. GUI navegando carpetas/archivos.
6. Ejecucion de comandos nuevos (incl. errores intencionales).
7. `loss` + `journaling` + `recovery`.
8. Reportes generados (`mbr`, `disk`, `tree`, `inode`, `block`).

