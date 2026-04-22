# Validacion Linux Proyecto 2

Fecha: 2026-04-21
Repo validado: `RSGarcia2002/MIA_1S2026_P2_202202123`
Ruta local: `/tmp/MIA_1S2026_P2_202202123`

## Comandos ejecutados

```bash
git clone https://github.com/RSGarcia2002/MIA_1S2026_P2_202202123.git /tmp/MIA_1S2026_P2_202202123
cmake -S . -B build
cmake --build build -j1
./build/MIA_P1 --server 8080
curl -sS http://127.0.0.1:8080/health
npm install
node -e '<cliente HTTP>' demo_p2.smia
node -e '<cliente HTTP>' prueba_auxiliar_compat_p2.smia
```

## Resultados

- Backend C++ compila en Linux con GCC 13.3.0.
- `/health` responde: `{"ok":true,"service":"MIA_P2"}`.
- `demo_p2.smia` ejecuta correctamente `mkdisk`, `fdisk -add`, `fdisk -delete`, `mount`, `mkfs -fs=3fs`, `login`, `mkdir`, `mkfile -cont`, `cat`, `copy`, `rename`, `find`, `chmod`, `chown`, `move`, `remove`, reportes, `journaling`, `loss`, `logout` y `unmount`.
- Reportes generados: `/tmp/p2_mbr.png`, `/tmp/p2_disk.png`, `/tmp/p2_tree.png`, `/tmp/p2_inode.png`, `/tmp/p2_block.png`.
- `prueba_auxiliar_compat_p2.smia` ejecuta la ruta auxiliar con errores intencionales y finaliza con `RECOVERY completado`, `Fallidos: 0` y `No hay particiones montadas actualmente.`
- `loss -> journaling -> recovery` queda validado en EXT3 sobre `232A` dentro del auxiliar.
- `edit -contenido`, `copy`, `move`, `remove`, `rename`, `find`, `chown` y `chmod` quedan validados.

## Errores esperados del auxiliar

- Parametros incorrectos: `mkdisk -param`, `mkdisk -tamaño`.
- Disco, particion o id inexistente.
- Particion ya montada.
- Grupo/usuario duplicado o inexistente.
- Directorio padre inexistente sin `-p`/`-r`.
- `mkfile` con size negativo.
- `chmod` con permisos fuera de rango.
- `copy`, `move`, `edit`, `remove` sobre rutas inexistentes.
- `remove` de carpeta no vacia sin `-r`.

## Cambios aplicados para compatibilidad Linux

- Se reemplazo Crow por un servidor HTTP minimo POSIX en `HttpServer/HttpServer.cpp`, eliminando la dependencia de Boost no instalada en el host.
- `remove` ahora respeta `-r` para carpetas no vacias.
- Se corrigio almacenamiento/listado de nombres de 12 bytes exactos en directorios.
- Se corrigio `chown` al resolver usuarios desde `users.txt`.
- `copy` y `move` devuelven mensajes propios y no crean padres en destinos explicitos inexistentes.
- Scripts ajustados a rutas Linux `/tmp`, IDs reales del backend y nombres compatibles con `b_name[12]`.

## Pendientes para 100/100

- Instalar `npm` en este host Linux y ejecutar `npm install && npm run build`.
- Si se usa defensa cloud, capturar evidencia de EC2/S3 reales segun `ESTADO_RUTA_100.md`.
- Subir/commitear estos ajustes al repo remoto P2 si se quiere conservar la validacion final.

## Capturas recomendadas

1. `cmake --build build -j1` terminando con `Built target MIA_P1`.
2. Backend en `./build/MIA_P1 --server 8080`.
3. `curl http://127.0.0.1:8080/health`.
4. Salida del demo mostrando `OK [copy]`, `OK [rename]`, `OK [chmod]`, `OK [chown]`, `OK [move]`, `OK [remove]`.
5. Salida de `journaling` antes y despues de `loss`.
6. Salida de `RECOVERY completado` con `Fallidos: 0`.
7. `mounted` final mostrando que no hay particiones montadas.
8. Archivos de reporte generados en `/tmp`: `mbr`, `disk`, `tree`, `inode`, `block`.
9. EC2 backend `/health`, Security Group puerto 8080, S3 static hosting y frontend publico funcionando.
